#define _POSIX_C_SOURCE 200809L
#include "server.h"
#include "protocol.h"
#include "main_utils.h"
#include "check.h"
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

// один фиксированный пакет вывода вмещает результаты самой большой фазы возвратов
enum
{
    MAX_CONNECTIONS = MAX_READERS + 9,
    MAX_PENDING_MESSAGES = 512,
    POLL_TIMEOUT_MS = 200,
    MAX_READS_PER_POLL = 64
};

typedef enum ServerPhase
{
    SERVER_WAITING,
    SERVER_LIBRARIAN,
    SERVER_READERS,
    SERVER_FINISHED
} ServerPhase;

typedef struct ClientConnection
{
    int fd;
    int reader_index;
    unsigned char receive_buffer[NET_MESSAGE_SIZE];
    size_t received;
    unsigned char send_buffer[NET_MESSAGE_SIZE * MAX_PENDING_MESSAGES];
    size_t send_size;
    size_t sent;
    bool closing;
    bool final_queued;
    bool write_closed;
} ClientConnection;

// прогресс фазы сохраняется после закрытия физического соединения
typedef struct WorkerProgress
{
    int day;
    bool operation_applied;
    bool phase_done;
} WorkerProgress;

typedef struct ServerContext
{
    int listening_fd;
    Simulation *simulation;
    int total_days;
    ServerPhase phase;
    ClientConnection clients[MAX_CONNECTIONS];
    int librarian_slot;
    struct timespec finish_started;
    int reader_slots[MAX_READERS];
    int registered_readers;
    int pending_readers;
    bool failed;
    WorkerProgress librarian;
    WorkerProgress readers[MAX_READERS];
} ServerContext;

static void disconnect_client(ServerContext *server, ClientConnection *connection);

static void begin_shutdown(ServerContext *server, int reason);

static WorkerProgress *worker_progress(ServerContext *server, int index)
{
    SOFT_ASSERT(server != NULL, "Нет сервера logical worker", NULL);
    SOFT_ASSERT(index == NET_LIBRARIAN || (index >= 0 && index < server->simulation->reader_count),
        "Индекс logical worker вне модели", NULL);
    return index == NET_LIBRARIAN ? &server->librarian : &server->readers[index];
}

static bool queue_message(ClientConnection *connection, const NetMessage *message)
{
    SOFT_ASSERT(connection != NULL && message != NULL, "Нет данных отложенной отправки", false);
    SOFT_ASSERT(connection->fd >= 0 && connection->sent <= connection->send_size &&
                    connection->send_size <= sizeof connection->send_buffer,
        "Нарушены границы выходного буфера", false);
    // освобождаем уже отправленную часть без динамической очереди
    if (connection->sent > 0)
    {
        connection->send_size -= connection->sent;
        memmove(connection->send_buffer, connection->send_buffer + connection->sent, connection->send_size);
        connection->sent = 0;
    }
    SOFT_ASSERT(connection->send_size + NET_MESSAGE_SIZE <= sizeof connection->send_buffer,
        "Результаты фазы не помещаются в фиксированный пакет", false);
    if (!protocol_encode(message, connection->send_buffer + connection->send_size))
    {
        return false;
    }
    connection->send_size += NET_MESSAGE_SIZE;
    return true;
}

static void send_to_observer(ServerContext *server, ClientConnection *connection, const NetMessage *message)
{
    SOFT_ASSERT_VOID(server != NULL && connection != NULL && message != NULL, "Нет данных Observer");
    SOFT_ASSERT_VOID(connection->reader_index == NET_OBSERVER && connection->fd >= 0, "Неверная роль Observer");
    SOFT_ASSERT_VOID(connection->sent <= connection->send_size, "Смещение Observer вне пакета");
    // медленный наблюдатель не задерживает рабочие роли и не переполняет пакет
    if (connection->send_size - connection->sent + NET_MESSAGE_SIZE > sizeof connection->send_buffer)
    {
        fprintf(stderr, "Observer не успевает получать события: соединение закрыто\n");
        disconnect_client(server, connection);
        return;
    }
    if (!queue_message(connection, message))
    {
        disconnect_client(server, connection);
    }
}

