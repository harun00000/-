#include "network.h"
#include "protocol.h"
#include "check.h"
#include <stdio.h>
#include <stdlib.h>

static bool print_phase(const NetMessage *message)
{
    SOFT_ASSERT(message != NULL, "Нет события Observer", false);
    if (message->type != NET_OBSERVER_EVENT || message->code < EVENT_SIMULATION_START ||
        message->code > EVENT_SIMULATION_DONE || message->day < 0 || message->book_id != 0 || message->value != 0)
    {
        fprintf(stderr, "Ошибка протокола: неверное событие Observer\n");
        return false;
    }
    bool reader = message->code == EVENT_READER_START || message->code == EVENT_READER_DONE;
    bool librarian = message->code == EVENT_LIBRARIAN_START || message->code == EVENT_LIBRARIAN_DONE;
    if ((reader && message->reader_index < 0) || (librarian && message->reader_index != NET_LIBRARIAN) ||
        (!reader && !librarian && message->reader_index != NET_OBSERVER))
    {
        fprintf(stderr, "Ошибка протокола: роль события Observer\n");
        return false;
    }
    // границы фаз дополняют предметные события до общей картины моделирования
    const char *descriptions[] = {"моделирование начато", "день начат", "Librarian начал фазу",
        "Librarian завершил фазу", "Reader начал фазу", "Reader завершил фазу", "день завершён",
        "моделирование завершено"};
    printf("День %d: %s", message->day, descriptions[message->code - EVENT_SIMULATION_START]);
    if (reader)
    {
        printf("; Reader %d", message->reader_index);
    }
    printf("\n");
    return true;
}

static bool observer_receive(int fd, NetMessage *message, bool *finished)
{
    SOFT_ASSERT(message != NULL && finished != NULL, "Нет данных Observer receive", false);
    if (!protocol_receive_message(fd, message))
    {
        return false;
    }
    // штатное завершение допустимо даже вместо ответа регистрации
    if (protocol_is_finished(message) &&
        (message->reader_index == NET_OBSERVER || message->reader_index == NET_UNREGISTERED))
    {
        printf(message->code == NET_FINISH_NORMAL ? "Observer: нормальное завершение\n"
                                                  : "Observer: сервер завершает работу\n");
        *finished = true;
    }
    return true;
}

static bool observe(int fd)
{
    SOFT_ASSERT(fd >= 0, "Неверный внутренний socket Observer", false);
    bool finished = false;
    NetMessage message = {.type = NET_REGISTER_OBSERVER, .reader_index = NET_OBSERVER};
    if (!protocol_send_message(fd, &message) || !observer_receive(fd, &message, &finished))
    {
        return false;
    }
    if (finished)
    {
        return true;
    }
    if (message.type == NET_ERROR)
    {
        fprintf(stderr, "Ошибка регистрации Observer: код %d\n", message.code);
        return false;
    }
    if (message.type != NET_REGISTER_OK || message.reader_index != NET_OBSERVER || message.code != 0 ||
        message.value < 1 || message.day < 0 || message.day > message.value)
    {
        fprintf(stderr, "Ошибка протокола регистрации Observer\n");
        return false;
    }
    int total_days = message.value;
    int last_day = message.day;
    printf("Observer подключён; текущий день %d; события с момента подключения\n", last_day);
    // наблюдатель только читает поток и не подтверждает рабочие фазы
    while (observer_receive(fd, &message, &finished))
    {
        if (finished)
        {
            return true;
        }
        if (message.day < last_day || message.day > total_days)
        {
            fprintf(stderr, "Ошибка протокола: день Observer\n");
            return false;
        }
        last_day = message.day;
        if (message.type == NET_EVENT || (message.type == NET_OBSERVER_EVENT && message.code >= EVENT_ARRIVAL &&
                                             message.code <= EVENT_BOOK_RESERVED))
        {
            message.type = NET_EVENT;
            if (!protocol_print_event(&message))
            {
                return false;
            }
        }
        else if (!print_phase(&message))
        {
            return false;
        }
    }
    fprintf(stderr, "Observer: соединение закрыто до завершения моделирования\n");
    return false;
}

int main(int argc, char *argv[])
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    NetworkOptions options;
    if (!network_parse_options(argc, argv, NETWORK_OBSERVER, &options))
    {
        return EXIT_FAILURE;
    }
    if (options.show_help)
    {
        network_print_help(NETWORK_OBSERVER);
        return EXIT_SUCCESS;
    }
    int fd = network_connect(options.host, options.port);
    if (fd < 0)
    {
        return EXIT_FAILURE;
    }
    bool success = observe(fd);
    network_close(&fd);
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
