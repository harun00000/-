#define _POSIX_C_SOURCE 200809L
#include "ipc_10.h"
#include "check.h"
#include "main_utils.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

_Static_assert(sizeof(Ipc10Message) <= 512, "Сообщения IPC должны оставаться небольшими");

// проверяем локальный учёт ресурсов до вывода имён или очистки
static bool resources_valid(const Ipc10Resources *ipc)
{
    SOFT_ASSERT(ipc != NULL, "Указатель на ресурсы = NULL", false);
    SOFT_ASSERT(memchr(ipc->prefix, '\0', sizeof ipc->prefix) != NULL && ipc->prefix[0] == '/' &&
                    ipc->prefix[1] != '\0' && strchr(ipc->prefix + 1, '/') == NULL,
        "Некорректный внутренний префикс IPC", false);
    for (int idx = 0; idx < IPC_10_MAX_QUEUES; ++idx)
    {
        SOFT_ASSERT(!ipc->queue_created[idx] || ipc->queues[idx] != (mqd_t)-1,
            "Созданная очередь не имеет локального дескриптора", false);
    }
    for (int idx = 0; idx < IPC_10_MAX_SEMAPHORES; ++idx)
    {
        SOFT_ASSERT(!ipc->semaphore_created[idx] || ipc->semaphores[idx] != SEM_FAILED,
            "Созданный семафор не имеет локального дескриптора", false);
    }
    return true;
}

