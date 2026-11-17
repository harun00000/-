#include "client.h"
#include "protocol.h"
#include "reader.h"
#include "check.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct ClientContext
{
    int fd;
    int reader_index;
    int total_days;
    int last_day;
    Reader reader;
    bool finished;
} ClientContext;

static bool receive_message(ClientContext *client, NetMessage *message)
{
    SOFT_ASSERT(client != NULL && message != NULL, "Нет данных получения client", false);
    if (!protocol_receive_message(client->fd, message))
    {
        return false;
    }
    // штатный конец допустим и вместо ответа регистрации, и посередине операции
    if (protocol_is_finished(message) &&
        (message->reader_index == client->reader_index || message->reader_index == NET_UNREGISTERED))
    {
        if (message->code == NET_FINISH_NORMAL && client->total_days > 0 && message->day != client->total_days)
        {
            fprintf(stderr, "Ошибка протокола: преждевременный normal finish\n");
            return false;
        }
        printf(message->code == NET_FINISH_NORMAL ? "Моделирование завершено: день %d\n"
                                                  : "Сервер завершает работу: день %d\n",
            message->day);
        client->finished = true;
    }
    return true;
}

static bool print_event(const ClientContext *client, const NetMessage *message)
{
    SOFT_ASSERT(client != NULL && message != NULL, "Нет события клиента", false);
    if (client->reader_index >= 0 && message->reader_index != client->reader_index)
    {
        fprintf(stderr, "Ошибка протокола: событие другого Reader\n");
        return false;
    }
    return protocol_print_event(message);
}

static bool send_message(ClientContext *client, const NetMessage *message)
{
    SOFT_ASSERT(client != NULL && message != NULL, "Нет сообщения отправки client", false);
    if (protocol_send_message(client->fd, message))
    {
        return true;
    }
    // финальное сообщение могло прийти одновременно с последней отправкой клиента
    NetMessage pending;
    while (receive_message(client, &pending))
    {
        if (client->finished)
        {
            return true;
        }
        if (pending.type == NET_EVENT && !print_event(client, &pending))
        {
            return false;
        }
    }
    return false;
}

static bool register_client(ClientContext *client)
{
    SOFT_ASSERT(client != NULL, "Нет контекста регистрации", false);
    // регистрируем роль, не загружая конфигурацию на стороне клиента
    NetMessage message = {.type = client->reader_index == NET_LIBRARIAN ? NET_REGISTER_LIBRARIAN : NET_REGISTER_READER,
        .reader_index = client->reader_index};
    if (!send_message(client, &message) || client->finished)
    {
        return client->finished;
    }
    if (!receive_message(client, &message))
    {
        return false;
    }
    if (client->finished)
    {
        return true;
    }
    if (message.type == NET_ERROR)
    {
        fprintf(stderr, "Ошибка регистрации: отказ сервера (код %d)\n", message.code);
        return false;
    }
    if (message.type != NET_REGISTER_OK || message.reader_index != client->reader_index || message.value < 1 ||
        message.code < 0 || message.code > READER_VISIT_CAPACITY ||
        (client->reader_index == NET_LIBRARIAN && message.code != 0))
    {
        fprintf(stderr, "Ошибка протокола регистрации\n");
        return false;
    }
    // личное расписание получаем от единственного владельца модели
    client->total_days = message.value;
    client->reader.visit_count = message.code;
    for (int idx = 0; idx < client->reader.visit_count; ++idx)
    {
        if (!receive_message(client, &message))
        {
            return false;
        }
        if (client->finished)
        {
            return true;
        }
        if (message.type != NET_VISIT_DAY || message.reader_index != client->reader_index || message.day < 1)
        {
            fprintf(stderr, "Ошибка протокола расписания\n");
            return false;
        }
        client->reader.visit_days[idx] = message.day;
    }
    printf("Регистрация подтверждена: %s %d\n", client->reader_index == NET_LIBRARIAN ? "Librarian" : "Reader",
        client->reader_index);
    return true;
}

