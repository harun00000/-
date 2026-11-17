#define _POSIX_C_SOURCE 200809L
#include "network.h"
#include "check.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

static bool parse_number(const char *text, int minimum, int maximum, int *value)
{
    SOFT_ASSERT(text != NULL && value != NULL, "Нет аргумента разбора числа", false);
    char *end = NULL;
    errno = 0;
    // отклоняем переполнение и посторонние символы после числа
    long parsed = strtol(text, &end, 10);
    if (end == text || *end != '\0' || errno == ERANGE || parsed < minimum || parsed > maximum)
    {
        fprintf(stderr, "Ошибка: ожидается целое число от %d до %d: %s\n", minimum, maximum, text);
        return false;
    }
    *value = (int)parsed;
    return true;
}

bool network_parse_options(int argc, char *argv[], NetworkRole role, NetworkOptions *options)
{
    SOFT_ASSERT(argc > 0 && argv != NULL && options != NULL, "Нет аргументов CLI", false);
    SOFT_ASSERT(role >= NETWORK_SERVER && role <= NETWORK_OBSERVER, "Неизвестная внутренняя роль", false);
    *options = (NetworkOptions){.reader_index = -1};
    const struct option long_options[] = {{"host", required_argument, NULL, 'H'},
        {"port", required_argument, NULL, 'p'}, {"reader", required_argument, NULL, 'i'},
        {"config", required_argument, NULL, 'c'}, {"days", required_argument, NULL, 'd'},
        {"reading-days", required_argument, NULL, 'r'}, {"help", no_argument, NULL, 'h'}, {NULL, 0, NULL, 0}};
    opterr = 0;
    optind = 1;
    int option;
    // набор допустимых параметров зависит от роли программы
    while ((option = getopt_long(argc, argv, ":c:d:r:h", long_options, NULL)) != -1)
    {
        int port = 0;
        switch (option)
        {
            case 'H':
                options->host = optarg;
                break;
            case 'p':
                if (!parse_number(optarg, 1, 65535, &port))
                {
                    return false;
                }
                options->port = (uint16_t)port;
                break;
            case 'i':
                if (role != NETWORK_READER || !parse_number(optarg, 0, INT_MAX, &options->reader_index))
                {
                    fprintf(stderr, "Ошибка параметра --reader\n");
                    return false;
                }
                break;
            case 'c':
                if (role != NETWORK_SERVER || optarg[0] == '\0')
                {
                    fprintf(stderr, "Ошибка параметра --config\n");
                    return false;
                }
                options->config_filename = optarg;
                break;
            case 'd':
            case 'r':
                if (role != NETWORK_SERVER ||
                    !parse_number(optarg, 1, INT_MAX, option == 'd' ? &options->total_days : &options->reading_days))
                {
                    fprintf(stderr, "Ошибка параметров моделирования\n");
                    return false;
                }
                break;
            case 'h':
                options->show_help = true;
                break;
            default:
                fprintf(stderr, "Ошибка CLI: неизвестная опция или отсутствует значение\n");
                return false;
        }
    }
    if (optind != argc)
    {
        fprintf(stderr, "Ошибка CLI: лишние аргументы\n");
        return false;
    }
    if (options->show_help)
    {
        return true;
    }
    if (options->host == NULL || options->port == 0 || (role == NETWORK_READER && options->reader_index < 0))
    {
        fprintf(stderr, "Ошибка: обязательны --host, --port и для Reader --reader\n");
        return false;
    }
    // адрес привязки ко всем интерфейсам допустим только для сервера
    struct in_addr address;
    if (inet_pton(AF_INET, options->host, &address) != 1 ||
        (role != NETWORK_SERVER && address.s_addr == htonl(INADDR_ANY)))
    {
        fprintf(stderr, "Ошибка IPv4: %s\n", options->host);
        return false;
    }
    return true;
}