// ASCII-имя одинаково интерпретируется всеми независимо запущенными программами
bool ipc_10_valid_session(const char *session)
{
    SOFT_ASSERT(session != NULL, "Имя сессии = NULL", false);
    size_t length = strlen(session);
    if (length == 0 || length > IPC_10_SESSION_MAX)
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

// пустые дескрипторы позволяют безопасно закрыть и частично открытый набор
void ipc_10_init(Ipc10Resources *ipc, const char *session)
{
    SOFT_ASSERT_VOID(ipc != NULL && session != NULL, "Нет ресурсов или имени сессии");
    SOFT_ASSERT_VOID(ipc_10_valid_session(session), "Внутреннее имя сессии не прошло проверку");
    *ipc = (Ipc10Resources){0};
    for (int idx = 0; idx < IPC_10_MAX_QUEUES; ++idx)
    {
        ipc->queues[idx] = (mqd_t)-1;
    }
    for (int idx = 0; idx < IPC_10_MAX_SEMAPHORES; ++idx)
    {
        ipc->semaphores[idx] = SEM_FAILED;
    }
    int length = snprintf(ipc->prefix, sizeof ipc->prefix, "/library_10_%s", session);
    SOFT_ASSERT_VOID(length > 0 && (size_t)length < sizeof ipc->prefix, "Префикс IPC не помещается");
}

// общие объекты идут первыми, далее имя дополняется индексом Reader
bool ipc_10_name(const Ipc10Resources *ipc, bool semaphore, int index, char name[IPC_10_NAME_SIZE])
{
    SOFT_ASSERT(ipc != NULL && name != NULL, "Нет ресурсов или буфера имени", false);
    SOFT_ASSERT(memchr(ipc->prefix, '\0', sizeof ipc->prefix) != NULL && ipc->prefix[0] == '/' &&
                    ipc->prefix[1] != '\0' && strchr(ipc->prefix + 1, '/') == NULL,
        "Некорректный внутренний префикс IPC", false);
    SOFT_ASSERT(index >= 0 && index < (semaphore ? IPC_10_MAX_SEMAPHORES : IPC_10_MAX_QUEUES),
        "Индекс IPC-объекта вне массива", false);
    const char *queue_roles[] = {"requests", "registration", "librarian"};
    const char *semaphore_roles[] = {"register", "ready", "librarian_start"};
    int length;
    if (index < IPC_10_READERS)
    {
        length = snprintf(
            name, IPC_10_NAME_SIZE, "%s_%s", ipc->prefix, semaphore ? semaphore_roles[index] : queue_roles[index]);
    }
    else
    {
        length = snprintf(name, IPC_10_NAME_SIZE, semaphore ? "%s_reader_%d_start" : "%s_reader_%d", ipc->prefix,
            index - IPC_10_READERS);
    }
    SOFT_ASSERT(length > 0 && length < IPC_10_NAME_SIZE, "Имя IPC-объекта не помещается", false);
    return true;
}

// размер сообщения проверяется и при подключении к существующей очереди
static bool open_queue(Ipc10Resources *ipc, int index, bool create)
{
    SOFT_ASSERT(ipc != NULL && index >= 0 && index < IPC_10_MAX_QUEUES, "Некорректный индекс очереди", false);
    SOFT_ASSERT(ipc->queues[index] == (mqd_t)-1, "Повторное открытие очереди", false);
    char name[IPC_10_NAME_SIZE];
    if (!ipc_10_name(ipc, false, index, name))
    {
        return false;
    }
    struct mq_attr attributes = {.mq_maxmsg = 4, .mq_msgsize = sizeof(Ipc10Message)};
    mqd_t queue = create ? mq_open(name, O_CREAT | O_EXCL | O_RDWR, 0600, &attributes) : mq_open(name, O_RDWR);
    if (queue == (mqd_t)-1)
    {
        fprintf(stderr, "Не удалось %s очередь %s: %s\n", create ? "создать" : "открыть", name, strerror(errno));
        return false;
    }
    ipc->queues[index] = queue;
    ipc->queue_created[index] = create;
    struct mq_attr actual;
    if (mq_getattr(queue, &actual) != 0)
    {
        perror("mq_getattr");
        return false;
    }
    if (actual.mq_msgsize != (long)sizeof(Ipc10Message))
    {
        fprintf(stderr, "Несовместимый размер сообщения в сессии\n");
        return false;
    }
    return true;
}

// register начинается с одного разрешения, события фаз начинаются с нуля
static bool open_semaphore(Ipc10Resources *ipc, int index, bool create)
{
    SOFT_ASSERT(ipc != NULL && index >= 0 && index < IPC_10_MAX_SEMAPHORES, "Некорректный индекс семафора", false);
    SOFT_ASSERT(ipc->semaphores[index] == SEM_FAILED, "Повторное открытие семафора", false);
    char name[IPC_10_NAME_SIZE];
    if (!ipc_10_name(ipc, true, index, name))
    {
        return false;
    }
    sem_t *semaphore =
        create ? sem_open(name, O_CREAT | O_EXCL, 0600, index == IPC_10_REGISTER ? 1 : 0) : sem_open(name, 0);
    if (semaphore == SEM_FAILED)
    {
        fprintf(stderr, "Не удалось %s семафор %s: %s\n", create ? "создать" : "открыть", name, strerror(errno));
        return false;
    }
    ipc->semaphores[index] = semaphore;
    ipc->semaphore_created[index] = create;
    return true;
}

bool ipc_10_open_queue(Ipc10Resources *ipc, int index)
{
    return open_queue(ipc, index, false);
}

bool ipc_10_open_semaphore(Ipc10Resources *ipc, int index)
{
    return open_semaphore(ipc, index, false);
}

// создаём только новый набор, чужие имена не открываем вместо своих
bool ipc_10_create(Ipc10Resources *ipc, int reader_count)
{
    SOFT_ASSERT(ipc != NULL, "Указатель на ресурсы = NULL", false);
    if (!resources_valid(ipc))
    {
        return false;
    }
    for (int idx = 0; idx < IPC_10_MAX_QUEUES; ++idx)
    {
        SOFT_ASSERT(ipc->queues[idx] == (mqd_t)-1, "Очереди уже открыты перед созданием сессии", false);
    }
    for (int idx = 0; idx < IPC_10_MAX_SEMAPHORES; ++idx)
    {
        SOFT_ASSERT(ipc->semaphores[idx] == SEM_FAILED, "Семафоры уже открыты перед созданием сессии", false);
    }
    SOFT_ASSERT(reader_count > 0 && reader_count <= MAX_READERS, "Количество читателей вне массива", false);
    for (int idx = 0; idx < IPC_10_READER_QUEUES + reader_count; ++idx)
    {
        if (!open_queue(ipc, idx, true))
        {
            return false;
        }
    }
    for (int idx = 0; idx < IPC_10_READERS + reader_count; ++idx)
    {
        if (idx != IPC_10_READY && !open_semaphore(ipc, idx, true))
        {
            return false;
        }
    }
    return open_semaphore(ipc, IPC_10_READY, true);
}

// диагностика показывает только объекты, которые создал этот Coordinator
void ipc_10_print(const Ipc10Resources *ipc)
{
    SOFT_ASSERT_VOID(ipc != NULL, "Указатель на ресурсы = NULL");
    if (!resources_valid(ipc))
    {
        return;
    }
    for (int kind = 0; kind < 2; ++kind)
    {
        int count = kind ? IPC_10_MAX_SEMAPHORES : IPC_10_MAX_QUEUES;
        for (int idx = 0; idx < count; ++idx)
        {
            if (kind ? ipc->semaphore_created[idx] : ipc->queue_created[idx])
            {
                char name[IPC_10_NAME_SIZE];
                if (ipc_10_name(ipc, kind != 0, idx, name))
                {
                    printf("IPC %s: %s\n", kind ? "semaphore" : "queue", name);
                }
            }
        }
    }
}

// признаки created сохраняют границу между закрытием дескриптора и удалением имени
bool ipc_10_close(Ipc10Resources *ipc)
{
    SOFT_ASSERT(ipc != NULL, "Указатель на ресурсы = NULL", false);
    if (!resources_valid(ipc))
    {
        return false;
    }
    bool success = true;
    for (int idx = IPC_10_MAX_QUEUES - 1; idx >= 0; --idx)
    {
        if (ipc->queues[idx] != (mqd_t)-1 && mq_close(ipc->queues[idx]) != 0)
        {
            perror("mq_close");
            success = false;
        }
        ipc->queues[idx] = (mqd_t)-1;
        if (ipc->queue_created[idx])
        {
            char name[IPC_10_NAME_SIZE];
            if (!ipc_10_name(ipc, false, idx, name) || mq_unlink(name) != 0)
            {
                perror("mq_unlink");
                success = false;
            }
            ipc->queue_created[idx] = false;
        }
    }
    for (int idx = IPC_10_MAX_SEMAPHORES - 1; idx >= 0; --idx)
    {
        if (ipc->semaphores[idx] != SEM_FAILED && sem_close(ipc->semaphores[idx]) != 0)
        {
            perror("sem_close");
            success = false;
        }
        ipc->semaphores[idx] = SEM_FAILED;
        if (ipc->semaphore_created[idx])
        {
            char name[IPC_10_NAME_SIZE];
            if (!ipc_10_name(ipc, true, idx, name) || sem_unlink(name) != 0)
            {
                perror("sem_unlink");
                success = false;
            }
            ipc->semaphore_created[idx] = false;
        }
    }
    return success;
}

// абсолютный срок не продлевается при повторном ожидании после EINTR
static bool deadline_after(int timeout_ms, struct timespec *deadline)
{
    SOFT_ASSERT(timeout_ms > 0 && deadline != NULL, "Некорректный срок ожидания", false);
    if (clock_gettime(CLOCK_REALTIME, deadline) != 0)
    {
        perror("clock_gettime");
        return false;
    }
    deadline->tv_sec += timeout_ms / 1000;
    deadline->tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
    if (deadline->tv_nsec >= 1000000000L)
    {
        ++deadline->tv_sec;
        deadline->tv_nsec -= 1000000000L;
    }
    return true;
}

// монотонные часы используются для общего срока фазы и демонстрационной паузы
long long ipc_10_now(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
    {
        perror("clock_gettime");
        return -1;
    }
    return (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

// очередь может заполниться, поэтому отправка имеет ограниченный срок
Ipc10Result ipc_10_send(mqd_t queue, const Ipc10Message *message, bool interruptible)
{
    SOFT_ASSERT(queue != (mqd_t)-1 && message != NULL, "Нет очереди или сообщения", IPC_10_FAILED);
    SOFT_ASSERT(message->version == 1 && message->day >= 0, "Исходящее сообщение имеет неверную версию или день",
        IPC_10_FAILED);
    SOFT_ASSERT(message->type >= IPC_10_REGISTER_PARTICIPANT && message->type <= IPC_10_ERROR,
        "Некорректный тип исходящего сообщения", IPC_10_FAILED);
    struct timespec deadline;
    if (!deadline_after(IPC_10_TIMEOUT_MS, &deadline))
    {
        return IPC_10_FAILED;
    }
    while (true)
    {
        if (interruptible && stop_requested)
        {
            return IPC_10_STOPPED;
        }
        if (mq_timedsend(queue, (const char *)message, sizeof *message, 0, &deadline) == 0)
        {
            return IPC_10_OK;
        }
        if (interruptible && stop_requested)
        {
            return IPC_10_STOPPED;
        }
        if (errno == ETIMEDOUT)
        {
            fprintf(stderr, "Истёк срок отправки сообщения\n");
            return IPC_10_FAILED;
        }
        if (errno != EINTR)
        {
            perror("mq_timedsend");
            return IPC_10_FAILED;
        }
    }
}

// размер, версия и тип проверяются до обработки содержимого сообщения
Ipc10Result ipc_10_receive(mqd_t queue, Ipc10Message *message, bool interruptible, int timeout_ms)
{
    SOFT_ASSERT(queue != (mqd_t)-1 && message != NULL, "Нет очереди или буфера сообщения", IPC_10_FAILED);
    struct timespec deadline;
    if (!deadline_after(timeout_ms, &deadline))
    {
        return IPC_10_FAILED;
    }
    while (true)
    {
        if (interruptible && stop_requested)
        {
            return IPC_10_STOPPED;
        }
        ssize_t size = mq_timedreceive(queue, (char *)message, sizeof *message, NULL, &deadline);
        if (size >= 0)
        {
            if (size != (ssize_t)sizeof *message || message->version != 1 ||
                message->type < IPC_10_REGISTER_PARTICIPANT || message->type > IPC_10_ERROR)
            {
                fprintf(stderr, "Некорректное сообщение IPC или несовместимая версия протокола\n");
                return IPC_10_FAILED;
            }
            return IPC_10_OK;
        }
        if (interruptible && stop_requested)
        {
            return IPC_10_STOPPED;
        }
        if (errno == ETIMEDOUT)
        {
            return IPC_10_TIMEOUT;
        }
        if (errno != EINTR)
        {
            perror("mq_timedreceive");
            return IPC_10_FAILED;
        }
    }
}

// сигнал прерывает обычное ожидание, cleanup может игнорировать локальный флаг
Ipc10Result ipc_10_wait(sem_t *semaphore, bool interruptible, int timeout_ms)
{
    SOFT_ASSERT(semaphore != SEM_FAILED && semaphore != NULL, "Семафор не открыт", IPC_10_FAILED);
    struct timespec deadline;
    if (!deadline_after(timeout_ms, &deadline))
    {
        return IPC_10_FAILED;
    }
    while (true)
    {
        if (interruptible && stop_requested)
        {
            return IPC_10_STOPPED;
        }
        if (sem_timedwait(semaphore, &deadline) == 0)
        {
            return IPC_10_OK;
        }
        if (interruptible && stop_requested)
        {
            return IPC_10_STOPPED;
        }
        if (errno == ETIMEDOUT)
        {
            return IPC_10_TIMEOUT;
        }
        if (errno != EINTR)
        {
            perror("sem_timedwait");
            return IPC_10_FAILED;
        }
    }
}

bool ipc_10_post(sem_t *semaphore)
{
    SOFT_ASSERT(semaphore != SEM_FAILED && semaphore != NULL, "Семафор не открыт", false);
    if (sem_post(semaphore) != 0)
    {
        perror("sem_post");
        return false;
    }
    return true;
}

// нулевые поля не содержат неинициализированных данных при отправке всей структуры
Ipc10Message ipc_10_message(Ipc10Type type, int reader_index, int day)
{
    Ipc10Message message = {.version = 1, .type = IPC_10_ERROR};
    SOFT_ASSERT(
        type >= IPC_10_REGISTER_PARTICIPANT && type <= IPC_10_ERROR, "Неизвестный тип создаваемого сообщения", message);
    SOFT_ASSERT(day >= 0, "Отрицательный день создаваемого сообщения", message);
    message.type = type;
    message.reader_index = reader_index;
    message.day = day;
    return message;
}
