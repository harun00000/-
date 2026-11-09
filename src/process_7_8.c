#define _POSIX_C_SOURCE 200809L
#include "process_7_8.h"
#include "check.h"
#include "config.h"
#include "logger.h"
#include "main_utils.h"
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

enum
{
    SESSION_MAX = 40,
    IPC_NAME_SIZE = 96,
    SHARED_MAGIC = 0x783801,
    SEM_STATE = 0,
    SEM_LIBRARIAN = 1,
    SEM_DONE,
    SEM_READY,
    SEM_EXITED,
    SEM_READERS,
    MAX_SEMAPHORES = SEM_READERS + MAX_READERS
};

// в общей памяти нет указателей и дескрипторов семафоров
typedef struct SessionShared
{
    int magic;
    Simulation simulation;
    int librarian_connected;
    int reader_connected[MAX_READERS];
    int connected;
    int exited;
    bool accepting;
    bool failed;
} SessionShared;

typedef struct SessionOptions
{
    const char *session;
    const char *config_filename;
    int total_days;
    int reading_days;
    int reader_index;
    bool has_total_days;
    bool has_reading_days;
    bool has_reader;
    bool help;
} SessionOptions;

// каждый процесс хранит свои отображение и открытые дескрипторы
typedef struct SessionResources
{
    char prefix[IPC_NAME_SIZE];
    char shm_name[IPC_NAME_SIZE];
    SessionShared *shared;
    sem_t *semaphores[MAX_SEMAPHORES];
    bool created[MAX_SEMAPHORES];
    bool shm_created;
    bool model_ready;
    bool registered;
} SessionResources;

typedef enum WaitResult
{
    WAIT_READY,
    WAIT_TIMEOUT,
    WAIT_STOPPED,
    WAIT_FAILED
} WaitResult;

// ошибочное значение параметра возвращаем обычной ошибкой разбора
static bool parse_number(const char *text, int minimum, int *value)
{
    SOFT_ASSERT(text != NULL && value != NULL, "Нет строки числа или результата", false);
    SOFT_ASSERT(minimum >= 0, "Отрицательная нижняя граница разбора числа", false);
    char *end;
    errno = 0;
    long number = strtol(text, &end, 10);
    if (end == text || *end != '\0' || errno == ERANGE || number < minimum || number > INT_MAX)
    {
        return false;
    }
    *value = (int)number;
    return true;
}

// имя сессии не должно содержать разделители путей или превышать длину
static bool valid_session(const char *session)
{
    SOFT_ASSERT(session != NULL, "Указатель на имя сессии = NULL", false);
    size_t length = strlen(session);
    if (length == 0 || length > SESSION_MAX)
    {
        return false;
    }
    for (size_t idx = 0; idx < length; ++idx)
    {
        char ch = session[idx];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' ||
                ch == '-'))
        {
            return false;
        }
    }
    return true;
}

