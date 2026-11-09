#define _POSIX_C_SOURCE 200809L
#include "process_10.h"
#include "ipc_10.h"
#include "check.h"
#include "main_utils.h"
#include <stdio.h>
#include <string.h>

typedef struct Participant10
{
    Ipc10Resources *ipc;
    int reader_index;
    int day;
    ReaderPlan plan;
    bool registered;
    bool shutdown;
} Participant10;

// полученный из очереди план ещё является внешними данными
static bool valid_plan(const ReaderPlan *plan)
{
    SOFT_ASSERT(plan != NULL, "Указатель на план = NULL", false);
    if (memchr(plan->name, '\0', sizeof plan->name) == NULL || plan->wanted_count < 1 ||
        plan->wanted_count > MAX_WANTED_BOOKS || plan->visit_count < 0 || plan->visit_count > READER_VISIT_CAPACITY ||
        (plan->request_rule != REQUEST_ON_UNAVAILABLE && plan->request_rule != REQUEST_ALWAYS_WAIT))
    {
        return false;
    }
    for (int idx = 0; idx < plan->wanted_count; ++idx)
    {
        if (plan->wanted_books[idx] <= 0)
        {
            return false;
        }
    }
    for (int idx = 0; idx < plan->visit_count; ++idx)
    {
        if (plan->visit_days[idx] < FIRST_SIMULATION_DAY)
        {
            return false;
        }
    }
    return true;
}

// register не даёт двум подключениям забрать общий ответ друг друга
static bool register_participant(Participant10 *participant)
{
    SOFT_ASSERT(participant != NULL && participant->ipc != NULL, "Нет участника или IPC", false);
    SOFT_ASSERT(!participant->registered && !participant->shutdown && participant->day == 0,
        "Повторная регистрация локального участника", false);
    Ipc10Resources *ipc = participant->ipc;
    if (!ipc_10_open_semaphore(ipc, IPC_10_READY) || !ipc_10_open_semaphore(ipc, IPC_10_REGISTER) ||
        !ipc_10_open_queue(ipc, IPC_10_REQUESTS) || !ipc_10_open_queue(ipc, IPC_10_REGISTRATION))
    {
        return false;
    }
    if (ipc_10_wait(ipc->semaphores[IPC_10_REGISTER], true, IPC_10_TIMEOUT_MS) != IPC_10_OK)
    {
        fprintf(stderr, "Не удалось дождаться регистрации\n");
        return false;
    }
    // один запрос регистрации получает ровно свой ответ, включая отказ дубликату
    Ipc10Message message = ipc_10_message(IPC_10_REGISTER_PARTICIPANT, participant->reader_index, 0);
    bool success = ipc_10_send(ipc->queues[IPC_10_REQUESTS], &message, false) == IPC_10_OK &&
                   ipc_10_receive(ipc->queues[IPC_10_REGISTRATION], &message, false, IPC_10_TIMEOUT_MS) == IPC_10_OK;
    if (success && message.type == IPC_10_ERROR)
    {
        fprintf(stderr, "Ошибка регистрации: %.*s\n", (int)sizeof message.text, message.text);
        success = false;
    }
    else if (success && (message.type != IPC_10_REGISTERED || message.reader_index != participant->reader_index ||
                            message.reader_index < -1 || message.reader_index >= MAX_READERS))
    {
        fprintf(stderr, "Некорректный ответ регистрации\n");
        success = false;
    }
    else if (success)
    {
        participant->registered = true;
        int slot = participant->reader_index + 1;
        if (participant->reader_index >= 0 && !valid_plan(&message.plan))
        {
            fprintf(stderr, "Некорректный план читателя в ответе Coordinator\n");
            success = false;
        }
        if (success)
        {
            participant->plan = message.plan;
            success = ipc_10_open_queue(ipc, IPC_10_LIBRARIAN_QUEUE + slot) &&
                      ipc_10_open_semaphore(ipc, IPC_10_LIBRARIAN + slot);
        }
        // подтверждаем получение ответа и при ошибке, затем сообщим о выходе
        if (!ipc_10_post(ipc->semaphores[IPC_10_READY]))
        {
            success = false;
        }
    }
    if (!ipc_10_post(ipc->semaphores[IPC_10_REGISTER]))
    {
        success = false;
    }
    if (success)
    {
        if (participant->reader_index == -1)
        {
            printf("Librarian подключён\n");
        }
        else
        {
            printf("Reader %d подключён\n", participant->reader_index);
        }
    }
    return success;
}

