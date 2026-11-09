#define _POSIX_C_SOURCE 200809L
#include "process_10.h"
#include "check.h"
#include "config.h"
#include "logger.h"
#include "main_utils.h"
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Options10
{
    const char *session;
    const char *config_filename;
    int total_days;
    int reading_days;
    int reader_index;
    int delay_ms;
    bool has_total_days;
    bool has_reading_days;
    bool has_reader;
    bool print_ipc;
    bool help;
} Options10;

typedef enum Phase10
{
    PHASE_10_IDLE,
    PHASE_10_LIBRARIAN,
    PHASE_10_READERS
} Phase10;

// единственный изменяемый экземпляр модели находится в Coordinator
typedef struct Coordinator10
{
    Ipc10Resources *ipc;
    Simulation simulation;
    bool connected[IPC_10_PARTICIPANTS];
    bool exited[IPC_10_PARTICIPANTS];
    bool done[IPC_10_PARTICIPANTS];
    bool begun[IPC_10_PARTICIPANTS];
    bool reserved[IPC_10_PARTICIPANTS];
    bool wait_for_books[IPC_10_PARTICIPANTS];
    int pending_book[IPC_10_PARTICIPANTS];
    int connected_count;
    int exited_count;
    Phase10 phase;
    bool returns_processed;
    int active_reader;
    int waiting[MAX_READERS];
    int waiting_count;
    bool shutting_down;
    bool failed;
} Coordinator10;