// один CLI helper разбирает только разрешённые для данной роли параметры
static bool parse_options(int argc, char *argv[], ProcessRole role, SessionOptions *options)
{
    SOFT_ASSERT(argc > 0 && argv != NULL && options != NULL, "Некорректные параметры CLI helper", false);
    SOFT_ASSERT(role == PROCESS_COORDINATOR || role == PROCESS_LIBRARIAN || role == PROCESS_READER,
        "Неизвестная роль в CLI helper", false);
    SOFT_ASSERT(argv[argc] == NULL, "Массив аргументов не завершён NULL", false);
    for (int idx = 0; idx < argc; ++idx)
    {
        SOFT_ASSERT(argv[idx] != NULL, "Указатель на аргумент = NULL", false);
    }
    *options = (SessionOptions){.config_filename = "config.txt"};
    static const struct option long_options[] = {{"session", required_argument, NULL, 's'},
        {"reader", required_argument, NULL, 'i'}, {"config", required_argument, NULL, 'c'},
        {"days", required_argument, NULL, 'd'}, {"reading-days", required_argument, NULL, 'r'},
        {"help", no_argument, NULL, 'h'}, {NULL, 0, NULL, 0}};
    opterr = 0;
    optind = 1;
    int option;
    while ((option = getopt_long(argc, argv, ":c:d:r:h", long_options, NULL)) != -1)
    {
        switch (option)
        {
            case 's':
                if (options->session != NULL || !valid_session(optarg))
                {
                    fprintf(stderr,
                        "Ошибка: --session задаётся один раз, допустимы 1-%d символов A-Z, a-z, 0-9, _, -\n",
                        SESSION_MAX);
                    return false;
                }
                options->session = optarg;
                break;
            case 'i':
                if (role != PROCESS_READER || options->has_reader || !parse_number(optarg, 0, &options->reader_index))
                {
                    fprintf(stderr, "Ошибка: --reader требует один неотрицательный индекс и доступен только Reader\n");
                    return false;
                }
                options->has_reader = true;
                break;
            case 'c':
                if (role != PROCESS_COORDINATOR || optarg[0] == '\0')
                {
                    fprintf(stderr, "Ошибка: -c доступен только Coordinator и требует имя config\n");
                    return false;
                }
                options->config_filename = optarg;
                break;
            case 'd':
            case 'r':
                if (role != PROCESS_COORDINATOR ||
                    !parse_number(optarg, 1, option == 'd' ? &options->total_days : &options->reading_days))
                {
                    fprintf(stderr, "Ошибка: -d/-r доступны только Coordinator и требуют положительное целое число\n");
                    return false;
                }
                if (option == 'd')
                {
                    options->has_total_days = true;
                }
                else
                {
                    options->has_reading_days = true;
                }
                break;
            case 'h':
                options->help = true;
                break;
            default:
                fprintf(stderr, "Ошибка: неизвестная опция или отсутствует значение: %s\n", argv[optind - 1]);
                return false;
        }
    }
    if (optind < argc)
    {
        fprintf(stderr, "Ошибка: лишний аргумент %s\n", argv[optind]);
        return false;
    }
    if (!options->help && (options->session == NULL || (role == PROCESS_READER && !options->has_reader)))
    {
        fprintf(stderr, "Ошибка: укажите --session NAME, а для Reader также --reader INDEX\n");
        return false;
    }
    return true;
}

// все программы получают одинаковые имена по session и назначению
static bool semaphore_name(const SessionResources *resources, int index, char name[IPC_NAME_SIZE])
{
    SOFT_ASSERT(resources != NULL && name != NULL, "Нет ресурсов или буфера имени", false);
    SOFT_ASSERT(memchr(resources->prefix, '\0', sizeof resources->prefix) != NULL && resources->prefix[0] == '/' &&
                    resources->prefix[1] != '\0' && strchr(resources->prefix + 1, '/') == NULL,
        "Некорректный внутренний префикс IPC", false);
    SOFT_ASSERT(index >= 0 && index < MAX_SEMAPHORES, "Индекс семафора вне массива", false);
    const char *roles[] = {"state", "librarian", "done", "ready", "exited"};
    int length;
    if (index < SEM_READERS)
    {
        length = snprintf(name, IPC_NAME_SIZE, "%s_%s", resources->prefix, roles[index]);
    }
    else
    {
        length = snprintf(name, IPC_NAME_SIZE, "%s_reader_%d", resources->prefix, index - SEM_READERS);
    }
    SOFT_ASSERT(length > 0 && length < IPC_NAME_SIZE, "Имя семафора не помещается в буфер", false);
    return true;
}

// только Coordinator создаёт имя, участники открывают существующее
static bool open_semaphore(SessionResources *resources, int index, bool create)
{
    SOFT_ASSERT(resources != NULL, "Указатель на ресурсы = NULL", false);
    SOFT_ASSERT(index >= 0 && index < MAX_SEMAPHORES, "Индекс семафора вне массива", false);
    SOFT_ASSERT(
        resources->semaphores[index] == SEM_FAILED && !resources->created[index], "Повторное открытие семафора", false);
    char name[IPC_NAME_SIZE];
    if (!semaphore_name(resources, index, name))
    {
        return false;
    }
    sem_t *semaphore = create ? sem_open(name, O_CREAT | O_EXCL, 0600, index == SEM_STATE ? 1 : 0) : sem_open(name, 0);
    if (semaphore == SEM_FAILED)
    {
        fprintf(stderr, "Не удалось %s семафор %s: %s\n", create ? "создать" : "открыть", name, strerror(errno));
        return false;
    }
    resources->semaphores[index] = semaphore;
    resources->created[index] = create;
    return true;
}