static void broadcast_observer_event(ServerContext *server, const NetMessage *message)
{
    SOFT_ASSERT_VOID(server != NULL && message != NULL, "Нет события для Observer");
    SOFT_ASSERT_VOID(message->type == NET_OBSERVER_EVENT, "Рассылается сообщение, не являющееся событием");
    // адресные ответы не попадают в рассылку событий наблюдателям
    for (int slot = 0; slot < MAX_CONNECTIONS; ++slot)
    {
        ClientConnection *connection = &server->clients[slot];
        if (connection->fd >= 0 && connection->reader_index == NET_OBSERVER)
        {
            send_to_observer(server, connection, message);
        }
    }
}

static int observer_count(const ServerContext *server)
{
    SOFT_ASSERT(server != NULL, "Нет сервера подсчёта Observer", 0);
    int count = 0;
    for (int slot = 0; slot < MAX_CONNECTIONS; ++slot)
    {
        count += server->clients[slot].fd >= 0 && server->clients[slot].reader_index == NET_OBSERVER;
    }
    return count;
}

static void observe_phase(ServerContext *server, SimulationEvent event, int reader_index)
{
    SOFT_ASSERT_VOID(server != NULL, "Нет сервера события фазы");
    SOFT_ASSERT_VOID(event >= EVENT_SIMULATION_START && event <= EVENT_SIMULATION_DONE, "Неверное событие фазы");
    NetMessage message = {.type = NET_OBSERVER_EVENT,
        .reader_index = reader_index,
        .day = server->simulation->current_day,
        .code = event};
    broadcast_observer_event(server, &message);
}

static void send_to_slot(ServerContext *server, int slot, const NetMessage *message)
{
    SOFT_ASSERT_VOID(server != NULL && message != NULL, "Нет внутреннего получателя");
    if (slot < 0)
    {
        return;
    }
    SOFT_ASSERT_VOID(slot < MAX_CONNECTIONS, "Индекс соединения вне массива");
    if (!queue_message(&server->clients[slot], message))
    {
        server->failed = true;
    }
}

static void on_simulation_event(void *context, SimulationEvent event, int reader_id, int book_id, int day, int value)
{
    SOFT_ASSERT_VOID(context != NULL, "Нет контекста предметного события");
    ServerContext *server = context;
    SOFT_ASSERT_VOID(
        server->phase == SERVER_LIBRARIAN || server->phase == SERVER_READERS, "Предметное событие вне рабочей фазы");
    int index = -1;
    for (int idx = 0; idx < server->simulation->reader_count; ++idx)
    {
        if (server->simulation->readers[idx].id == reader_id)
        {
            index = idx;
        }
    }
    SOFT_ASSERT_VOID(index >= 0 && index < server->simulation->reader_count, "Не найден читатель события");
    NetMessage message = {
        .type = NET_EVENT, .reader_index = index, .day = day, .book_id = book_id, .code = event, .value = value};
    NetMessage observer_message = message;
    observer_message.type = NET_OBSERVER_EVENT;
    broadcast_observer_event(server, &observer_message);
    if (server->phase == SERVER_LIBRARIAN)
    {
        // библиотекарь получает результаты своей операции возврата
        send_to_slot(server, server->librarian_slot, &message);
        if (event == EVENT_NOTIFICATION || event == EVENT_BOOK_RESERVED || event == EVENT_BOOK_RETURNED)
        {
            send_to_slot(server, server->reader_slots[index], &message);
        }
    }
    else
    {
        send_to_slot(server, server->reader_slots[index], &message);
    }
}