void network_print_help(NetworkRole role)
{
    SOFT_ASSERT_VOID(role >= NETWORK_SERVER && role <= NETWORK_OBSERVER, "Неизвестная роль справки");
    const char *names[] = {
        "library_server", "library_librarian_client", "library_reader_client", "library_observer_client"};
    printf("Использование: %s --host IPv4 --port PORT", names[role]);
    if (role == NETWORK_SERVER)
    {
        printf(" [-c FILE] [-d DAYS] [-r READING_DAYS]");
    }
    if (role == NETWORK_READER)
    {
        printf(" --reader INDEX");
    }
    printf("\n-h, --help — справка\n");
    if (role == NETWORK_SERVER)
    {
        printf("-c, --config FILE; -d, --days N; -r, --reading-days N\nБез -c используются встроенные данные\n");
    }
}

bool network_set_nonblocking(int fd)
{
    SOFT_ASSERT(fd >= 0, "Внутренний дескриптор отрицательный", false);
    // сохраняем прежние флаги дескриптора при включении неблокирующего режима
    int flags = fcntl(fd, F_GETFL);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
    {
        perror("fcntl O_NONBLOCK");
        return false;
    }
    return true;
}

static int create_socket(const char *host, uint16_t port, bool server)
{
    SOFT_ASSERT(host != NULL && port > 0, "Нет внутреннего адреса", -1);
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_port = htons(port)};
    if (inet_pton(AF_INET, host, &address.sin_addr) != 1)
    {
        fprintf(stderr, "Ошибка IPv4: %s\n", host);
        return -1;
    }
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        perror("socket");
        return -1;
    }
    bool success;
    // сервер создаёт точку приёма, клиент подключается к указанному адресу
    if (server)
    {
        int enabled = 1;
        success = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof enabled) == 0;
        if (!success)
        {
            perror("setsockopt");
        }
        if (success && bind(fd, (struct sockaddr *)&address, sizeof address) != 0)
        {
            perror("bind");
            success = false;
        }
        if (success && listen(fd, 32) != 0)
        {
            perror("listen");
            success = false;
        }
        success = success && network_set_nonblocking(fd);
    }
    else
    {
        success = connect(fd, (struct sockaddr *)&address, sizeof address) == 0;
        if (!success)
        {
            perror("connect");
        }
    }
    if (!success)
    {
        network_close(&fd);
    }
    return fd;
}

int network_listen(const char *host, uint16_t port)
{
    return create_socket(host, port, true);
}

int network_connect(const char *host, uint16_t port)
{
    return create_socket(host, port, false);
}

bool send_all(int fd, const void *buffer, size_t size)
{
    SOFT_ASSERT(fd >= 0 && buffer != NULL, "Нет внутренних данных отправки", false);
    const unsigned char *bytes = buffer;
    size_t sent = 0;
    // частичная отправка продолжает тот же кадр, не создавая новое сообщение
    while (sent < size)
    {
        ssize_t count = send(fd, bytes + sent, size - sent, MSG_NOSIGNAL);
        if (count < 0 && errno == EINTR)
        {
            continue;
        }
        if (count <= 0)
        {
            if (count < 0)
            {
                perror("send");
            }
            return false;
        }
        sent += (size_t)count;
    }
    return true;
}

bool recv_all(int fd, void *buffer, size_t size)
{
    SOFT_ASSERT(fd >= 0 && buffer != NULL, "Нет внутренних данных получения", false);
    unsigned char *bytes = buffer;
    size_t received = 0;
    // TCP может разбить один кадр на несколько чтений
    while (received < size)
    {
        ssize_t count = recv(fd, bytes + received, size - received, 0);
        if (count < 0 && errno == EINTR)
        {
            continue;
        }
        if (count <= 0)
        {
            if (count < 0)
            {
                perror("recv");
            }
            else
            {
                // различаем закрытое соединение и оборванный посередине кадр
                fprintf(stderr, received == 0 ? "EOF: соединение закрыто\n" : "Ошибка протокола: неполное сообщение\n");
            }
            return false;
        }
        received += (size_t)count;
    }
    return true;
}

void network_close(int *fd)
{
    SOFT_ASSERT_VOID(fd != NULL, "Нет дескриптора для закрытия");
    if (*fd >= 0)
    {
        // shutdown может вернуть ENOTCONN для listening socket
        if (shutdown(*fd, SHUT_RDWR) != 0 && errno != ENOTCONN)
        {
            perror("shutdown");
        }
        if (close(*fd) != 0)
        {
            perror("close");
        }
        *fd = -1;
    }
}
