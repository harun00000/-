#define _POSIX_C_SOURCE 200809L
#include "process_4_5.h"
#include "check.h"
#include "main_utils.h"
#include "logger.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

enum
{
    SHM_NAME_SIZE = 96,
    LIBRARIAN_INDEX = -1
};

// общее состояние защищает семафор state_lock
// в общей памяти нет указателей на данные отдельных процессов
typedef struct ProcessShared
{
    Simulation simulation;
    sem_t state_lock;
} ProcessShared;

// pid и служебные данные остаются только у родителя
typedef struct ChildProcess
{
    pid_t pid;
    bool reaped;
} ChildProcess;

// состояние запуска хранится у родителя и учитывает частичную инициализацию
typedef struct ProcessResources
{
    char shm_name[SHM_NAME_SIZE];
    int fd;
    bool shm_created;
    ProcessShared *shared;
    sem_t *semaphores[MAX_READERS + 3];
    int initialized;
    ChildProcess children[MAX_READERS + 1];
    int created;
    bool signals_blocked;
    bool state_ready;
    bool lock_broken;
    sigset_t previous_mask;
} ProcessResources;

// отличаем штатную остановку по сигналу от ошибки ожидания
typedef enum WaitResult
{
    WAIT_READY,
    WAIT_STOPPED,
    WAIT_FAILED
} WaitResult;

// выдаём одно разрешение на продолжение работы
static bool process_post(sem_t *semaphore)
{
    SOFT_ASSERT(semaphore != NULL, "Указатель на семафор = NULL", false);
    if (sem_post(semaphore) != 0)
    {
        perror("sem_post");
        return false;
    }
    return true;
}

// дети повторяют ожидание после EINTR, остановку им сообщает родитель
static bool child_wait(sem_t *semaphore)
{
    SOFT_ASSERT(semaphore != NULL, "Указатель на семафор = NULL", false);
    // прерывание ожидания сигналом допускает повторную попытку
    while (sem_wait(semaphore) != 0)
    {
        if (errno != EINTR)
        {
            perror("sem_wait");
            return false;
        }
    }
    return true;
}

// замечаем завершение ребёнка, чтобы не зависнуть на его семафоре
static bool check_children(ChildProcess children[], int count, bool finishing)
{
    SOFT_ASSERT(children != NULL, "Указатель на дочерние процессы = NULL", false);
    SOFT_ASSERT(count >= 0 && count <= MAX_READERS + 1, "Количество процессов вне массива", false);
    bool success = true;
    for (int idx = 0; idx < count; ++idx)
    {
        SOFT_ASSERT(children[idx].pid > 0, "Некорректный pid дочернего процесса", false);
        // повторно не ждём процессы, уже отмеченные как собранные
        if (children[idx].reaped)
        {
            if (!finishing)
            {
                success = false;
            }
            continue;
        }
        // проверяем статус без ожидания, пока ребёнок ещё работает
        int status = 0;
        pid_t result;
        do
        {
            result = waitpid(children[idx].pid, &status, WNOHANG);
        } while (result == -1 && errno == EINTR);

        if (result == -1)
        {
            perror("waitpid");
            children[idx].reaped = true;
            success = false;
        }
        else if (result > 0)
        {
            children[idx].reaped = true;
            // до общей остановки даже успешный выход ребёнка считается ошибкой
            if (!finishing || !WIFEXITED(status) || WEXITSTATUS(status) != EXIT_SUCCESS)
            {
                fprintf(stderr, "Дочерний процесс %ld завершился неожиданно (status=%d)\n", (long)children[idx].pid,
                    status);
                success = false;
            }
        }
    }
    return success;
}