static void start_day(ServerContext *server)
{
    SOFT_ASSERT_VOID(server != NULL, "Нет сервера для начала дня");
    SOFT_ASSERT_VOID(server->simulation->current_day < server->total_days, "Дни моделирования исчерпаны");
    if (server->simulation->current_day == 0)
    {
        observe_phase(server, EVENT_SIMULATION_START, NET_OBSERVER);
    }
    ++server->simulation->current_day;
    observe_phase(server, EVENT_DAY_START, NET_OBSERVER);
    observe_phase(server, EVENT_LIBRARIAN_START, NET_LIBRARIAN);
    server->phase = SERVER_LIBRARIAN;
    server->librarian = (WorkerProgress){.day = server->simulation->current_day};
    NetMessage message = {
        .type = NET_START_LIBRARIAN_DAY, .reader_index = NET_LIBRARIAN, .day = server->simulation->current_day};
    send_to_slot(server, server->librarian_slot, &message);
    printf("День %d: начало серверной фазы возвратов\n", message.day);
}

static void start_readers(ServerContext *server)
{
    SOFT_ASSERT_VOID(server != NULL && server->phase == SERVER_LIBRARIAN, "Неверная фаза запуска Reader");
    server->phase = SERVER_READERS;
    // отсутствующие читатели остаются в барьере до возвращения и завершения фазы
    server->pending_readers = server->simulation->reader_count;
    for (int idx = 0; idx < server->simulation->reader_count; ++idx)
    {
        server->readers[idx] = (WorkerProgress){.day = server->simulation->current_day};
        NetMessage message = {
            .type = NET_START_READER_DAY, .reader_index = idx, .day = server->simulation->current_day};
        send_to_slot(server, server->reader_slots[idx], &message);
        observe_phase(server, EVENT_READER_START, idx);
    }
    printf("День %d: сервер принимает посещения\n", server->simulation->current_day);
}

static void finish_simulation(ServerContext *server)
{
    SOFT_ASSERT_VOID(server != NULL && server->pending_readers == 0, "Остались незавершённые фазы");
    server->phase = SERVER_FINISHED;
    server->simulation->finished = true;
    simulation_print_results(server->simulation);
    observe_phase(server, EVENT_SIMULATION_DONE, NET_OBSERVER);
    begin_shutdown(server, NET_FINISH_NORMAL);
}

static void begin_shutdown(ServerContext *server, int reason)
{
    SOFT_ASSERT_VOID(server != NULL, "Нет сервера для shutdown");
    SOFT_ASSERT_VOID(reason == NET_FINISH_NORMAL || reason == NET_FINISH_SHUTDOWN, "Неверная причина завершения");
    server->phase = SERVER_FINISHED;
    network_close(&server->listening_fd);
    if (clock_gettime(CLOCK_MONOTONIC, &server->finish_started) != 0)
    {
        perror("clock_gettime");
        server->failed = true;
        return;
    }
    for (int slot = 0; slot < MAX_CONNECTIONS; ++slot)
    {
        ClientConnection *connection = &server->clients[slot];
        if (connection->fd < 0 || connection->closing || connection->final_queued)
        {
            continue;
        }
        SOFT_ASSERT_VOID(
            connection->sent <= connection->send_size && connection->send_size <= sizeof connection->send_buffer,
            "Нарушены границы пакета при завершении");
        // финальный кадр добавляем один раз, сохраняя порядок уже отправляемых данных
        NetMessage message = {.type = NET_FINISHED,
            .reader_index = connection->reader_index,
            .day = server->simulation->current_day,
            .code = reason};
        if (connection->send_size - connection->sent + NET_MESSAGE_SIZE > sizeof connection->send_buffer ||
            !queue_message(connection, &message))
        {
            disconnect_client(server, connection);
        }
        else
        {
            connection->final_queued = true;
        }
    }
}

