#ifndef NETWORK_H
#define NETWORK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum NetworkRole
{
    NETWORK_SERVER,
    NETWORK_LIBRARIAN,
    NETWORK_READER,
    NETWORK_OBSERVER
} NetworkRole;

typedef struct NetworkOptions
{
    const char *host;
    uint16_t port;
    const char *config_filename;
    int total_days;
    int reading_days;
    int reader_index;
    bool show_help;
} NetworkOptions;

/** @brief Разбирает CLI роли; внешние ошибки диагностируются без assert. */
bool network_parse_options(int argc, char *argv[], NetworkRole role, NetworkOptions *options);

/** @brief Выводит справку соответствующей программы. */
void network_print_help(NetworkRole role);

/** @brief Создаёт неблокирующий listening socket IPv4 или возвращает -1. */
int network_listen(const char *host, uint16_t port);

/** @brief Подключает блокирующий клиент к числовому адресу IPv4 или возвращает -1. */
int network_connect(const char *host, uint16_t port);

/** @brief Переводит существующий socket в неблокирующий режим. */
bool network_set_nonblocking(int fd);

/** @brief Отправляет все байты через блокирующий socket с обработкой EINTR и SIGPIPE. */
bool send_all(int fd, const void *buffer, size_t size);

/** @brief Получает все байты; EOF внутри сообщения диагностируется отдельно. */
bool recv_all(int fd, void *buffer, size_t size);

/** @brief Закрывает socket и устанавливает дескриптор в -1. */
void network_close(int *fd);

#endif