// одна команда ожидает один ответ либо SHUTDOWN в личной очереди
static bool request_operation(Participant10 *participant, Ipc10Type type, int book_id, int value, int *result)
{
    SOFT_ASSERT(participant != NULL && participant->ipc != NULL && participant->registered && result != NULL,
        "Нет участника или результата", false);
    SOFT_ASSERT(participant->reader_index >= -1 && participant->reader_index < MAX_READERS &&
                    participant->day >= FIRST_SIMULATION_DAY && !participant->shutdown,
        "Операция вне активной фазы участника", false);
    SOFT_ASSERT(participant->reader_index == -1
                    ? type == IPC_10_PROCESS_RETURNS
                    : (type == IPC_10_BEGIN_VISIT || type == IPC_10_TRY_RESERVED || type == IPC_10_BOOK_STATUS ||
                          type == IPC_10_TRY_BOOK || type == IPC_10_CREATE_REQUEST),
        "Операция не соответствует роли участника", false);
    SOFT_ASSERT((type == IPC_10_BOOK_STATUS || type == IPC_10_TRY_BOOK || type == IPC_10_CREATE_REQUEST) ? book_id > 0
                                                                                                         : book_id == 0,
        "Некорректный идентификатор книги во внутренней команде", false);
    int slot = participant->reader_index + 1;
    Ipc10Message message = ipc_10_message(type, participant->reader_index, participant->day);
    message.book_id = book_id;
    message.result = value;
    if (ipc_10_send(participant->ipc->queues[IPC_10_REQUESTS], &message, true) != IPC_10_OK)
    {
        return false;
    }
    if (ipc_10_receive(participant->ipc->queues[IPC_10_LIBRARIAN_QUEUE + slot], &message, true, IPC_10_TIMEOUT_MS) !=
        IPC_10_OK)
    {
        fprintf(stderr, "Не удалось получить ответ Coordinator\n");
        return false;
    }
    if (message.type == IPC_10_SHUTDOWN)
    {
        participant->shutdown = true;
        return false;
    }
    if (message.type != IPC_10_RESPONSE || message.reader_index != participant->reader_index ||
        message.day != participant->day)
    {
        fprintf(stderr, "Нарушен протокол ответа Coordinator\n");
        return false;
    }
    *result = message.result;
    return true;
}

// расписание и решение о заявке проверяет сам Reader, состояние меняет Coordinator
static bool reader_visit(Participant10 *participant)
{
    SOFT_ASSERT(participant != NULL && participant->reader_index >= 0 && participant->reader_index < MAX_READERS,
        "Некорректный внутренний индекс Reader", false);
    SOFT_ASSERT(participant->ipc != NULL && participant->registered && !participant->shutdown &&
                    participant->day >= FIRST_SIMULATION_DAY,
        "Посещение вне активной фазы Reader", false);
    SOFT_ASSERT(valid_plan(&participant->plan), "План зарегистрированного Reader повреждён", false);
    bool visiting = false;
    for (int idx = 0; idx < participant->plan.visit_count; ++idx)
    {
        visiting = visiting || participant->plan.visit_days[idx] == participant->day;
    }
    if (!visiting)
    {
        return true;
    }
    int result;
    if (!request_operation(participant, IPC_10_BEGIN_VISIT, 0, 0, &result) ||
        !request_operation(participant, IPC_10_TRY_RESERVED, 0, 0, &result))
    {
        return false;
    }
    // Reader сам проверяет весь список до решения об обычных выдачах
    bool wanted[MAX_WANTED_BOOKS] = {false};
    bool wait_for_books = false;
    for (int idx = 0; idx < participant->plan.wanted_count; ++idx)
    {
        if (!request_operation(participant, IPC_10_BOOK_STATUS, participant->plan.wanted_books[idx], 0, &result))
        {
            return false;
        }
        if (result != BOOK_NOT_WANTED && result != BOOK_AVAILABLE && result != BOOK_UNAVAILABLE)
        {
            fprintf(stderr, "Некорректный ответ о доступности книги\n");
            return false;
        }
        wanted[idx] = result != BOOK_NOT_WANTED;
        wait_for_books = wait_for_books || result == BOOK_UNAVAILABLE;
    }
    wait_for_books = wait_for_books && participant->plan.request_rule == REQUEST_ALWAYS_WAIT;
    for (int idx = 0; idx < participant->plan.wanted_count; ++idx)
    {
        if (!wanted[idx])
        {
            continue;
        }
        int book_id = participant->plan.wanted_books[idx];
        if (!request_operation(participant, IPC_10_TRY_BOOK, book_id, wait_for_books, &result))
        {
            return false;
        }
        if (result == BOOK_RECEIVED)
        {
            for (int next = idx + 1; next < participant->plan.wanted_count; ++next)
            {
                if (participant->plan.wanted_books[next] == book_id)
                {
                    wanted[next] = false;
                }
            }
        }
        if (result == BOOK_REQUEST_NEEDED)
        {
            if (!request_operation(participant, IPC_10_CREATE_REQUEST, book_id, 0, &result))
            {
                return false;
            }
        }
        else if (result != BOOK_RECEIVED && result != BOOK_WAITING)
        {
            fprintf(stderr, "Некорректный результат выдачи\n");
            return false;
        }
    }
    return true;
}