static void synchronize_worker(ServerContext *server, ClientConnection *connection)
{
    SOFT_ASSERT_VOID(server != NULL && connection != NULL, "Нет worker для синхронизации");
    int index = connection->reader_index;
    WorkerProgress *worker = worker_progress(server, index);
    SOFT_ASSERT_VOID(worker != NULL, "Нет logical progress");
    SOFT_ASSERT_VOID(worker->day >= 0 && worker->day <= server->simulation->current_day,
        "День логического участника опережает модель");
    // восстановленный клиент продолжает шаг с серверного состояния
    bool current = worker->day == server->simulation->current_day;
    bool active = current && !worker->phase_done &&
                  (index == NET_LIBRARIAN ? server->phase == SERVER_LIBRARIAN : server->phase == SERVER_READERS);
    NetMessage sync = {.type = NET_WORKER_SYNC,
        .reader_index = index,
        .day = server->simulation->current_day,
        .code = (active ? NET_SYNC_ACTIVE : 0) | (current && worker->operation_applied ? NET_SYNC_APPLIED : 0) |
                (current && worker->phase_done ? NET_SYNC_PHASE_DONE : 0)};
    if (!queue_message(connection, &sync))
    {
        server->failed = true;
    }
}

static void reject_registration(ClientConnection *connection, int code)
{
    SOFT_ASSERT_VOID(connection != NULL && connection->reader_index == NET_UNREGISTERED,
        "Отклоняется уже зарегистрированный участник");
    NetMessage error = {.type = NET_ERROR, .reader_index = NET_UNREGISTERED, .code = code};
    if (!queue_message(connection, &error))
    {
        network_close(&connection->fd);
    }
    connection->closing = true;
    fprintf(stderr, "Регистрация отклонена: код %d\n", code);
}

static void register_connection(ServerContext *server, int slot, const NetMessage *message)
{
    SOFT_ASSERT_VOID(server != NULL && message != NULL, "Нет данных регистрации");
    SOFT_ASSERT_VOID(slot >= 0 && slot < MAX_CONNECTIONS, "Неверный слот регистрации");
    ClientConnection *connection = &server->clients[slot];
    int index = message->reader_index;
    bool librarian = message->type == NET_REGISTER_LIBRARIAN;
    if (message->type == NET_REGISTER_OBSERVER)
    {
        if (index != NET_OBSERVER || message->day != 0 || message->book_id != 0 || message->code != 0 ||
            message->value != 0 || server->phase == SERVER_FINISHED)
        {
            reject_registration(connection, 1);
            return;
        }
        // сохраняем места всех рабочих ролей и один слот для ответа об отказе
        int limit = MAX_CONNECTIONS - server->simulation->reader_count - 2;
        if (observer_count(server) >= limit)
        {
            reject_registration(connection, 4);
            return;
        }
        connection->reader_index = NET_OBSERVER;
        NetMessage response = {.type = NET_REGISTER_OK,
            .reader_index = NET_OBSERVER,
            .day = server->simulation->current_day,
            .value = server->total_days};
        send_to_observer(server, connection, &response);
        printf("Зарегистрирован Observer; slot=%d\n", slot);
        return;
    }
    if (message->day != 0 || message->book_id != 0 || message->code != 0 || message->value != 0 ||
        (librarian ? (index != NET_LIBRARIAN || server->librarian_slot >= 0)
                   : (message->type != NET_REGISTER_READER || index < 0 || index >= server->simulation->reader_count ||
                         server->reader_slots[index] >= 0)))
    {
        reject_registration(connection, 1);
        return;
    }
    if (server->phase == SERVER_FINISHED)
    {
        reject_registration(connection, 2);
        return;
    }
    connection->reader_index = index;
    int visit_count = 0;
    if (librarian)
    {
        server->librarian_slot = slot;
    }
    else
    {
        server->reader_slots[index] = slot;
        ++server->registered_readers;
        visit_count = server->simulation->readers[index].visit_count;
    }
    SOFT_ASSERT_VOID(server->registered_readers <= server->simulation->reader_count, "Превышен состав Reader");
    NetMessage response = {
        .type = NET_REGISTER_OK, .reader_index = index, .code = visit_count, .value = server->total_days};
    send_to_slot(server, slot, &response);
    for (int idx = 0; idx < visit_count; ++idx)
    {
        response = (NetMessage){
            .type = NET_VISIT_DAY, .reader_index = index, .day = server->simulation->readers[index].visit_days[idx]};
        send_to_slot(server, slot, &response);
    }
    printf("Зарегистрирован %s %d\n", librarian ? "Librarian" : "Reader", index);
    synchronize_worker(server, connection);
    if (server->phase == SERVER_WAITING && server->librarian_slot >= 0 &&
        server->registered_readers == server->simulation->reader_count)
    {
        start_day(server);
    }
}