// родитель ждёт семафор и одновременно следит за сигналом и завершением детей
static WaitResult parent_wait(sem_t *semaphore, ChildProcess children[], int count, bool interruptible)
{
    SOFT_ASSERT(semaphore != NULL, "Указатель на семафор = NULL", WAIT_FAILED);
    SOFT_ASSERT(children != NULL, "Указатель на дочерние процессы = NULL", WAIT_FAILED);
    SOFT_ASSERT(count >= 0 && count <= MAX_READERS + 1, "Количество процессов вне массива", WAIT_FAILED);

    while (true)
    {
        // при очистке флаг остановки не должен мешать получить блокировку
        if (interruptible && stop_requested)
        {
            return WAIT_STOPPED;
        }
        if (!check_children(children, count, false))
        {
            return WAIT_FAILED;
        }

        // периодически проверяем завершение детей и сигнал остановки
        struct timespec deadline;
        if (clock_gettime(CLOCK_REALTIME, &deadline) != 0)
        {
            perror("clock_gettime");
            return WAIT_FAILED;
        }
        // задаём абсолютный срок ожидания через 100 миллисекунд
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
        // после сигнала или таймаута снова проверяем флаг остановки и детей
        if (errno != EINTR && errno != ETIMEDOUT)
        {
            perror("sem_timedwait");
            return WAIT_FAILED;
        }
    }
}

// индекс -1 задаёт роль библиотекаря, остальные индексы задают читателей
static int child_run(ProcessShared *shared, int reader_index)
{
    SOFT_ASSERT(shared != NULL && shared != MAP_FAILED, "Некорректный указатель на shared memory", EXIT_FAILURE);
    SOFT_ASSERT(shared->simulation.reader_count > 0 && shared->simulation.reader_count <= MAX_READERS,
        "Количество читателей вне массива", EXIT_FAILURE);
    SOFT_ASSERT(
        reader_index == LIBRARIAN_INDEX || (reader_index >= 0 && reader_index < shared->simulation.reader_count),
        "Индекс процесса читателя вне массива", EXIT_FAILURE);

    Simulation *simulation = &shared->simulation;
    // у библиотекаря и каждого читателя свой семафор запуска
    sem_t *start =
        reader_index == LIBRARIAN_INDEX ? &simulation->librarian_start : &simulation->reader_start[reader_index];
    while (true)
    {
        // сначала ждём свою фазу, затем получаем доступ к общему состоянию
        if (!child_wait(start) || !child_wait(&shared->state_lock))
        {
            return EXIT_FAILURE;
        }
        // пробуждение может означать завершение вместо нового рабочего дня
        if (simulation->finished)
        {
            return process_post(&shared->state_lock) ? EXIT_SUCCESS : EXIT_FAILURE;
        }

        // выполняем общую предметную логику под защитой семафора
        if (reader_index == LIBRARIAN_INDEX)
        {
            simulation_process_returns(simulation, simulation->current_day);
        }
        else
        {
            simulation_process_reader(simulation, reader_index, simulation->current_day);
        }

        // освобождаем блокировку перед уведомлением и новым ожиданием
        if (!process_post(&shared->state_lock) || !process_post(&simulation->phase_done))
        {
            return EXIT_FAILURE;
        }
    }
}

// аварийная остановка, если обычное пробуждение детей уже не помогает
static void kill_remaining_children(ChildProcess children[], int count)
{
    SOFT_ASSERT_VOID(children != NULL, "Указатель на дочерние процессы = NULL");
    SOFT_ASSERT_VOID(count >= 0 && count <= MAX_READERS + 1, "Количество процессов вне массива");
    for (int idx = 0; idx < count; ++idx)
    {
        SOFT_ASSERT_VOID(children[idx].pid > 0, "Некорректный pid дочернего процесса");
        if (!children[idx].reaped && kill(children[idx].pid, SIGKILL) != 0 && errno != ESRCH)
        {
            perror("kill");
        }
    }
}