// стартовый семафор разрешает прочитать данные очередной фазы
static bool participant_days(Participant10 *participant)
{
    SOFT_ASSERT(participant != NULL && participant->registered, "Участник не зарегистрирован", false);
    SOFT_ASSERT(participant->ipc != NULL && participant->reader_index >= -1 &&
                    participant->reader_index < MAX_READERS && !participant->shutdown,
        "Некорректное состояние зарегистрированного участника", false);
    int slot = participant->reader_index + 1;
    while (true)
    {
        // короткий срок ожидания нужен для локального SIGINT/SIGTERM
        Ipc10Result wait = ipc_10_wait(participant->ipc->semaphores[IPC_10_LIBRARIAN + slot], true, 100);
        if (wait == IPC_10_TIMEOUT)
        {
            continue;
        }
        if (wait != IPC_10_OK)
        {
            return false;
        }
        Ipc10Message message;
        if (ipc_10_receive(participant->ipc->queues[IPC_10_LIBRARIAN_QUEUE + slot], &message, true,
                IPC_10_TIMEOUT_MS) != IPC_10_OK)
        {
            return false;
        }
        if (message.type == IPC_10_SHUTDOWN)
        {
            participant->shutdown = true;
            return true;
        }
        if (message.type != IPC_10_START_DAY || message.reader_index != participant->reader_index ||
            message.day <= participant->day)
        {
            fprintf(stderr, "Некорректное начало модельного дня\n");
            return false;
        }
        participant->day = message.day;
        int result;
        bool success = participant->reader_index == -1
                           ? request_operation(participant, IPC_10_PROCESS_RETURNS, 0, 0, &result)
                           : reader_visit(participant);
        if (!success)
        {
            return participant->shutdown;
        }
        message = ipc_10_message(IPC_10_PHASE_DONE, participant->reader_index, participant->day);
        if (ipc_10_send(participant->ipc->queues[IPC_10_REQUESTS], &message, true) != IPC_10_OK)
        {
            return false;
        }
    }
}

// подключение не передаёт участнику владение именами очередей и семафоров
bool process_10_participant(Ipc10Resources *ipc, int reader_index)
{
    SOFT_ASSERT(ipc != NULL, "Указатель на IPC = NULL", false);
    // пользовательский индекс проверяет Coordinator во время регистрации
    Participant10 participant = {.ipc = ipc, .reader_index = reader_index};
    bool success = register_participant(&participant);
    if (success)
    {
        success = participant_days(&participant);
    }
    if (participant.registered)
    {
        Ipc10Message message = ipc_10_message(IPC_10_EXITED, reader_index, participant.day);
        message.result = success ? 0 : 1;
        if (ipc_10_send(ipc->queues[IPC_10_REQUESTS], &message, false) != IPC_10_OK)
        {
            success = false;
        }
    }
    return success;
}