static bool process_operation(ServerContext *server, ClientConnection *connection, const NetMessage *message)
{
    SOFT_ASSERT(server != NULL && connection != NULL && message != NULL, "Нет данных операции", false);
    int index = connection->reader_index;
    int day = server->simulation->current_day;
    bool librarian = index == NET_LIBRARIAN;
    WorkerProgress *worker = worker_progress(server, index);
    SOFT_ASSERT(worker != NULL, "Нет progress logical operation", false);
    if (worker->day != day || worker->operation_applied || worker->phase_done ||
        (librarian ? (server->phase != SERVER_LIBRARIAN || message->type != NET_PROCESS_RETURNS)
                   : (server->phase != SERVER_READERS || message->type != NET_READER_VISIT ||
                         !is_reader_visit_on_said_day(&server->simulation->readers[index], day))))
    {
        return false;
    }
    if (librarian)
    {
        simulation_process_returns(server->simulation, day);
    }
    else
    {
        simulation_process_reader(server->simulation, index, day);
    }
    if (!simulation_validate(server->simulation, server->total_days))
    {
        server->failed = true;
        return false;
    }
    // фиксируем применение до отправки ответа, чтобы разрыв не вызвал повтор операции
    worker->operation_applied = true;
    NetMessage response = {.type = NET_OPERATION_DONE, .reader_index = index, .day = day};
    if (!queue_message(connection, &response))
    {
        server->failed = true;
    }
    printf("День %d: запрос %s %d обработан\n", day, librarian ? "Librarian" : "Reader", index);
    return true;
}

static bool process_phase_done(ServerContext *server, ClientConnection *connection)
{
    SOFT_ASSERT(server != NULL && connection != NULL, "Нет данных завершения фазы", false);
    int index = connection->reader_index;
    WorkerProgress *worker = worker_progress(server, index);
    SOFT_ASSERT(worker != NULL, "Нет progress завершения фазы", false);
    if (worker->day != server->simulation->current_day || worker->phase_done)
    {
        return false;
    }
    if (index == NET_LIBRARIAN)
    {
        if (server->phase != SERVER_LIBRARIAN || !worker->operation_applied)
        {
            return false;
        }
        worker->phase_done = true;
        observe_phase(server, EVENT_LIBRARIAN_DONE, NET_LIBRARIAN);
        start_readers(server);
        return true;
    }
    if (server->phase != SERVER_READERS ||
        (is_reader_visit_on_said_day(&server->simulation->readers[index], server->simulation->current_day) &&
            !worker->operation_applied))
    {
        return false;
    }
    worker->phase_done = true;
    observe_phase(server, EVENT_READER_DONE, index);
    SOFT_ASSERT(server->pending_readers > 0, "Число ожидаемых Reader уже равно нулю", false);
    --server->pending_readers;
    if (server->pending_readers == 0)
    {
        observe_phase(server, EVENT_DAY_DONE, NET_OBSERVER);
        printf("День %d: серверная обработка завершена\n", server->simulation->current_day);
        if (server->simulation->current_day == server->total_days)
        {
            finish_simulation(server);
        }
        else
        {
            start_day(server);
        }
    }
    return true;
}