// собираем всех детей через waitpid до освобождения общей памяти
static bool reap_children(ChildProcess children[], int count)
{
    SOFT_ASSERT(children != NULL, "Указатель на дочерние процессы = NULL", false);
    SOFT_ASSERT(count >= 0 && count <= MAX_READERS + 1, "Количество процессов вне массива", false);
    bool success = true;
    while (true)
    {
        if (!check_children(children, count, true))
        {
            success = false;
            // погибший ребёнок мог удерживать блокировку
            kill_remaining_children(children, count);
        }
        bool all_reaped = true;
        for (int idx = 0; idx < count; ++idx)
        {
            if (!children[idx].reaped)
            {
                all_reaped = false;
            }
        }
        if (all_reaped)
        {
            return success;
        }
        // небольшая пауза не даёт циклу ожидания занимать всё процессорное время
        struct timespec pause = {.tv_sec = 0, .tv_nsec = 10000000L};
        if (nanosleep(&pause, NULL) != 0 && errno != EINTR)
        {
            perror("nanosleep");
            success = false;
            kill_remaining_children(children, count);
        }
    }
}

// готовим общую модель до создания детей, при ошибке сохраняем учёт уже созданного
static bool prepare_shared(ProcessResources *resources, const Simulation *simulation)
{
    SOFT_ASSERT(resources != NULL, "Указатель на ресурсы процессов = NULL", false);
    SOFT_ASSERT(simulation != NULL && simulation->book_count > 0 && simulation->book_count <= MAX_BOOKS &&
                    simulation->request_count >= 0 && simulation->request_count <= MAX_REQUESTS,
        "Размеры массивов модели вне допустимых границ", false);
    SOFT_ASSERT(simulation->reader_count > 0 && simulation->reader_count <= MAX_READERS,
        "Количество читателей вне массива", false);
    SOFT_ASSERT(resources->fd == -1 && resources->shared == MAP_FAILED && !resources->shm_created &&
                    !resources->state_ready && resources->initialized == 0 && resources->created == 0,
        "Повторная подготовка ресурсов", false);
    SOFT_ASSERT(memchr(resources->shm_name, '\0', sizeof resources->shm_name) != NULL &&
                    resources->shm_name[0] == '/' && resources->shm_name[1] != '\0' &&
                    strchr(resources->shm_name + 1, '/') == NULL,
        "Некорректное внутреннее имя shared memory", false);

    // создаём новый объект, не открывая случайно память другого запуска
    resources->fd = shm_open(resources->shm_name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (resources->fd == -1)
    {
        perror("shm_open");
        return false;
    }
    resources->shm_created = true;
    // новый объект пустой, сначала задаём размер общего состояния
    if (ftruncate(resources->fd, (off_t)sizeof *resources->shared) != 0)
    {
        perror("ftruncate");
        return false;
    }
    // общее отображение унаследуют дети при fork
    resources->shared = mmap(NULL, sizeof *resources->shared, PROT_READ | PROT_WRITE, MAP_SHARED, resources->fd, 0);
    if (resources->shared == MAP_FAILED)
    {
        perror("mmap");
        return false;
    }
    // после mmap дескриптор можно закрыть, отображение останется доступным
    int close_result = close(resources->fd);
    resources->fd = -1;
    if (close_result != 0)
    {
        perror("close (shared memory)");
        return false;
    }

    // переносим исходные данные до запуска детей и создания семафоров
    *resources->shared = (ProcessShared){0};
    resources->shared->simulation = *simulation;
    resources->shared->simulation.finished = false;
    resources->state_ready = true;
    resources->semaphores[0] = &resources->shared->state_lock;
    resources->semaphores[1] = &resources->shared->simulation.librarian_start;
    resources->semaphores[2] = &resources->shared->simulation.phase_done;
    for (int idx = 0; idx < simulation->reader_count; ++idx)
    {
        resources->semaphores[idx + 3] = &resources->shared->simulation.reader_start[idx];
    }
    // pshared = 1 разрешает работу между процессами, только блокировка начинается с 1
    // счётчик увеличивается после успеха, чтобы очистка знала число созданных семафоров
    for (; resources->initialized < simulation->reader_count + 3; ++resources->initialized)
    {
        if (sem_init(resources->semaphores[resources->initialized], 1, resources->initialized == 0 ? 1 : 0) != 0)
        {
            perror("sem_init");
            return false;
        }
    }

    return true;
}

// дети наследуют готовые IPC-ресурсы и выполняют только свою роль
static bool start_children(ProcessResources *resources)
{
    SOFT_ASSERT(resources != NULL, "Указатель на ресурсы процессов = NULL", false);
    SOFT_ASSERT(resources->initialized >= 0 && resources->initialized <= MAX_READERS + 3,
        "Количество подготовленных семафоров вне массива", false);
    for (int idx = 0; idx < resources->initialized; ++idx)
    {
        SOFT_ASSERT(resources->semaphores[idx] != NULL, "Не задан указатель на подготовленный семафор", false);
    }
    SOFT_ASSERT(resources->shared != NULL && resources->shared != MAP_FAILED && resources->state_ready,
        "Общее состояние не подготовлено", false);
    SOFT_ASSERT(
        resources->shared->simulation.reader_count > 0 && resources->shared->simulation.reader_count <= MAX_READERS,
        "Количество читателей вне массива", false);
    SOFT_ASSERT(resources->initialized == resources->shared->simulation.reader_count + 3,
        "Не все семафоры подготовлены", false);
    SOFT_ASSERT(resources->created == 0 && !resources->signals_blocked, "Повторный запуск дочерних процессов", false);
    sigset_t stop_signals;

    // сигналы остановки получает родитель, дети ждут finished и пробуждения
    sigemptyset(&stop_signals);
    sigaddset(&stop_signals, SIGINT);
    sigaddset(&stop_signals, SIGTERM);
    if (sigprocmask(SIG_BLOCK, &stop_signals, &resources->previous_mask) != 0)
    {
        perror("sigprocmask");
        return false;
    }
    resources->signals_blocked = true;
    // первым создаём библиотекаря, затем по одному процессу на читателя
    for (int idx = 0; idx < resources->shared->simulation.reader_count + 1; ++idx)
    {
        SOFT_ASSERT(resources->created == idx && resources->created < MAX_READERS + 1,
            "Нарушен счётчик создаваемых детей", false);
        pid_t pid = fork();
        if (pid == -1)
        {
            perror("fork");
            return false;
        }
        if (pid == 0)
        {
            // ребёнок выполняет свою роль и не продолжает родительский код
            int result = child_run(resources->shared, idx - 1);
            logger_close();
            _exit(result);
        }
        resources->children[resources->created++].pid = pid;
    }
    // возвращаем родителю приём сигналов, дети сохраняют унаследованную маску
    if (sigprocmask(SIG_SETMASK, &resources->previous_mask, NULL) != 0)
    {
        perror("sigprocmask");
        return false;
    }
    resources->signals_blocked = false;
    return true;
}

// в каждом дне полностью заканчиваем возвраты перед запуском читателей
static bool run_process_days(ProcessResources *resources, int total_days)
{
    SOFT_ASSERT(resources != NULL, "Указатель на ресурсы процессов = NULL", false);
    SOFT_ASSERT(total_days >= 0, "Количество дней симуляции не должно быть отрицательным", false);
    SOFT_ASSERT(resources->shared != NULL && resources->shared != MAP_FAILED && resources->state_ready,
        "Общее состояние не подготовлено", false);
    SOFT_ASSERT(
        resources->shared->simulation.reader_count > 0 && resources->shared->simulation.reader_count <= MAX_READERS,
        "Количество читателей вне массива", false);
    SOFT_ASSERT(resources->initialized == resources->shared->simulation.reader_count + 3,
        "Не все семафоры подготовлены", false);
    SOFT_ASSERT(resources->created == resources->shared->simulation.reader_count + 1 && !resources->signals_blocked,
        "Модель запущена до подготовки всех детей", false);
    Simulation *simulation = &resources->shared->simulation;

    printf("Библиотека НАЧАЛА РАБОТАТЬ!. Количество дней работы: %d\n", total_days);
    logger_write("Начало работы библиотеки: %d дней, срок чтения %d\n", total_days, simulation->reading_days);
    for (int completed = 0; completed < total_days; ++completed)
    {
        WaitResult wait = parent_wait(&resources->shared->state_lock, resources->children, resources->created, true);
        if (wait != WAIT_READY)
        {
            return wait == WAIT_STOPPED;
        }
        // только родитель меняет модельный день и делает это под блокировкой
        int day = completed + FIRST_SIMULATION_DAY;
        resources->shared->simulation.current_day = day;
        if (!process_post(&resources->shared->state_lock))
        {
            resources->lock_broken = true;
            return false;
        }
        printf("\nДень %d: начало\n", day);
        // сначала запускаем возвраты и ждём завершения работы библиотекаря
        if (!process_post(&resources->shared->simulation.librarian_start))
        {
            return false;
        }
        wait = parent_wait(&resources->shared->simulation.phase_done, resources->children, resources->created, true);
        if (wait != WAIT_READY)
        {
            return wait == WAIT_STOPPED;
        }
        // после возвратов разрешаем посещения всем читателям
        for (int idx = 0; idx < simulation->reader_count; ++idx)
        {
            if (!process_post(&resources->shared->simulation.reader_start[idx]))
            {
                return false;
            }
        }
        // каждый читатель подтверждает свою фазу, даже если сегодня не приходит
        for (int idx = 0; idx < simulation->reader_count; ++idx)
        {
            wait =
                parent_wait(&resources->shared->simulation.phase_done, resources->children, resources->created, true);
            if (wait != WAIT_READY)
            {
                return wait == WAIT_STOPPED;
            }
        }
        printf("День %d: завершён\n", day);
    }
    return true;
}

// сначала останавливаем и собираем детей, затем освобождаем их общие ресурсы
static bool cleanup_processes(ProcessResources *resources, Simulation *simulation)
{
    SOFT_ASSERT(resources != NULL, "Указатель на ресурсы процессов = NULL", false);
    SOFT_ASSERT(simulation != NULL, "Указатель на симуляцию = NULL", false);
    SOFT_ASSERT(resources->created >= 0 && resources->created <= MAX_READERS + 1,
        "Количество созданных детей вне массива", false);
    SOFT_ASSERT(resources->initialized >= 0 && resources->initialized <= MAX_READERS + 3,
        "Количество семафоров вне массива", false);
    SOFT_ASSERT(!resources->state_ready ||
                    (resources->shared != NULL && resources->shared != MAP_FAILED && resources->shm_created),
        "Состояние модели не согласовано с shared memory", false);
    SOFT_ASSERT(resources->state_ready || (resources->initialized == 0 && resources->created == 0),
        "Ресурсы работников созданы до состояния модели", false);
    if (resources->state_ready)
    {
        SOFT_ASSERT(
            resources->shared->simulation.reader_count > 0 && resources->shared->simulation.reader_count <= MAX_READERS,
            "Количество читателей вне массива", false);
        SOFT_ASSERT(resources->created <= resources->shared->simulation.reader_count + 1 &&
                        resources->initialized <= resources->shared->simulation.reader_count + 3,
            "Счётчики ресурсов не согласованы с числом читателей", false);
        SOFT_ASSERT(resources->created == 0 || resources->initialized == resources->shared->simulation.reader_count + 3,
            "Дети запущены до создания всех семафоров", false);
    }
    if (resources->shm_created)
    {
        SOFT_ASSERT(memchr(resources->shm_name, '\0', sizeof resources->shm_name) != NULL &&
                        resources->shm_name[0] == '/' && resources->shm_name[1] != '\0' &&
                        strchr(resources->shm_name + 1, '/') == NULL,
            "Некорректное имя shared memory для удаления", false);
    }
    for (int idx = 0; idx < resources->initialized; ++idx)
    {
        SOFT_ASSERT(resources->semaphores[idx] != NULL, "Некорректный указатель на созданный семафор", false);
    }
    bool success = true;

    // восстанавливаем маску и при ошибке, случившейся во время запуска детей
    if (resources->signals_blocked)
    {
        if (sigprocmask(SIG_SETMASK, &resources->previous_mask, NULL) != 0)
        {
            perror("sigprocmask");
            success = false;
        }
        resources->signals_blocked = false;
    }
    if (resources->created > 0)
    {
        bool graceful = !resources->lock_broken && parent_wait(&resources->shared->state_lock, resources->children,
                                                       resources->created, false) == WAIT_READY;
        if (graceful)
        {
            // сообщаем об остановке под блокировкой и будим всех созданных детей
            resources->shared->simulation.finished = true;
            graceful = process_post(&resources->shared->state_lock);
            for (int idx = 0; idx < resources->created; ++idx)
            {
                sem_t *start = idx == 0 ? &resources->shared->simulation.librarian_start
                                        : &resources->shared->simulation.reader_start[idx - 1];
                if (!process_post(start))
                {
                    graceful = false;
                }
            }
        }
        if (!graceful)
        {
            success = false;
            kill_remaining_children(resources->children, resources->created);
        }
        if (!reap_children(resources->children, resources->created))
        {
            success = false;
        }
    }

    // дети уже собраны, уничтожаем только созданные семафоры
    for (int idx = resources->initialized - 1; idx >= 0; --idx)
    {
        if (sem_destroy(resources->semaphores[idx]) != 0)
        {
            perror("sem_destroy");
            success = false;
        }
    }
    if (resources->state_ready)
    {
        // сохраняем итог перед удалением памяти, копии семафоров больше не используем
        *simulation = resources->shared->simulation;
    }
    // сначала убираем отображение из памяти родителя
    if (resources->shared != MAP_FAILED && munmap(resources->shared, sizeof *resources->shared) != 0)
    {
        perror("munmap");
        success = false;
    }
    // при ранней ошибке дескриптор мог остаться открытым
    if (resources->fd != -1 && close(resources->fd) != 0)
    {
        perror("close (shared memory)");
        success = false;
    }
    // удаляем имя только того объекта, который создали сами
    if (resources->shm_created && shm_unlink(resources->shm_name) != 0)
    {
        perror("shm_unlink");
        success = false;
    }
    return success;
}

bool process_4_5_run(Simulation *simulation, int total_days)
{
    if (!simulation_validate(simulation, total_days))
    {
        return false;
    }

    ProcessResources resources = {.fd = -1, .shared = MAP_FAILED};
    // uid и pid различают объекты памяти разных запусков
    int length =
        snprintf(resources.shm_name, sizeof resources.shm_name, "/library_4_5_%ld_%ld", (long)getuid(), (long)getpid());
    SOFT_ASSERT(
        length > 0 && (size_t)length < sizeof resources.shm_name, "Имя shared memory не помещается в буфер", false);

    // очищаем буфер перед fork и отключаем буферизацию для вывода детей
    if (fflush(stdout) != 0 || setvbuf(stdout, NULL, _IONBF, 0) != 0)
    {
        fprintf(stderr, "Не удалось подготовить вывод процессов\n");
        return false;
    }

    // следующий этап выполняется только после успешного завершения предыдущего
    bool success = prepare_shared(&resources, simulation) && start_children(&resources) &&
                   run_process_days(&resources, total_days);
    // очистка нужна и после успеха, и после частичного запуска
    if (!cleanup_processes(&resources, simulation))
    {
        success = false;
    }
    if (success)
    {
        simulation_print_results(simulation);
    }
    return success;
}
