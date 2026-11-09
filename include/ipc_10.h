#ifndef IPC_10_H
#define IPC_10_H

#include "simulation.h"
#include <mqueue.h>
#include <semaphore.h>

enum
{
    IPC_10_SESSION_MAX = 40,
    IPC_10_NAME_SIZE = 96,
    IPC_10_TIMEOUT_MS = 5000,
    IPC_10_PARTICIPANTS = MAX_READERS + 1,
    // индексы общих семафоров, далее располагаются стартовые семафоры Reader
    IPC_10_REGISTER = 0,
    IPC_10_READY,
    IPC_10_LIBRARIAN,
    IPC_10_READERS,
    IPC_10_MAX_SEMAPHORES = IPC_10_READERS + MAX_READERS,
    // индексы общих очередей, далее располагаются личные очереди Reader
    IPC_10_REQUESTS = 0,
    IPC_10_REGISTRATION,
    IPC_10_LIBRARIAN_QUEUE,
    IPC_10_READER_QUEUES,
    IPC_10_MAX_QUEUES = IPC_10_READER_QUEUES + MAX_READERS
};

/** @brief Команды и ответы протокола без адресов локальной памяти. */
typedef enum Ipc10Type
{
    IPC_10_REGISTER_PARTICIPANT,
    IPC_10_REGISTERED,
    IPC_10_START_DAY,
    IPC_10_PROCESS_RETURNS,
    IPC_10_BEGIN_VISIT,
    IPC_10_TRY_RESERVED,
    IPC_10_BOOK_STATUS,
    IPC_10_TRY_BOOK,
    IPC_10_CREATE_REQUEST,
    IPC_10_RESPONSE,
    IPC_10_PHASE_DONE,
    IPC_10_SHUTDOWN,
    IPC_10_EXITED,
    IPC_10_ERROR
} Ipc10Type;

/** @brief Неизменяемые данные читателя, передаваемые при регистрации. */
typedef struct ReaderPlan
{
    char name[READER_NAME_SIZE];
    int wanted_books[MAX_WANTED_BOOKS];
    int wanted_count;
    int visit_days[READER_VISIT_CAPACITY];
    int visit_count;
    RequestRule request_rule;
} ReaderPlan;

/** @brief Фиксированное сообщение POSIX message queue. */
typedef struct Ipc10Message
{
    int version;
    Ipc10Type type;
    // контекст команды, -1 обозначает Librarian
    int reader_index;
    int day;
    int book_id;
    int result;
    // план используется только в подтверждении регистрации
    ReaderPlan plan;
    char text[96];
} Ipc10Message;

/** @brief Дескрипторы процесса и признаки владения созданными именами. */
typedef struct Ipc10Resources
{
    char prefix[IPC_10_NAME_SIZE];
    mqd_t queues[IPC_10_MAX_QUEUES];
    sem_t *semaphores[IPC_10_MAX_SEMAPHORES];
    bool queue_created[IPC_10_MAX_QUEUES];
    bool semaphore_created[IPC_10_MAX_SEMAPHORES];
} Ipc10Resources;

typedef enum Ipc10Result
{
    IPC_10_OK,
    IPC_10_TIMEOUT,
    IPC_10_STOPPED,
    IPC_10_FAILED
} Ipc10Result;

/** @brief Проверяет пользовательское имя сессии. */
bool ipc_10_valid_session(const char *session);

/** @brief Подготавливает локальные дескрипторы и префикс имён. */
void ipc_10_init(Ipc10Resources *ipc, const char *session);

/** @brief Формирует имя очереди или семафора по внутреннему индексу. */
bool ipc_10_name(const Ipc10Resources *ipc, bool semaphore, int index, char name[IPC_10_NAME_SIZE]);

/** @brief Создаёт все IPC-объекты Coordinator, ready создаётся последним. */
bool ipc_10_create(Ipc10Resources *ipc, int reader_count);

/** @brief Открывает одну очередь или один семафор существующей сессии. */
bool ipc_10_open_queue(Ipc10Resources *ipc, int index);
bool ipc_10_open_semaphore(Ipc10Resources *ipc, int index);

/** @brief Выводит имена реально созданных IPC-объектов. */
void ipc_10_print(const Ipc10Resources *ipc);

/** @brief Закрывает дескрипторы и удаляет только имена, созданные этим процессом. */
bool ipc_10_close(Ipc10Resources *ipc);

/** @brief Ожидания имеют срок и могут прерываться локальным сигналом. */
Ipc10Result ipc_10_send(mqd_t queue, const Ipc10Message *message, bool interruptible);
Ipc10Result ipc_10_receive(mqd_t queue, Ipc10Message *message, bool interruptible, int timeout_ms);
Ipc10Result ipc_10_wait(sem_t *semaphore, bool interruptible, int timeout_ms);
bool ipc_10_post(sem_t *semaphore);

/**
 * @brief Создаёт сообщение текущей версии протокола.
 * @param reader_index Индекс из регистрации или -1 для Librarian; проверяется Coordinator.
 * @param day Неотрицательный день, 0 используется до начала моделирования.
 */
Ipc10Message ipc_10_message(Ipc10Type type, int reader_index, int day);

/** @brief Возвращает монотонное время в миллисекундах для срока ожидания. */
long long ipc_10_now(void);

#endif