static void process_message(ServerContext *server, int slot, const NetMessage *message)
{
    SOFT_ASSERT_VOID(server != NULL && message != NULL, "Нет сообщения сервера");
    SOFT_ASSERT_VOID(slot >= 0 && slot < MAX_CONNECTIONS, "Неверный слот сообщения");
    ClientConnection *connection = &server->clients[slot];
    // до регистрации соединение не может отправлять команды предметной модели
    if (connection->reader_index == NET_UNREGISTERED)
    {
        register_connection(server, slot, message);
        return;
    }
    if (connection->reader_index == NET_OBSERVER)
    {
        fprintf(stderr, "Observer прислал недопустимую команду\n");
        disconnect_client(server, connection);
        return;
    }
    bool valid = message->reader_index == connection->reader_index && message->day == server->simulation->current_day &&
                 message->day > 0 && message->book_id == 0 && message->code == 0 && message->value == 0;
    if (valid)
    {
        valid = message->type == NET_PHASE_DONE ? process_phase_done(server, connection)
                                                : process_operation(server, connection, message);
    }
    if (!valid)
    {
        fprintf(stderr, "Ошибка протокола: роль %d, день %d, тип %u\n", connection->reader_index, message->day,
            message->type);
        server->failed = true;
    }
}

static void disconnect_client(ServerContext *server, ClientConnection *connection)
{
    SOFT_ASSERT_VOID(server != NULL && connection != NULL, "Нет соединения для отключения");
    int slot = (int)(connection - server->clients);
    SOFT_ASSERT_VOID(slot >= 0 && slot < MAX_CONNECTIONS, "Слот отключаемого соединения вне массива");
    int index = connection->reader_index;
    if (index == NET_OBSERVER)
    {
        printf("Observer отключён; slot=%d\n", (int)(connection - server->clients));
    }
    else if (index == NET_LIBRARIAN)
    {
        SOFT_ASSERT_VOID(server->librarian_slot == slot, "Привязка Librarian не соответствует соединению");
        server->librarian_slot = -1;
        printf("Отключился рабочий клиент Librarian; ожидание reconnect\n");
    }
    else if (index >= 0)
    {
        SOFT_ASSERT_VOID(index < server->simulation->reader_count, "Индекс отключённого worker вне модели");
        SOFT_ASSERT_VOID(server->reader_slots[index] == slot && server->registered_readers > 0,
            "Привязка Reader или число подключённых участников нарушены");
        server->reader_slots[index] = -1;
        --server->registered_readers;
        printf("Отключился рабочий клиент Reader %d; ожидание reconnect\n", index);
    }
    // очищаем socket и буферы, оставляя WorkerProgress для следующего подключения
    network_close(&connection->fd);
    *connection = (ClientConnection){.fd = -1, .reader_index = NET_UNREGISTERED};
}

static void read_client(ServerContext *server, int slot)
{
    SOFT_ASSERT_VOID(server != NULL && slot >= 0 && slot < MAX_CONNECTIONS, "Нет внутреннего соединения чтения");
    ClientConnection *connection = &server->clients[slot];
    for (int attempt = 0;
         attempt < MAX_READS_PER_POLL && !server->failed && !connection->closing && connection->fd >= 0; ++attempt)
    {
        SOFT_ASSERT_VOID(connection->received < NET_MESSAGE_SIZE, "Переполнен входной буфер");
        ssize_t count = recv(connection->fd, connection->receive_buffer + connection->received,
            NET_MESSAGE_SIZE - connection->received, 0);
        if (count < 0)
        {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
            {
                return;
            }
            perror("recv server");
            disconnect_client(server, connection);
            return;
        }
        if (count == 0)
        {
            if (connection->received != 0)
            {
                fprintf(stderr, "Ошибка протокола: EOF посередине сообщения\n");
            }
            disconnect_client(server, connection);
            return;
        }
        // при завершении дочитываем входящие байты, не применяя новые операции
        if (server->phase == SERVER_FINISHED)
        {
            connection->received = 0;
            continue;
        }
        connection->received += (size_t)count;
        // декодируем только полный кадр, остаток ждём в следующем проходе poll
        if (connection->received == NET_MESSAGE_SIZE)
        {
            NetMessage message;
            connection->received = 0;
            if (!protocol_decode(connection->receive_buffer, &message))
            {
                if (connection->reader_index == NET_UNREGISTERED)
                {
                    reject_registration(connection, 3);
                }
                else if (connection->reader_index == NET_OBSERVER)
                {
                    disconnect_client(server, connection);
                }
                else
                {
                    server->failed = true;
                }
                return;
            }
            process_message(server, slot, &message);
        }
    }
}

