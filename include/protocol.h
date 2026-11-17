#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "simulation_events.h"
#include <stdbool.h>
#include <stdint.h>

enum
{
    NET_MESSAGE_SIZE = 24,
    NET_LIBRARIAN = -1,
    NET_UNREGISTERED = -2,
    NET_OBSERVER = -3
};

typedef enum NetMessageType
{
    NET_REGISTER_LIBRARIAN = 1,
    NET_REGISTER_READER,
    NET_REGISTER_OK,
    NET_ERROR,
    NET_VISIT_DAY,
    NET_START_LIBRARIAN_DAY,
    NET_PROCESS_RETURNS,
    NET_START_READER_DAY,
    NET_READER_VISIT,
    NET_EVENT,
    NET_OPERATION_DONE,
    NET_PHASE_DONE,
    NET_FINISHED,
    NET_REGISTER_OBSERVER,
    NET_OBSERVER_EVENT,
    NET_WORKER_SYNC
} NetMessageType;

enum
{
    // прогресс текущей фазы передаётся в поле code сообщения NET_WORKER_SYNC
    NET_SYNC_ACTIVE = 1,
    NET_SYNC_APPLIED = 2,
    NET_SYNC_PHASE_DONE = 4,
    // причина завершения передаётся в поле code сообщения NET_FINISHED
    NET_FINISH_NORMAL = 0,
    NET_FINISH_SHUTDOWN = 1
};

typedef struct NetMessage
{
    uint32_t type;
    int32_t reader_index;
    int32_t day;
    int32_t book_id;
    int32_t code;
    int32_t value;
} NetMessage;

/** @brief Кодирует ровно 24 байта без пересылки padding и указателей. */
bool protocol_encode(const NetMessage *message, unsigned char buffer[NET_MESSAGE_SIZE]);

/** @brief Декодирует сообщение и проверяет известный тип. */
bool protocol_decode(const unsigned char buffer[NET_MESSAGE_SIZE], NetMessage *message);

/** @brief Отправляет сообщение по блокирующему сокету клиента. */
bool protocol_send_message(int fd, const NetMessage *message);

/** @brief Получает сообщение по блокирующему сокету; EOF возвращает false. */
bool protocol_receive_message(int fd, NetMessage *message);

/** @brief Печатает предметное событие в собственный вывод клиента. */
bool protocol_print_event(const NetMessage *message);

/** @brief Проверяет финальное сообщение в любом состоянии клиентского протокола. */
bool protocol_is_finished(const NetMessage *message);

#endif
