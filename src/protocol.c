#include "protocol.h"
#include "network.h"
#include "check.h"
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>

bool protocol_encode(const NetMessage *message, unsigned char buffer[NET_MESSAGE_SIZE])
{
    SOFT_ASSERT(message != NULL && buffer != NULL, "Нет данных кодирования", false);
    // формируем сетевой кадр из полей, не копируя выравнивание C-структуры
    const uint32_t fields[] = {message->type, (uint32_t)message->reader_index, (uint32_t)message->day,
        (uint32_t)message->book_id, (uint32_t)message->code, (uint32_t)message->value};
    for (size_t idx = 0; idx < sizeof fields / sizeof fields[0]; ++idx)
    {
        uint32_t value = htonl(fields[idx]);
        memcpy(buffer + idx * sizeof value, &value, sizeof value);
    }
    return true;
}

static int32_t signed_field(uint32_t value)
{
    // декодируем отрицательные значения без непереносимого unsigned-to-signed cast
    return value <= INT32_MAX ? (int32_t)value : -1 - (int32_t)(UINT32_MAX - value);
}

bool protocol_decode(const unsigned char buffer[NET_MESSAGE_SIZE], NetMessage *message)
{
    SOFT_ASSERT(buffer != NULL && message != NULL, "Нет данных декодирования", false);
    // читаем числа через memcpy без требований к выравниванию входного буфера
    uint32_t fields[6];
    for (size_t idx = 0; idx < sizeof fields / sizeof fields[0]; ++idx)
    {
        uint32_t value;
        memcpy(&value, buffer + idx * sizeof value, sizeof value);
        fields[idx] = ntohl(value);
    }
    // неизвестный тип является внешней ошибкой протокола
    if (fields[0] < NET_REGISTER_LIBRARIAN || fields[0] > NET_WORKER_SYNC)
    {
        fprintf(stderr, "Ошибка протокола: неизвестный тип %u\n", fields[0]);
        return false;
    }
    *message = (NetMessage){.type = fields[0],
        .reader_index = signed_field(fields[1]),
        .day = signed_field(fields[2]),
        .book_id = signed_field(fields[3]),
        .code = signed_field(fields[4]),
        .value = signed_field(fields[5])};
    return true;
}

bool protocol_send_message(int fd, const NetMessage *message)
{
    unsigned char buffer[NET_MESSAGE_SIZE];
    return protocol_encode(message, buffer) && send_all(fd, buffer, sizeof buffer);
}

bool protocol_receive_message(int fd, NetMessage *message)
{
    unsigned char buffer[NET_MESSAGE_SIZE];
    return recv_all(fd, buffer, sizeof buffer) && protocol_decode(buffer, message);
}

bool protocol_print_event(const NetMessage *message)
{
    SOFT_ASSERT(message != NULL, "Нет события для вывода", false);
    if (message->type != NET_EVENT || message->code < EVENT_ARRIVAL || message->code > EVENT_BOOK_RESERVED ||
        message->reader_index < 0 || message->day < 1)
    {
        fprintf(stderr, "Ошибка протокола события\n");
        return false;
    }
    // события отображаются одинаково у рабочих клиентов и наблюдателей
    const char *description[] = {"", "приход читателя", "запрос книги", "получение книги", "книга недоступна",
        "создана заявка", "возврат книги", "уведомление о возврате", "книга зарезервирована, заявка закрыта"};
    const char *event_description = description[message->code];
    if (message->code == EVENT_BOOK_UNAVAILABLE && message->value == 1)
    {
        event_description = "выдача отложена по always_wait";
    }
    printf(
        "День %d; Reader %d; %s; книга %d", message->day, message->reader_index, event_description, message->book_id);
    if (message->code == EVENT_BOOK_GIVEN)
    {
        printf("; день возврата %d", message->value);
    }
    else if (message->code == EVENT_REQUEST_CREATED || message->code == EVENT_BOOK_RESERVED)
    {
        printf("; номер заявки %d", message->value);
    }
    else if (message->code == EVENT_BOOK_REQUEST && message->value == 1)
    {
        printf("; получение по собственному резерву");
    }
    printf("\n");
    return true;
}

bool protocol_is_finished(const NetMessage *message)
{
    SOFT_ASSERT(message != NULL, "Нет финального сообщения", false);
    return message->type == NET_FINISHED && message->day >= 0 && message->book_id == 0 && message->value == 0 &&
           (message->code == NET_FINISH_NORMAL || message->code == NET_FINISH_SHUTDOWN);
}