static void write_client(ServerContext *server, ClientConnection *connection)
{
    SOFT_ASSERT_VOID(server != NULL && connection != NULL, "Нет данных записи");
    SOFT_ASSERT_VOID(connection->sent <= connection->send_size, "Смещение отправки вне пакета");
    if (connection->sent < connection->send_size)
    {
        ssize_t count = send(connection->fd, connection->send_buffer + connection->sent,
            connection->send_size - connection->sent, MSG_NOSIGNAL);
        if (count < 0)
        {
            if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
            {
                perror("send server");
                disconnect_client(server, connection);
            }
            return;
        }
        if (count == 0)
        {
            disconnect_client(server, connection);
            return;
        }
        connection->sent += (size_t)count;
    }
    if (connection->sent == connection->send_size)
    {
        connection->sent = 0;
        connection->send_size = 0;
        if (connection->final_queued && !connection->write_closed)
        {
            // сообщаем EOF после финального кадра, сохраняя возможность дочитать ответы клиента
            if (shutdown(connection->fd, SHUT_WR) != 0)
            {
                perror("shutdown write");
                disconnect_client(server, connection);
                return;
            }
            connection->write_closed = true;
        }
        if (connection->closing)
        {
            network_close(&connection->fd);
        }
    }
}

static void accept_clients(ServerContext *server)
{
    SOFT_ASSERT_VOID(server != NULL && server->listening_fd >= 0, "Нет listening socket");
    // ограничиваем число accept за проход, сохраняя обслуживание рабочих клиентов
    for (int attempt = 0; attempt < MAX_CONNECTIONS; ++attempt)
    {
        int fd = accept(server->listening_fd, NULL, NULL);
        if (fd < 0)
        {
            if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
            {
                perror("accept");
                server->failed = true;
            }
            return;
        }
        int slot = -1;
        for (int idx = 0; idx < MAX_CONNECTIONS && slot < 0; ++idx)
        {
            if (server->clients[idx].fd < 0)
            {
                slot = idx;
            }
        }
        if (slot < 0 || !network_set_nonblocking(fd))
        {
            fprintf(stderr, "Соединение отклонено: нет доступного слота/socket\n");
            network_close(&fd);
            continue;
        }
        server->clients[slot] = (ClientConnection){.fd = fd, .reader_index = NET_UNREGISTERED};
        printf("Принято TCP-соединение\n");
    }
}

static bool has_active_connections(const ServerContext *server)
{
    SOFT_ASSERT(server != NULL, "Нет сервера для проверки вывода", false);
    for (int idx = 0; idx < MAX_CONNECTIONS; ++idx)
    {
        if (server->clients[idx].fd >= 0)
        {
            return true;
        }
    }
    return false;
}