static bool perform_phase(ClientContext *client, int day, bool applied)
{
    SOFT_ASSERT(client != NULL && day >= 1, "Нет данных текущей фазы", false);
    bool librarian = client->reader_index == NET_LIBRARIAN;
    bool visit = !librarian && is_reader_visit_on_said_day(&client->reader, day);
    NetMessage message = {
        .type = librarian ? NET_PROCESS_RETURNS : NET_READER_VISIT, .reader_index = client->reader_index, .day = day};
    if (!applied && (librarian || visit))
    {
        printf("День %d: %s\n", day, librarian ? "обработка возвратов" : "посещение библиотеки");
        if (!send_message(client, &message))
        {
            return false;
        }
        if (client->finished)
        {
            return true;
        }
        // результаты операции могут приходить вместе с личными уведомлениями
        while (true)
        {
            if (!receive_message(client, &message))
            {
                return false;
            }
            if (client->finished)
            {
                return true;
            }
            if (message.type == NET_OPERATION_DONE && message.reader_index == client->reader_index &&
                message.day == day)
            {
                break;
            }
            if (message.type != NET_EVENT || !print_event(client, &message))
            {
                fprintf(stderr, "Ошибка протокола ответа операции\n");
                return false;
            }
        }
    }
    else
    {
        printf("День %d: %s\n", day, applied ? "операция уже применена сервером" : "посещение не запланировано");
    }
    message = (NetMessage){.type = NET_PHASE_DONE, .reader_index = client->reader_index, .day = day};
    printf("День %d: фаза завершена\n", day);
    return send_message(client, &message);
}

static bool client_run(ClientContext *client)
{
    SOFT_ASSERT(client != NULL, "Нет контекста client loop", false);
    NetMessage message;
    while (!client->finished && receive_message(client, &message))
    {
        if (client->finished)
        {
            return true;
        }
        if (message.type == NET_EVENT)
        {
            if (!print_event(client, &message))
            {
                return false;
            }
            continue;
        }
        if (message.reader_index != client->reader_index || message.day < client->last_day ||
            message.day > client->total_days)
        {
            fprintf(stderr, "Ошибка протокола: неожиданный день/роль\n");
            return false;
        }
        client->last_day = message.day;
        bool applied = false;
        // после переподключения день и прогресс задаёт сервер
        if (message.type == NET_WORKER_SYNC)
        {
            if (message.code < 0 || message.code > (NET_SYNC_ACTIVE | NET_SYNC_APPLIED | NET_SYNC_PHASE_DONE) ||
                message.book_id != 0 || message.value != 0 ||
                ((message.code & NET_SYNC_ACTIVE) && (message.code & NET_SYNC_PHASE_DONE)))
            {
                fprintf(stderr, "Ошибка протокола синхронизации\n");
                return false;
            }
            printf("Синхронизация: день %d; состояние %d\n", message.day, message.code);
            if (!(message.code & NET_SYNC_ACTIVE))
            {
                continue;
            }
            applied = (message.code & NET_SYNC_APPLIED) != 0;
        }
        else if (message.type !=
                 (client->reader_index == NET_LIBRARIAN ? NET_START_LIBRARIAN_DAY : NET_START_READER_DAY))
        {
            fprintf(stderr, "Ошибка протокола: неожиданная команда\n");
            return false;
        }
        if (message.day < 1)
        {
            fprintf(stderr, "Ошибка протокола: фаза до начала моделирования\n");
            return false;
        }
        printf("Начало фазы: день %d\n", message.day);
        // применённую операцию пропускаем, незавершённую фазу подтверждаем
        if (!perform_phase(client, message.day, applied))
        {
            return false;
        }
    }
    if (!client->finished)
    {
        fprintf(stderr, "Ошибка: сервер отключился до NET_FINISHED\n");
    }
    return client->finished;
}

int client_main(int argc, char *argv[], NetworkRole role)
{
    SOFT_ASSERT(role == NETWORK_READER || role == NETWORK_LIBRARIAN, "Недопустимая роль клиента", EXIT_FAILURE);
    setvbuf(stdout, NULL, _IOLBF, 0);
    NetworkOptions options;
    if (!network_parse_options(argc, argv, role, &options))
    {
        return EXIT_FAILURE;
    }
    if (options.show_help)
    {
        network_print_help(role);
        return EXIT_SUCCESS;
    }
    int fd = network_connect(options.host, options.port);
    if (fd < 0)
    {
        return EXIT_FAILURE;
    }
    printf("Подключение к %s:%u\n", options.host, (unsigned)options.port);
    // локально храним только расписание и состояние сетевого диалога
    ClientContext client = {.fd = fd, .reader_index = role == NETWORK_READER ? options.reader_index : NET_LIBRARIAN};
    bool success = register_client(&client) && (client.finished || client_run(&client));
    network_close(&client.fd);
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