static bool post_semaphore(sem_t *semaphore)
{
    SOFT_ASSERT(semaphore != SEM_FAILED && semaphore != NULL, "Семафор не открыт", false);
    if (sem_post(semaphore) != 0)
    {
        perror("sem_post");
        return false;
    }
    return true;
}

// ограниченное ожидание позволяет заметить сигнал даже перед входом в ожидание
static WaitResult wait_once(sem_t *semaphore, bool interruptible)
{
    SOFT_ASSERT(semaphore != SEM_FAILED && semaphore != NULL, "Семафор не открыт", WAIT_FAILED);
    if (interruptible && stop_requested)
    {
        return WAIT_STOPPED;
    }
    struct timespec deadline;
    if (clock_gettime(CLOCK_REALTIME, &deadline) != 0)
    {
        perror("clock_gettime");
        return WAIT_FAILED;
    }
    deadline.tv_nsec += 100000000L;
    if (deadline.tv_nsec >= 1000000000L)
    {
        ++deadline.tv_sec;
        deadline.tv_nsec -= 1000000000L;
    }
    if (sem_timedwait(semaphore, &deadline) == 0)
    {
        return WAIT_READY;
    }
    if (interruptible && stop_requested)
    {
        return WAIT_STOPPED;
    }
    if (errno == EINTR || errno == ETIMEDOUT)
    {
        return WAIT_TIMEOUT;
    }
    perror("sem_timedwait");
    return WAIT_FAILED;
}

// таймаут позволяет повторить ожидание, остановка и ошибка завершают его
static WaitResult wait_semaphore(sem_t *semaphore, bool interruptible)
{
    WaitResult result;
    do
    {
        result = wait_once(semaphore, interruptible);
    } while (result == WAIT_TIMEOUT);
    return result;
}

// эти счётчики читаются и меняются только под блокировкой state
static bool registration_valid(const SessionShared *shared)
{
    SOFT_ASSERT(shared != NULL, "Указатель на общее состояние = NULL", false);
    SOFT_ASSERT(shared->simulation.reader_count > 0 && shared->simulation.reader_count <= MAX_READERS,
        "Количество читателей вне массива", false);
    SOFT_ASSERT(shared->librarian_connected == 0 || shared->librarian_connected == 1,
        "Некорректный признак подключения библиотекаря", false);
    int count = shared->librarian_connected;
    for (int idx = 0; idx < shared->simulation.reader_count; ++idx)
    {
        SOFT_ASSERT(shared->reader_connected[idx] == 0 || shared->reader_connected[idx] == 1,
            "Некорректный признак подключения читателя", false);
        count += shared->reader_connected[idx];
    }
    SOFT_ASSERT(shared->connected == count && shared->exited >= 0 && shared->exited <= shared->connected,
        "Счётчики регистрации не согласованы", false);
    return true;
}