static void poll_loop(ServerContext *server)
{
    SOFT_ASSERT_VOID(server != NULL, "Нет сервера для poll");
    while (!server->failed && (server->phase != SERVER_FINISHED || has_active_connections(server)))
    {
        if (stop_requested && server->phase != SERVER_FINISHED)
        {
            begin_shutdown(server, NET_FINISH_SHUTDOWN);
        }
        // общий deadline не позволяет зависшему клиенту удерживать сервер
        if (server->phase == SERVER_FINISHED)
        {
            struct timespec now;
            if (clock_gettime(CLOCK_MONOTONIC, &now) != 0 || now.tv_sec - server->finish_started.tv_sec >= 2)
            {
                for (int slot = 0; slot < MAX_CONNECTIONS; ++slot)
                {
                    if (server->clients[slot].fd >= 0)
                    {
                        disconnect_client(server, &server->clients[slot]);
                    }
                }
            }
        }
        SOFT_ASSERT_VOID(server->phase >= SERVER_WAITING && server->phase <= SERVER_FINISHED, "Неверная фаза сервера");
        struct pollfd descriptors[MAX_CONNECTIONS + 1];
        descriptors[0] =
            (struct pollfd){.fd = server->phase == SERVER_FINISHED ? -1 : server->listening_fd, .events = POLLIN};
        for (int idx = 0; idx < MAX_CONNECTIONS; ++idx)
        {
            ClientConnection *connection = &server->clients[idx];
            descriptors[idx + 1] = (struct pollfd){.fd = connection->fd, .events = connection->closing ? 0 : POLLIN};
            if (connection->send_size > connection->sent)
            {
                descriptors[idx + 1].events |= POLLOUT;
            }
        }
        // запись запрашивается только для соединений с неотправленными данными
        int ready = poll(descriptors, MAX_CONNECTIONS + 1, POLL_TIMEOUT_MS);
        if (ready < 0)
        {
            if (errno != EINTR)
            {
                perror("poll");
                server->failed = true;
            }
            continue;
        }
        if (stop_requested && server->phase != SERVER_FINISHED)
        {
            continue;
        }
        if (descriptors[0].revents & POLLIN)
        {
            accept_clients(server);
        }
        if (descriptors[0].revents & (POLLERR | POLLHUP | POLLNVAL))
        {
            fprintf(stderr, "Ошибка listening socket\n");
            server->failed = true;
        }
        for (int idx = 0; idx < MAX_CONNECTIONS && !server->failed; ++idx)
        {
            ClientConnection *connection = &server->clients[idx];
            short events = descriptors[idx + 1].revents;
            if (connection->fd < 0 || descriptors[idx + 1].fd != connection->fd)
            {
                continue;
            }
            if ((events & (POLLIN | POLLHUP)) && !connection->closing)
            {
                read_client(server, idx);
            }
            if (connection->fd >= 0 && (events & POLLOUT))
            {
                write_client(server, connection);
            }
            if (connection->fd >= 0 && (events & (POLLERR | POLLNVAL)))
            {
                fprintf(stderr, "Ошибка клиентского socket\n");
                disconnect_client(server, connection);
            }
            if (connection->fd >= 0 && (events & POLLHUP) && (connection->closing || server->phase == SERVER_FINISHED))
            {
                disconnect_client(server, connection);
            }
        }
    }
}

bool server_run(Simulation *simulation, const NetworkOptions *options)
{
    SOFT_ASSERT(simulation != NULL && options != NULL, "Нет данных сервера", false);
    SOFT_ASSERT(
        simulation->reader_count > 0 && simulation->reader_count <= MAX_READERS, "Некорректный состав Reader", false);
    ServerContext server = {
        .listening_fd = -1, .simulation = simulation, .total_days = options->total_days, .librarian_slot = -1};
    for (int idx = 0; idx < MAX_CONNECTIONS; ++idx)
    {
        server.clients[idx].fd = -1;
        server.clients[idx].reader_index = NET_UNREGISTERED;
    }
    for (int idx = 0; idx < MAX_READERS; ++idx)
    {
        server.reader_slots[idx] = -1;
    }
    server.listening_fd = network_listen(options->host, options->port);
    if (server.listening_fd < 0)
    {
        return false;
    }
    simulation->event_handler = on_simulation_event;
    simulation->event_context = &server;
    printf("READY %s:%u; Reader=%d; days=%d; reading_days=%d; strategy=%s; request_rule=%s\n", options->host,
        (unsigned)options->port, simulation->reader_count, server.total_days, simulation->reading_days,
        simulation->issue_strategy == ISSUE_FIFO ? "fifo" : "lifo",
        simulation->request_rule == REQUEST_ALWAYS_WAIT ? "always_wait" : "on_unavailable");
    poll_loop(&server);
    for (int idx = 0; idx < MAX_CONNECTIONS; ++idx)
    {
        network_close(&server.clients[idx].fd);
    }
    network_close(&server.listening_fd);
    simulation->event_handler = NULL;
    simulation->event_context = NULL;
    if (stop_requested)
    {
        fprintf(stderr, "Получен сигнал: сервер остановлен\n");
    }
    printf("Сервер завершён\n");
    return !server.failed && server.phase == SERVER_FINISHED;
}