// границы CLI проверяются до сохранения числа во внутренние параметры
static bool parse_number(const char *text, int minimum, int *value)
{
    SOFT_ASSERT(text != NULL && value != NULL, "Нет строки или результата разбора", false);
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

// Reader выбирает индекс, только Coordinator меняет параметры моделирования
static bool parse_options(int argc, char *argv[], Process10Role role, Options10 *options)
{
    SOFT_ASSERT(argc > 0 && argv != NULL && options != NULL, "Нет аргументов CLI helper", false);
    SOFT_ASSERT(role == PROCESS_10_COORDINATOR || role == PROCESS_10_LIBRARIAN || role == PROCESS_10_READER,
        "Неизвестная роль в CLI helper", false);
    SOFT_ASSERT(argv[argc] == NULL, "Массив аргументов не завершён NULL", false);
    for (int idx = 0; idx < argc; ++idx)
    {
        SOFT_ASSERT(argv[idx] != NULL, "Указатель на аргумент = NULL", false);
    }
    *options = (Options10){.config_filename = "config.txt"};
    static const struct option long_options[] = {{"session", required_argument, NULL, 's'},
        {"reader", required_argument, NULL, 'i'}, {"config", required_argument, NULL, 'c'},
        {"days", required_argument, NULL, 'd'}, {"reading-days", required_argument, NULL, 'r'},
        {"delay-ms", required_argument, NULL, 't'}, {"print-ipc", no_argument, NULL, 'p'},
        {"help", no_argument, NULL, 'h'}, {NULL, 0, NULL, 0}};
    optind = 1;
    opterr = 0;
    int option;
    while ((option = getopt_long(argc, argv, ":c:d:r:h", long_options, NULL)) != -1)
    {
        switch (option)
        {
            case 's':
                if (options->session != NULL || !ipc_10_valid_session(optarg))
                {
                    fprintf(stderr,
                        "Ошибка: --session задаётся один раз, допустимы 1-%d символов A-Z, a-z, 0-9, _, -\n",
                        IPC_10_SESSION_MAX);
                    return false;
                }
                options->session = optarg;
                break;
            case 'i':
                if (role != PROCESS_10_READER || options->has_reader ||
                    !parse_number(optarg, 0, &options->reader_index))
                {
                    fprintf(stderr, "Ошибка: --reader требует один неотрицательный индекс и доступен только Reader\n");
                    return false;
                }
                options->has_reader = true;
                break;
            case 'c':
                if (role != PROCESS_10_COORDINATOR || optarg[0] == '\0')
                {
                    fprintf(stderr, "Ошибка: -c доступен только Coordinator и требует имя config\n");
                    return false;
                }
                options->config_filename = optarg;
                break;
            case 'd':
            case 'r':
            case 't':
            {
                int *value = option == 'd'   ? &options->total_days
                             : option == 'r' ? &options->reading_days
                                             : &options->delay_ms;
                if (role != PROCESS_10_COORDINATOR || !parse_number(optarg, option == 't' ? 0 : 1, value))
                {
                    fprintf(stderr, "Ошибка: -d/-r требуют положительное число, --delay-ms требует число от 0 до %d\n",
                        INT_MAX);
                    return false;
                }
                options->has_total_days = options->has_total_days || option == 'd';
                options->has_reading_days = options->has_reading_days || option == 'r';
                break;
            }
            case 'p':
                if (role != PROCESS_10_COORDINATOR)
                {
                    fprintf(stderr, "Ошибка: --print-ipc доступен только Coordinator\n");
                    return false;
                }
                options->print_ipc = true;
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
    if (!options->help && (options->session == NULL || (role == PROCESS_10_READER && !options->has_reader)))
    {
        fprintf(stderr, "Ошибка: укажите --session NAME, а для Reader также --reader INDEX\n");
        return false;
    }
    return true;
}

// флаги участников должны совпадать с суммарными счётчиками
static bool coordinator_counts_valid(const Coordinator10 *coordinator)
{
    SOFT_ASSERT(coordinator != NULL && coordinator->ipc != NULL, "Нет Coordinator или IPC", false);
    SOFT_ASSERT(coordinator->simulation.reader_count > 0 && coordinator->simulation.reader_count <= MAX_READERS,
        "Количество читателей вне массива", false);
    int connected = 0;
    int exited = 0;
    for (int slot = 0; slot <= coordinator->simulation.reader_count; ++slot)
    {
        SOFT_ASSERT(!coordinator->exited[slot] || coordinator->connected[slot],
            "Завершившийся участник не зарегистрирован", false);
        connected += coordinator->connected[slot];
        exited += coordinator->exited[slot];
    }
    SOFT_ASSERT(coordinator->connected_count == connected && coordinator->exited_count == exited,
        "Счётчики подключений и завершений не согласованы с флагами", false);
    return true;
}

// ошибка входящего протокола останавливает сессию обычным кодом
static bool protocol_error(Coordinator10 *coordinator, const char *reason)
{
    SOFT_ASSERT(coordinator != NULL && reason != NULL, "Нет описания ошибки протокола", false);
    fprintf(stderr, "Ошибка протокола: %s\n", reason);
    coordinator->failed = true;
    return false;
}

// ответ попадает в очередь только того участника, которому принадлежит slot
static bool send_reply(Coordinator10 *coordinator, int slot, Ipc10Type type, int result, bool interruptible)
{
    SOFT_ASSERT(coordinator != NULL && coordinator->ipc != NULL, "Нет Coordinator или IPC", false);
    SOFT_ASSERT(coordinator->simulation.reader_count > 0 && coordinator->simulation.reader_count <= MAX_READERS,
        "Количество читателей вне массива", false);
    SOFT_ASSERT(type == IPC_10_START_DAY || type == IPC_10_RESPONSE || type == IPC_10_SHUTDOWN,
        "Тип ответа не соответствует очереди участника", false);
    SOFT_ASSERT(
        slot >= 0 && slot <= coordinator->simulation.reader_count, "Внутренний индекс участника вне массива", false);
    Ipc10Message message = ipc_10_message(type, slot - 1, coordinator->simulation.current_day);
    message.result = result;
    Ipc10Result sent = ipc_10_send(coordinator->ipc->queues[IPC_10_LIBRARIAN_QUEUE + slot], &message, interruptible);
    if (sent == IPC_10_FAILED)
    {
        coordinator->failed = true;
    }
    return sent == IPC_10_OK;
}

// отказ и подтверждение идут через отдельную очередь регистрации
static bool register_participant(Coordinator10 *coordinator, const Ipc10Message *message)
{
    SOFT_ASSERT(coordinator != NULL && message != NULL, "Нет Coordinator или сообщения", false);
    if (!coordinator_counts_valid(coordinator))
    {
        return false;
    }
    SOFT_ASSERT(message->type == IPC_10_REGISTER_PARTICIPANT, "В helper регистрации передана другая команда", false);
    const char *error = NULL;
    int index = message->reader_index;
    if (coordinator->shutting_down)
    {
        error = "сессия завершает работу";
    }
    else if (index < -1 || index >= coordinator->simulation.reader_count)
    {
        error = "индекс Reader вне диапазона текущего config";
    }
    else if (coordinator->connected[index + 1])
    {
        error = index == -1 ? "Librarian уже подключён" : "Reader уже подключён";
    }
    Ipc10Message response = ipc_10_message(error ? IPC_10_ERROR : IPC_10_REGISTERED, index, 0);
    if (error != NULL)
    {
        snprintf(response.text, sizeof response.text, "%s", error);
    }
    else
    {
        int slot = index + 1;
        coordinator->connected[slot] = true;
        ++coordinator->connected_count;
        if (!coordinator_counts_valid(coordinator))
        {
            return false;
        }
        SOFT_ASSERT(coordinator->connected_count > 0 &&
                        coordinator->connected_count <= coordinator->simulation.reader_count + 1,
            "Счётчик подключений вне допустимых границ", false);
        if (index >= 0)
        {
            const Reader *reader = &coordinator->simulation.readers[index];
            memcpy(response.plan.name, reader->name, sizeof response.plan.name);
            response.plan.wanted_count = reader->wanted_count;
            response.plan.visit_count = reader->visit_count;
            response.plan.request_rule = coordinator->simulation.request_rule;
            for (int idx = 0; idx < reader->wanted_count; ++idx)
            {
                response.plan.wanted_books[idx] = reader->wanted_books[idx];
            }
            memcpy(response.plan.visit_days, reader->visit_days, sizeof response.plan.visit_days);
        }
    }
    if (ipc_10_send(coordinator->ipc->queues[IPC_10_REGISTRATION], &response, false) != IPC_10_OK)
    {
        coordinator->failed = true;
        return false;
    }
    if (error == NULL)
    {
        Ipc10Result result = ipc_10_wait(coordinator->ipc->semaphores[IPC_10_READY], true, IPC_10_TIMEOUT_MS);
        if (result != IPC_10_OK && result != IPC_10_STOPPED)
        {
            return protocol_error(coordinator, "участник не подтвердил подключение");
        }
        return result == IPC_10_OK;
    }
    return true;
}

// посещение занимает единственный активный слот до подтверждения PHASE_DONE
static bool begin_visit(Coordinator10 *coordinator, int reader_index)
{
    SOFT_ASSERT(coordinator != NULL && reader_index >= 0 && reader_index < coordinator->simulation.reader_count,
        "Внутренний индекс Reader вне массива", false);
    SOFT_ASSERT(coordinator->simulation.reader_count > 0 && coordinator->simulation.reader_count <= MAX_READERS &&
                    coordinator->phase == PHASE_10_READERS,
        "Посещение вне фазы читателей", false);
    SOFT_ASSERT(coordinator->connected[reader_index + 1] && !coordinator->exited[reader_index + 1] &&
                    coordinator->begun[reader_index + 1] && !coordinator->done[reader_index + 1],
        "Участник не готов к началу посещения", false);
    SOFT_ASSERT(coordinator->active_reader == -1, "Два посещения выполняются одновременно", false);
    coordinator->active_reader = reader_index;
    simulation_reader_begin(&coordinator->simulation, reader_index, coordinator->simulation.current_day);
    return send_reply(coordinator, reader_index + 1, IPC_10_RESPONSE, 0, true);
}

// ожидающие посещения продолжаются в порядке получения BEGIN_VISIT
static bool next_visit(Coordinator10 *coordinator)
{
    SOFT_ASSERT(coordinator != NULL && coordinator->waiting_count >= 0 && coordinator->waiting_count <= MAX_READERS,
        "Очередь посещений вне массива", false);
    SOFT_ASSERT(coordinator->simulation.reader_count > 0 && coordinator->simulation.reader_count <= MAX_READERS &&
                    coordinator->waiting_count <= coordinator->simulation.reader_count &&
                    coordinator->active_reader == -1,
        "Очередь посещений не согласована с моделью", false);
    if (coordinator->waiting_count == 0)
    {
        return true;
    }
    int index = coordinator->waiting[0];
    SOFT_ASSERT(
        index >= 0 && index < coordinator->simulation.reader_count, "Индекс ожидающего Reader вне массива", false);
    --coordinator->waiting_count;
    memmove(coordinator->waiting, coordinator->waiting + 1,
        (size_t)coordinator->waiting_count * sizeof coordinator->waiting[0]);
    return begin_visit(coordinator, index);
}

// применяем одну команду к центральному состоянию, не отдавая Reader копию модели
static bool reader_operation(Coordinator10 *coordinator, const Ipc10Message *message, int slot)
{
    SOFT_ASSERT(coordinator != NULL && message != NULL && slot > 0 && slot <= coordinator->simulation.reader_count,
        "Некорректные параметры операции Reader", false);
    SOFT_ASSERT(coordinator->ipc != NULL && coordinator->simulation.reader_count > 0 &&
                    coordinator->simulation.reader_count <= MAX_READERS && coordinator->phase == PHASE_10_READERS,
        "Некорректная внутренняя фаза Reader", false);
    SOFT_ASSERT(coordinator->active_reader >= -1 && coordinator->active_reader < coordinator->simulation.reader_count &&
                    coordinator->waiting_count >= 0 &&
                    coordinator->waiting_count <= coordinator->simulation.reader_count,
        "Состояние очереди посещений вне допустимых границ", false);
    int reader_index = slot - 1;
    Simulation *simulation = &coordinator->simulation;
    if (message->type == IPC_10_BEGIN_VISIT)
    {
        if (coordinator->begun[slot] || !is_reader_visit_on_said_day(&simulation->readers[reader_index], message->day))
        {
            return protocol_error(coordinator, "повторное посещение или день вне расписания");
        }
        coordinator->begun[slot] = true;
        if (coordinator->active_reader == -1)
        {
            return begin_visit(coordinator, reader_index);
        }
        SOFT_ASSERT(coordinator->waiting_count < MAX_READERS, "Очередь посещений переполнена", false);
        coordinator->waiting[coordinator->waiting_count++] = reader_index;
        return true;
    }
    if (coordinator->active_reader != reader_index)
    {
        return protocol_error(coordinator, "операция вне своего посещения");
    }
    int result = 0;
    if (message->type == IPC_10_TRY_RESERVED)
    {
        if (coordinator->reserved[slot])
        {
            return protocol_error(coordinator, "повторная обработка резервов");
        }
        simulation_reader_take_reservations(simulation, reader_index, message->day);
        coordinator->reserved[slot] = true;
        const Reader *reader = &simulation->readers[reader_index];
        for (int idx = 0; idx < reader->wanted_count; ++idx)
        {
            coordinator->wait_for_books[slot] =
                coordinator->wait_for_books[slot] ||
                simulation_reader_book_status(simulation, reader_index, reader->wanted_books[idx]) == BOOK_UNAVAILABLE;
        }
        coordinator->wait_for_books[slot] =
            coordinator->wait_for_books[slot] && simulation->request_rule == REQUEST_ALWAYS_WAIT;
    }
    else
    {
        if (!coordinator->reserved[slot] || message->book_id <= 0 ||
            find_book_by_id(simulation->books, simulation->book_count, message->book_id) == BOOK_INDEX_NOT_FOUND)
        {
            return protocol_error(coordinator, "неизвестная книга или резервы ещё не обработаны");
        }
        if (coordinator->pending_book[slot] != 0 && message->type != IPC_10_CREATE_REQUEST)
        {
            return protocol_error(coordinator, "не завершена предыдущая заявка");
        }
        if (message->type == IPC_10_BOOK_STATUS)
        {
            result = simulation_reader_book_status(simulation, reader_index, message->book_id);
        }
        else if (message->type == IPC_10_TRY_BOOK)
        {
            if (simulation_reader_book_status(simulation, reader_index, message->book_id) == BOOK_NOT_WANTED ||
                (message->result != 0 && message->result != 1) || message->result != coordinator->wait_for_books[slot])
            {
                return protocol_error(coordinator, "некорректное решение о выдаче");
            }
            result = simulation_reader_try_book(
                simulation, reader_index, message->book_id, message->day, message->result != 0);
            if (result == BOOK_REQUEST_NEEDED)
            {
                coordinator->pending_book[slot] = message->book_id;
            }
        }
        else if (message->type == IPC_10_CREATE_REQUEST)
        {
            if (coordinator->pending_book[slot] != message->book_id)
            {
                return protocol_error(coordinator, "заявка без предшествующей попытки выдачи");
            }
            simulation_reader_request_book(simulation, reader_index, message->book_id, message->day);
            coordinator->pending_book[slot] = 0;
        }
        else
        {
            return protocol_error(coordinator, "неизвестная операция Reader");
        }
    }
    return send_reply(coordinator, slot, IPC_10_RESPONSE, result, true);
}

// перед завершением проверяем, что обязательные действия фазы выполнены
static bool phase_done(Coordinator10 *coordinator, int slot)
{
    SOFT_ASSERT(coordinator != NULL && slot >= 0 && slot <= coordinator->simulation.reader_count,
        "Индекс завершённой фазы вне массива", false);
    SOFT_ASSERT(coordinator->simulation.reader_count > 0 && coordinator->simulation.reader_count <= MAX_READERS &&
                    coordinator->active_reader >= -1 &&
                    coordinator->active_reader < coordinator->simulation.reader_count,
        "Состояние фазы не согласовано с участниками", false);
    if (coordinator->done[slot])
    {
        return protocol_error(coordinator, "повторное завершение фазы");
    }
    if (slot == 0)
    {
        if (!coordinator->returns_processed)
        {
            return protocol_error(coordinator, "библиотекарь не запросил возвраты");
        }
    }
    else
    {
        bool visiting = is_reader_visit_on_said_day(
            &coordinator->simulation.readers[slot - 1], coordinator->simulation.current_day);
        if (visiting && (coordinator->active_reader != slot - 1 || !coordinator->reserved[slot] ||
                            coordinator->pending_book[slot] != 0))
        {
            return protocol_error(coordinator, "посещение не завершено");
        }
        if (visiting)
        {
            coordinator->active_reader = -1;
        }
    }
    coordinator->done[slot] = true;
    return slot == 0 || coordinator->active_reader != -1 || next_visit(coordinator);
}

// поля входящих сообщений проверяем обычными условиями до доступа к массивам
static bool handle_message(Coordinator10 *coordinator, const Ipc10Message *message)
{
    SOFT_ASSERT(coordinator != NULL && message != NULL, "Нет Coordinator или сообщения", false);
    SOFT_ASSERT(coordinator->ipc != NULL && coordinator->simulation.reader_count > 0 &&
                    coordinator->simulation.reader_count <= MAX_READERS,
        "Не подготовлены IPC или участники Coordinator", false);
    SOFT_ASSERT(coordinator->phase >= PHASE_10_IDLE && coordinator->phase <= PHASE_10_READERS,
        "Некорректная внутренняя фаза Coordinator", false);
    if (message->type == IPC_10_REGISTER_PARTICIPANT)
    {
        return register_participant(coordinator, message);
    }
    int index = message->reader_index;
    if (index < -1 || index >= coordinator->simulation.reader_count || !coordinator->connected[index + 1] ||
        coordinator->exited[index + 1])
    {
        return protocol_error(coordinator, "неизвестный или уже завершившийся участник");
    }
    int slot = index + 1;
    if (message->type == IPC_10_EXITED)
    {
        coordinator->exited[slot] = true;
        ++coordinator->exited_count;
        if (!coordinator_counts_valid(coordinator))
        {
            return false;
        }
        return coordinator->shutting_down || protocol_error(coordinator, "участник завершился раньше времени");
    }
    if (coordinator->shutting_down)
    {
        return true;
    }
    if (message->day != coordinator->simulation.current_day || coordinator->done[slot] ||
        (slot == 0 ? coordinator->phase != PHASE_10_LIBRARIAN : coordinator->phase != PHASE_10_READERS))
    {
        return protocol_error(coordinator, "сообщение не соответствует модельному дню или фазе");
    }
    if (message->type == IPC_10_PHASE_DONE)
    {
        return phase_done(coordinator, slot);
    }
    if (slot == 0)
    {
        if (message->type != IPC_10_PROCESS_RETURNS || coordinator->returns_processed)
        {
            return protocol_error(coordinator, "неверная или повторная команда Librarian");
        }
        simulation_process_returns(&coordinator->simulation, message->day);
        coordinator->returns_processed = true;
        return send_reply(coordinator, slot, IPC_10_RESPONSE, 0, true);
    }
    return reader_operation(coordinator, message, slot);
}

// продолжаем обслуживать сообщения, пока участники не подтвердят всю фазу
static bool wait_phase(Coordinator10 *coordinator)
{
    SOFT_ASSERT(coordinator != NULL, "Указатель на Coordinator = NULL", false);
    SOFT_ASSERT(coordinator->ipc != NULL && coordinator->simulation.reader_count > 0 &&
                    coordinator->simulation.reader_count <= MAX_READERS,
        "Не подготовлены IPC или участники ожидаемой фазы", false);
    SOFT_ASSERT(coordinator->phase == PHASE_10_LIBRARIAN || coordinator->phase == PHASE_10_READERS,
        "Ожидание вне рабочей фазы", false);
    long long last_message = ipc_10_now();
    if (last_message < 0)
    {
        coordinator->failed = true;
        return false;
    }
    while (true)
    {
        bool complete = true;
        int begin = coordinator->phase == PHASE_10_LIBRARIAN ? 0 : 1;
        int end = coordinator->phase == PHASE_10_LIBRARIAN ? 1 : coordinator->simulation.reader_count + 1;
        for (int slot = begin; slot < end; ++slot)
        {
            complete = complete && coordinator->done[slot];
        }
        if (complete)
        {
            return true;
        }
        Ipc10Message message;
        Ipc10Result result = ipc_10_receive(coordinator->ipc->queues[IPC_10_REQUESTS], &message, true, 100);
        if (result == IPC_10_STOPPED)
        {
            return false;
        }
        if (result == IPC_10_FAILED)
        {
            coordinator->failed = true;
            return false;
        }
        long long now = ipc_10_now();
        if (now < 0 || now - last_message >= IPC_10_TIMEOUT_MS)
        {
            return protocol_error(coordinator, "участник не завершил фазу в срок");
        }
        if (result == IPC_10_OK)
        {
            last_message = now;
            if (!handle_message(coordinator, &message))
            {
                return false;
            }
        }
    }
}

// сначала передаём данные дня, затем открываем стартовый семафор
static bool start_participant(Coordinator10 *coordinator, int slot)
{
    SOFT_ASSERT(coordinator != NULL && coordinator->ipc != NULL, "Нет Coordinator или IPC", false);
    SOFT_ASSERT(coordinator->simulation.reader_count > 0 && coordinator->simulation.reader_count <= MAX_READERS &&
                    slot >= 0 && slot <= coordinator->simulation.reader_count,
        "Индекс запускаемого участника вне массива", false);
    SOFT_ASSERT(coordinator->connected[slot] && !coordinator->exited[slot] && !coordinator->done[slot] &&
                    (slot == 0 ? coordinator->phase == PHASE_10_LIBRARIAN : coordinator->phase == PHASE_10_READERS),
        "Участник не готов к запуску своей фазы", false);
    if (!send_reply(coordinator, slot, IPC_10_START_DAY, 0, true))
    {
        return false;
    }
    if (!ipc_10_post(coordinator->ipc->semaphores[IPC_10_LIBRARIAN + slot]))
    {
        coordinator->failed = true;
        return false;
    }
    return true;
}

// пауза нужна для демонстрации, сообщения и сигналы продолжают обрабатываться
static bool display_delay(Coordinator10 *coordinator, int delay_ms)
{
    SOFT_ASSERT(coordinator != NULL && delay_ms >= 0, "Некорректная задержка отображения", false);
    SOFT_ASSERT(coordinator->ipc != NULL && coordinator->phase == PHASE_10_IDLE,
        "Задержка отображения допустима только между днями", false);
    long long now = ipc_10_now();
    if (now < 0)
    {
        coordinator->failed = true;
        return false;
    }
    long long deadline = now + delay_ms;
    while (now < deadline)
    {
        int timeout_ms = deadline - now > 100 ? 100 : (int)(deadline - now);
        Ipc10Message message;
        Ipc10Result result = ipc_10_receive(coordinator->ipc->queues[IPC_10_REQUESTS], &message, true, timeout_ms);
        if (result == IPC_10_STOPPED)
        {
            return false;
        }
        if (result == IPC_10_FAILED)
        {
            coordinator->failed = true;
            return false;
        }
        if (result == IPC_10_OK && !handle_message(coordinator, &message))
        {
            return false;
        }
        now = ipc_10_now();
        if (now < 0)
        {
            coordinator->failed = true;
            return false;
        }
    }
    return !stop_requested;
}

// регистрация, возвраты и посещения образуют последовательные этапы дня
static bool coordinator_days(Coordinator10 *coordinator, int total_days, int delay_ms)
{
    SOFT_ASSERT(coordinator != NULL && total_days > 0, "Нет Coordinator или модельных дней", false);
    SOFT_ASSERT(delay_ms >= 0 && coordinator->phase == PHASE_10_IDLE && !coordinator->shutting_down,
        "Некорректное начальное состояние моделирования", false);
    if (!coordinator_counts_valid(coordinator))
    {
        return false;
    }
    int expected = coordinator->simulation.reader_count + 1;
    printf("Сессия готова. Ожидается участников: %d\n", expected);
    while (coordinator->connected_count < expected)
    {
        Ipc10Message message;
        Ipc10Result result = ipc_10_receive(coordinator->ipc->queues[IPC_10_REQUESTS], &message, true, 100);
        if (result == IPC_10_TIMEOUT)
        {
            continue;
        }
        if (result != IPC_10_OK || !handle_message(coordinator, &message))
        {
            if (result == IPC_10_FAILED)
            {
                coordinator->failed = true;
            }
            return false;
        }
    }
    printf("Библиотека НАЧАЛА РАБОТАТЬ!. Количество дней работы: %d\n", total_days);
    logger_write(
        "Начало работы библиотеки: %d дней, срок чтения %d\n", total_days, coordinator->simulation.reading_days);
    for (int completed = 0; completed < total_days && !stop_requested; ++completed)
    {
        int day = completed + FIRST_SIMULATION_DAY;
        coordinator->simulation.current_day = day;
        memset(coordinator->done, 0, sizeof coordinator->done);
        memset(coordinator->begun, 0, sizeof coordinator->begun);
        memset(coordinator->reserved, 0, sizeof coordinator->reserved);
        memset(coordinator->wait_for_books, 0, sizeof coordinator->wait_for_books);
        memset(coordinator->pending_book, 0, sizeof coordinator->pending_book);
        SOFT_ASSERT(coordinator->active_reader == -1 && coordinator->waiting_count == 0,
            "Новый день начался до окончания посещений", false);
        coordinator->returns_processed = false;
        coordinator->phase = PHASE_10_LIBRARIAN;
        printf("\nДень %d: начало\n", day);
        if (!start_participant(coordinator, 0) || !wait_phase(coordinator))
        {
            return false;
        }
        coordinator->phase = PHASE_10_READERS;
        for (int slot = 1; slot < expected; ++slot)
        {
            if (!start_participant(coordinator, slot))
            {
                return false;
            }
        }
        if (!wait_phase(coordinator))
        {
            return false;
        }
        printf("День %d: завершён\n", day);
        coordinator->phase = PHASE_10_IDLE;
        if (completed + 1 < total_days && !display_delay(coordinator, delay_ms))
        {
            return false;
        }
    }
    return true;
}

// посылаем SHUTDOWN и ждём EXITED только от реально подключённых участников
static bool shutdown_participants(Coordinator10 *coordinator)
{
    SOFT_ASSERT(coordinator != NULL && coordinator->exited_count >= 0 &&
                    coordinator->exited_count <= coordinator->connected_count &&
                    coordinator->connected_count <= coordinator->simulation.reader_count + 1,
        "Счётчики завершения не согласованы", false);
    if (!coordinator_counts_valid(coordinator))
    {
        return false;
    }
    coordinator->shutting_down = true;
    coordinator->simulation.finished = true;
    bool success = true;
    for (int slot = 0; slot <= coordinator->simulation.reader_count; ++slot)
    {
        if (coordinator->connected[slot] && !coordinator->exited[slot])
        {
            if (!send_reply(coordinator, slot, IPC_10_SHUTDOWN, 0, false) ||
                !ipc_10_post(coordinator->ipc->semaphores[IPC_10_LIBRARIAN + slot]))
            {
                success = false;
            }
        }
    }
    long long now = ipc_10_now();
    if (now < 0)
    {
        success = false;
    }
    long long deadline = now + IPC_10_TIMEOUT_MS;
    while (coordinator->exited_count < coordinator->connected_count && now >= 0 && now < deadline)
    {
        Ipc10Message message;
        Ipc10Result result = ipc_10_receive(coordinator->ipc->queues[IPC_10_REQUESTS], &message, false, 100);
        if (result == IPC_10_FAILED)
        {
            success = false;
            break;
        }
        if (result == IPC_10_OK && !handle_message(coordinator, &message))
        {
            success = false;
        }
        now = ipc_10_now();
    }
    if (coordinator->exited_count != coordinator->connected_count)
    {
        fprintf(stderr, "Не все участники подтвердили выход в срок (%d/%d)\n", coordinator->exited_count,
            coordinator->connected_count);
        success = false;
    }
    // отклоняем регистрацию, которая могла попасть в очередь перед остановкой
    if (coordinator->ipc->queues[IPC_10_REQUESTS] != (mqd_t)-1)
    {
        Ipc10Message message;
        while (ipc_10_receive(coordinator->ipc->queues[IPC_10_REQUESTS], &message, false, 1) == IPC_10_OK)
        {
            if (message.type == IPC_10_REGISTER_PARTICIPANT && !register_participant(coordinator, &message))
            {
                success = false;
            }
        }
    }
    return success;
}

// модель и предметный журнал принадлежат только этому процессу
static bool run_coordinator(Ipc10Resources *ipc, const Options10 *options)
{
    SOFT_ASSERT(ipc != NULL && options != NULL, "Нет IPC или параметров", false);
    SOFT_ASSERT(options->config_filename != NULL && options->delay_ms >= 0,
        "Нет config или отрицательная внутренняя задержка", false);
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
    Coordinator10 coordinator = {.ipc = ipc, .active_reader = -1};
    simulation_initialization(&coordinator.simulation, config.books, config.book_count, config.readers,
        config.reader_count, config.reading_days, &librarian, config.issue_strategy, config.request_rule);
    if (!simulation_validate(&coordinator.simulation, config.total_days))
    {
        return false;
    }
    bool prepared = ipc_10_create(ipc, config.reader_count);
    bool success = prepared && logger_open_append("library.log");
    if (success && options->print_ipc)
    {
        ipc_10_print(ipc);
    }
    if (success)
    {
        success = coordinator_days(&coordinator, config.total_days, options->delay_ms);
        success = (success || stop_requested) && !coordinator.failed;
    }
    if (prepared && !shutdown_participants(&coordinator))
    {
        success = false;
    }
    // все подтверждения получены до удаления очередей и семафоров
    if (!ipc_10_close(ipc))
    {
        success = false;
    }
    if (prepared)
    {
        simulation_print_results(&coordinator.simulation);
    }
    return success;
}

// каждая программа выбирает свою роль без создания остальных процессов
int process_10_main(int argc, char *argv[], Process10Role role)
{
    SOFT_ASSERT(role == PROCESS_10_COORDINATOR || role == PROCESS_10_LIBRARIAN || role == PROCESS_10_READER,
        "Неизвестная роль программы", EXIT_FAILURE);
    Options10 options;
    if (!parse_options(argc, argv, role, &options))
    {
        return EXIT_FAILURE;
    }
    if (options.help)
    {
        printf("Использование: %s --session NAME", argv[0]);
        if (role == PROCESS_10_COORDINATOR)
        {
            printf(" [-c FILE] [-d N] [-r N] [--delay-ms N] [--print-ipc]\nConfig по умолчанию: config.txt\n");
        }
        else
        {
            printf(role == PROCESS_10_READER ? " --reader INDEX\n" : "\n");
        }
        printf("Session: 1-%d символов A-Z, a-z, 0-9, _, -\n", IPC_10_SESSION_MAX);
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
    Ipc10Resources ipc;
    ipc_10_init(&ipc, options.session);
    bool success;
    if (role == PROCESS_10_COORDINATOR)
    {
        success = run_coordinator(&ipc, &options);
    }
    else
    {
        success = process_10_participant(&ipc, role == PROCESS_10_LIBRARIAN ? -1 : options.reader_index);
        if (!ipc_10_close(&ipc))
        {
            success = false;
        }
    }
    logger_close();
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