// подключение проверяет размер объекта до обращения к данным модели
static bool map_shared(SessionResources *resources, bool create)
{
    SOFT_ASSERT(resources != NULL && resources->shared == MAP_FAILED, "Повторное отображение shared memory", false);
    SOFT_ASSERT(!resources->shm_created, "Повторное создание shared memory", false);
    SOFT_ASSERT(memchr(resources->shm_name, '\0', sizeof resources->shm_name) != NULL &&
                    resources->shm_name[0] == '/' && resources->shm_name[1] != '\0' &&
                    strchr(resources->shm_name + 1, '/') == NULL,
        "Некорректное внутреннее имя shared memory", false);
    int fd = shm_open(resources->shm_name, create ? O_CREAT | O_EXCL | O_RDWR : O_RDWR, 0600);
    if (fd == -1)
    {
        fprintf(stderr, "Не удалось %s сессию %s: %s\n", create ? "создать" : "открыть", resources->shm_name,
            strerror(errno));
        return false;
    }
    resources->shm_created = create;
    bool success = true;
    if (create)
    {
        if (ftruncate(fd, (off_t)sizeof *resources->shared) != 0)
        {
            perror("ftruncate");
            success = false;
        }
    }
    else
    {
        struct stat status;
        if (fstat(fd, &status) != 0)
        {
            perror("fstat");
            success = false;
        }
        else if (status.st_size != (off_t)sizeof *resources->shared)
        {
            fprintf(stderr, "Сессия ещё не готова или имеет несовместимый формат\n");
            success = false;
        }
    }
    if (success)
    {
        resources->shared = mmap(NULL, sizeof *resources->shared, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (resources->shared == MAP_FAILED)
        {
            perror("mmap");
            success = false;
        }
    }
    if (close(fd) != 0)
    {
        perror("close (shared memory)");
        success = false;
    }
    return success;
}

// публикуем ready последним, после подготовки модели и остальных семафоров
static bool prepare_coordinator(SessionResources *resources, const Simulation *simulation)
{
    SOFT_ASSERT(resources != NULL && simulation != NULL, "Нет ресурсов или модели", false);
    SOFT_ASSERT(simulation->reader_count > 0 && simulation->reader_count <= MAX_READERS,
        "Количество читателей вне массива", false);
    if (!map_shared(resources, true))
    {
        return false;
    }
    *resources->shared = (SessionShared){.magic = SHARED_MAGIC, .simulation = *simulation};
    resources->model_ready = true;
    for (int idx = 0; idx < SEM_READERS + simulation->reader_count; ++idx)
    {
        if (idx != SEM_READY && !open_semaphore(resources, idx, true))
        {
            return false;
        }
    }
    // ready создаём последним, когда к остальным объектам уже можно подключаться
    if (wait_semaphore(resources->semaphores[SEM_STATE], false) != WAIT_READY)
    {
        return false;
    }
    resources->shared->accepting = true;
    bool success = open_semaphore(resources, SEM_READY, true);
    if (!post_semaphore(resources->semaphores[SEM_STATE]))
    {
        success = false;
    }
    return success;
}

// проверяем индекс и дубликаты под state, затем подтверждаем регистрацию
static bool connect_participant(SessionResources *resources, ProcessRole role, int reader_index)
{
    SOFT_ASSERT(resources != NULL, "Указатель на ресурсы = NULL", false);
    SOFT_ASSERT(role == PROCESS_LIBRARIAN || role == PROCESS_READER, "Некорректная роль участника", false);
    if (!map_shared(resources, false))
    {
        return false;
    }
    for (int idx = 0; idx < SEM_READERS; ++idx)
    {
        if (!open_semaphore(resources, idx, false))
        {
            return false;
        }
    }
    if (wait_semaphore(resources->semaphores[SEM_STATE], true) != WAIT_READY)
    {
        return false;
    }
    SessionShared *shared = resources->shared;
    bool success = false;
    if (shared->magic != SHARED_MAGIC || shared->simulation.reader_count <= 0 ||
        shared->simulation.reader_count > MAX_READERS)
    {
        fprintf(stderr, "Сессия имеет несовместимое общее состояние\n");
    }
    else if (!shared->accepting || shared->simulation.finished)
    {
        fprintf(stderr, "Сессия уже завершает работу\n");
    }
    else if (role == PROCESS_READER && (reader_index < 0 || reader_index >= shared->simulation.reader_count))
    {
        fprintf(stderr, "Ошибка: индекс Reader должен быть от 0 до %d\n", shared->simulation.reader_count - 1);
    }
    else
    {
        int *connected =
            role == PROCESS_LIBRARIAN ? &shared->librarian_connected : &shared->reader_connected[reader_index];
        if (*connected)
        {
            fprintf(stderr,
                role == PROCESS_LIBRARIAN ? "Ошибка: Librarian уже подключён\n" : "Ошибка: Reader уже подключён\n");
        }
        else if (role == PROCESS_LIBRARIAN || open_semaphore(resources, SEM_READERS + reader_index, false))
        {
            *connected = 1;
            ++shared->connected;
            resources->registered = true;
            success = registration_valid(shared);
            if (role == PROCESS_LIBRARIAN)
            {
                printf("Librarian подключён\n");
            }
            else
            {
                printf("Reader %d подключён\n", reader_index);
            }
        }
    }
    if (!post_semaphore(resources->semaphores[SEM_STATE]))
    {
        success = false;
    }
    if (success)
    {
        success = post_semaphore(resources->semaphores[SEM_READY]);
    }
    return success;
}

// ожидание фазы прерывается при сигнале или досрочном выходе участника
static WaitResult coordinator_wait(SessionResources *resources, int index)
{
    SOFT_ASSERT(resources != NULL && resources->model_ready, "Общее состояние не подготовлено", WAIT_FAILED);
    SOFT_ASSERT(resources->shared != NULL && resources->shared != MAP_FAILED,
        "Отображение shared memory не подготовлено", WAIT_FAILED);
    SOFT_ASSERT(index == SEM_READY || index == SEM_DONE, "Ожидается неверный этап", WAIT_FAILED);
    while (true)
    {
        WaitResult result = wait_semaphore(resources->semaphores[SEM_STATE], true);
        if (result != WAIT_READY)
        {
            return result;
        }
        bool failed = resources->shared->failed;
        if (!post_semaphore(resources->semaphores[SEM_STATE]))
        {
            return WAIT_FAILED;
        }
        if (failed)
        {
            fprintf(stderr, "Участник завершился до окончания моделирования\n");
            return WAIT_FAILED;
        }
        result = wait_once(resources->semaphores[index], true);
        if (result != WAIT_TIMEOUT)
        {
            return result;
        }
    }
}

// модель начинается только после подключения библиотекаря и всех читателей
static bool run_days(SessionResources *resources, int total_days)
{
    SOFT_ASSERT(resources != NULL && resources->model_ready, "Общее состояние не подготовлено", false);
    SOFT_ASSERT(resources->shared != NULL && resources->shared != MAP_FAILED,
        "Отображение shared memory не подготовлено", false);
    SOFT_ASSERT(
        resources->shared->simulation.reader_count > 0 && resources->shared->simulation.reader_count <= MAX_READERS,
        "Количество читателей вне массива", false);
    SOFT_ASSERT(total_days > 0, "Количество дней должно быть положительным", false);
    Simulation *simulation = &resources->shared->simulation;
    printf("Сессия готова. Ожидается участников: %d\n", simulation->reader_count + 1);
    for (int idx = 0; idx < simulation->reader_count + 1; ++idx)
    {
        WaitResult result = coordinator_wait(resources, SEM_READY);
        if (result != WAIT_READY)
        {
            return result == WAIT_STOPPED;
        }
    }
    if (wait_semaphore(resources->semaphores[SEM_STATE], true) != WAIT_READY)
    {
        return stop_requested != 0;
    }
    bool valid = registration_valid(resources->shared) && resources->shared->connected == simulation->reader_count + 1;
    if (!post_semaphore(resources->semaphores[SEM_STATE]) || !valid)
    {
        return false;
    }
    printf("Библиотека НАЧАЛА РАБОТАТЬ!. Количество дней работы: %d\n", total_days);
    logger_write("Начало работы библиотеки: %d дней, срок чтения %d\n", total_days, simulation->reading_days);
    for (int completed = 0; completed < total_days; ++completed)
    {
        WaitResult result = wait_semaphore(resources->semaphores[SEM_STATE], true);
        if (result != WAIT_READY)
        {
            return result == WAIT_STOPPED;
        }
        int day = completed + FIRST_SIMULATION_DAY;
        simulation->current_day = day;
        if (!post_semaphore(resources->semaphores[SEM_STATE]))
        {
            return false;
        }
        printf("\nДень %d: начало\n", day);
        if (!post_semaphore(resources->semaphores[SEM_LIBRARIAN]))
        {
            return false;
        }
        result = coordinator_wait(resources, SEM_DONE);
        if (result != WAIT_READY)
        {
            return result == WAIT_STOPPED;
        }
        for (int idx = 0; idx < simulation->reader_count; ++idx)
        {
            if (!post_semaphore(resources->semaphores[SEM_READERS + idx]))
            {
                return false;
            }
        }
        for (int idx = 0; idx < simulation->reader_count; ++idx)
        {
            result = coordinator_wait(resources, SEM_DONE);
            if (result != WAIT_READY)
            {
                return result == WAIT_STOPPED;
            }
        }
        printf("День %d: завершён\n", day);
    }
    return true;
}

// участник ждёт свою фазу и меняет общую модель под блокировкой state
static bool run_participant(SessionResources *resources, ProcessRole role, int reader_index)
{
    SOFT_ASSERT(resources != NULL && resources->registered, "Участник не зарегистрирован", false);
    SOFT_ASSERT(resources->shared != NULL && resources->shared != MAP_FAILED,
        "Зарегистрированный участник потерял shared memory", false);
    SOFT_ASSERT(role == PROCESS_LIBRARIAN || role == PROCESS_READER, "Некорректная роль участника", false);
    Simulation *simulation = &resources->shared->simulation;
    SOFT_ASSERT(simulation->reader_count > 0 && simulation->reader_count <= MAX_READERS,
        "Количество читателей вне массива", false);
    SOFT_ASSERT(role != PROCESS_READER || (reader_index >= 0 && reader_index < simulation->reader_count),
        "Внутренний индекс Reader вне массива", false);
    sem_t *start = resources->semaphores[role == PROCESS_LIBRARIAN ? SEM_LIBRARIAN : SEM_READERS + reader_index];
    while (true)
    {
        if (wait_semaphore(start, true) != WAIT_READY ||
            wait_semaphore(resources->semaphores[SEM_STATE], true) != WAIT_READY)
        {
            return false;
        }
        if (simulation->finished)
        {
            return post_semaphore(resources->semaphores[SEM_STATE]);
        }
        if (role == PROCESS_LIBRARIAN)
        {
            simulation_process_returns(simulation, simulation->current_day);
        }
        else
        {
            simulation_process_reader(simulation, reader_index, simulation->current_day);
        }
        if (!post_semaphore(resources->semaphores[SEM_STATE]) || !post_semaphore(resources->semaphores[SEM_DONE]))
        {
            return false;
        }
    }
}

// участники закрывают свои дескрипторы, имена удаляет только владелец
static bool close_resources(SessionResources *resources, bool owner, sem_t *exited)
{
    SOFT_ASSERT(resources != NULL, "Указатель на ресурсы = NULL", false);
    SOFT_ASSERT(owner || !resources->shm_created, "Участник ошибочно владеет shared memory", false);
    for (int idx = 0; idx < MAX_SEMAPHORES; ++idx)
    {
        SOFT_ASSERT(!resources->created[idx] || (owner && resources->semaphores[idx] != SEM_FAILED),
            "Владелец или дескриптор созданного семафора не согласован", false);
    }
    bool success = true;
    for (int idx = MAX_SEMAPHORES - 1; idx >= 0; --idx)
    {
        if (resources->semaphores[idx] != SEM_FAILED && resources->semaphores[idx] != exited)
        {
            if (sem_close(resources->semaphores[idx]) != 0)
            {
                perror("sem_close");
                success = false;
            }
            resources->semaphores[idx] = SEM_FAILED;
        }
        if (owner && resources->created[idx])
        {
            char name[IPC_NAME_SIZE];
            if (!semaphore_name(resources, idx, name) || sem_unlink(name) != 0)
            {
                perror("sem_unlink");
                success = false;
            }
            resources->created[idx] = false;
        }
    }
    if (resources->shared != MAP_FAILED)
    {
        if (munmap(resources->shared, sizeof *resources->shared) != 0)
        {
            perror("munmap");
            success = false;
        }
        resources->shared = MAP_FAILED;
    }
    if (owner && resources->shm_created && shm_unlink(resources->shm_name) != 0)
    {
        perror("shm_unlink");
        success = false;
    }
    return success;
}

// сначала отмечаем выход, затем освобождаем ресурсы и подтверждаем завершение
static bool finish_participant(SessionResources *resources, bool success)
{
    SOFT_ASSERT(resources != NULL, "Указатель на ресурсы = NULL", false);
    SOFT_ASSERT(!resources->registered || (resources->shared != NULL && resources->shared != MAP_FAILED &&
                                              resources->semaphores[SEM_STATE] != SEM_FAILED &&
                                              resources->semaphores[SEM_EXITED] != SEM_FAILED),
        "Ресурсы зарегистрированного участника не согласованы", false);
    sem_t *exited = SEM_FAILED;
    if (resources->registered)
    {
        if (wait_semaphore(resources->semaphores[SEM_STATE], false) != WAIT_READY)
        {
            success = false;
        }
        else
        {
            // при прямом сигнале участнику Coordinator прерывает ожидание фаз
            if (!success || !resources->shared->simulation.finished)
            {
                resources->shared->failed = true;
            }
            ++resources->shared->exited;
            bool valid = registration_valid(resources->shared);
            if (!post_semaphore(resources->semaphores[SEM_STATE]) || !valid)
            {
                success = false;
            }
        }
        exited = resources->semaphores[SEM_EXITED];
    }
    // подтверждаем выход после освобождения отображения и остальных дескрипторов
    if (!close_resources(resources, false, exited))
    {
        success = false;
    }
    if (exited != SEM_FAILED)
    {
        if (!post_semaphore(exited))
        {
            success = false;
        }
        if (sem_close(exited) != 0)
        {
            perror("sem_close (exited)");
            success = false;
        }
    }
    return success;
}

// пробуждаем подключённых участников и ждём exited вместо waitpid
static bool finish_coordinator(SessionResources *resources, Simulation *simulation)
{
    SOFT_ASSERT(resources != NULL && simulation != NULL, "Нет ресурсов или модели", false);
    SOFT_ASSERT(
        !resources->model_ready || (resources->shared != NULL && resources->shared != MAP_FAILED &&
                                       resources->shm_created && resources->shared->simulation.reader_count > 0 &&
                                       resources->shared->simulation.reader_count <= MAX_READERS),
        "Модель не согласована с shared memory при завершении", false);
    bool success = true;
    if (resources->model_ready && resources->semaphores[SEM_STATE] != SEM_FAILED)
    {
        if (wait_semaphore(resources->semaphores[SEM_STATE], false) != WAIT_READY)
        {
            success = false;
        }
        else
        {
            resources->shared->accepting = false;
            resources->shared->simulation.finished = true;
            int connected = resources->shared->connected;
            bool valid = registration_valid(resources->shared);
            if (!post_semaphore(resources->semaphores[SEM_STATE]) || !valid)
            {
                success = false;
            }
            // не держим state, пока участники завершают работу
            for (int idx = 0; idx < resources->shared->simulation.reader_count + 1; ++idx)
            {
                int start = idx == 0 ? SEM_LIBRARIAN : SEM_READERS + idx - 1;
                if (resources->semaphores[start] != SEM_FAILED && !post_semaphore(resources->semaphores[start]))
                {
                    success = false;
                }
            }
            for (int idx = 0; idx < connected; ++idx)
            {
                if (wait_semaphore(resources->semaphores[SEM_EXITED], false) != WAIT_READY)
                {
                    success = false;
                    break;
                }
            }
            if (resources->shared->failed)
            {
                success = false;
            }
        }
    }
    if (resources->model_ready)
    {
        *simulation = resources->shared->simulation;
    }
    if (!close_resources(resources, true, SEM_FAILED))
    {
        success = false;
    }
    return success;
}

// config задаёт модель, IPC отвечает только за согласование участников
static bool run_coordinator(SessionResources *resources, const SessionOptions *options)
{
    SOFT_ASSERT(resources != NULL && options != NULL, "Нет ресурсов или опций", false);
    SOFT_ASSERT(options->config_filename != NULL, "Указатель на имя config = NULL", false);
    Config config;
    if (!config_load(options->config_filename, &config))
    {
        return false;
    }
    if (options->has_total_days)
    {
        config.total_days = options->total_days;
    }
    if (options->has_reading_days)
    {
        config.reading_days = options->reading_days;
    }
    const Librarian librarian = {.id = 1, .name = "Никита Пичурин"};
    Simulation simulation;
    simulation_initialization(&simulation, config.books, config.book_count, config.readers, config.reader_count,
        config.reading_days, &librarian, config.issue_strategy, config.request_rule);
    if (!simulation_validate(&simulation, config.total_days))
    {
        return false;
    }
    bool success = prepare_coordinator(resources, &simulation);
    if (success)
    {
        // очищаем журнал один раз, дальнейшая запись всех участников идёт в конец
        success = logger_open("library.log") && logger_open_append("library.log");
    }
    if (success)
    {
        success = run_days(resources, config.total_days);
    }
    bool print_results = resources->model_ready;
    if (!finish_coordinator(resources, &simulation))
    {
        success = false;
    }
    if (print_results)
    {
        simulation_print_results(&simulation);
    }
    return success;
}

// роль задаётся исполняемым файлом, session связывает независимые программы
int process_7_8_main(int argc, char *argv[], ProcessRole role)
{
    SOFT_ASSERT(role == PROCESS_COORDINATOR || role == PROCESS_LIBRARIAN || role == PROCESS_READER,
        "Неизвестная роль программы", EXIT_FAILURE);
    SessionOptions options;
    if (!parse_options(argc, argv, role, &options))
    {
        return EXIT_FAILURE;
    }
    if (options.help)
    {
        printf("Использование: %s --session NAME", argv[0]);
        if (role == PROCESS_COORDINATOR)
        {
            printf(" [-c FILE] [-d N] [-r N]\nConfig по умолчанию: config.txt\n");
        }
        else
        {
            printf(role == PROCESS_READER ? " --reader INDEX\n" : "\n");
        }
        printf("Session: 1-%d символов A-Z, a-z, 0-9, _, -\n", SESSION_MAX);
        return EXIT_SUCCESS;
    }
    if (!setup_signal_handlers())
    {
        return EXIT_FAILURE;
    }
    if (setvbuf(stdout, NULL, _IONBF, 0) != 0)
    {
        fprintf(stderr, "Не удалось подготовить вывод процесса\n");
        return EXIT_FAILURE;
    }
    SessionResources resources = {.shared = MAP_FAILED};
    for (int idx = 0; idx < MAX_SEMAPHORES; ++idx)
    {
        resources.semaphores[idx] = SEM_FAILED;
    }
    int length = snprintf(resources.prefix, sizeof resources.prefix, "/library_7_8_%s", options.session);
    SOFT_ASSERT(length > 0 && (size_t)length < sizeof resources.prefix, "Имя сессии не помещается", EXIT_FAILURE);
    length = snprintf(resources.shm_name, sizeof resources.shm_name, "%s_shm", resources.prefix);
    SOFT_ASSERT(
        length > 0 && (size_t)length < sizeof resources.shm_name, "Имя shared memory не помещается", EXIT_FAILURE);
    bool success;
    if (role == PROCESS_COORDINATOR)
    {
        success = run_coordinator(&resources, &options);
    }
    else
    {
        success = logger_open_append("library.log") && connect_participant(&resources, role, options.reader_index);
        if (success)
        {
            success = run_participant(&resources, role, options.reader_index);
        }
        success = finish_participant(&resources, success);
    }
    logger_close();
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
