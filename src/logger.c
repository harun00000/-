#include "logger.h"
#include "check.h"
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

enum
{
    LOGGER_CLOSED = -1,
    LOGGER_MESSAGE_SIZE = 1000
};

static int log_fd = LOGGER_CLOSED;

// открываем журнал с нужным режимом записи
static bool logger_open_file(const char *filename, int flags)
{
    SOFT_ASSERT(filename != NULL, "Ошибка логгера: имя файла = NULL", false);
    SOFT_ASSERT(flags == (O_WRONLY | O_CREAT | O_TRUNC) || flags == (O_WRONLY | O_CREAT | O_APPEND),
        "Недопустимый внутренний режим журнала", false);
    SOFT_ASSERT(log_fd == LOGGER_CLOSED || log_fd >= 0, "Некорректный дескриптор логгера", false);
    logger_close();

    int permissions = 0644;

    log_fd = open(filename, flags, permissions);

    if (log_fd == LOGGER_CLOSED)
    {
        perror("Не удалось открыть файл журнала");
        return false;
    }

    return true;
}

bool logger_open(const char *filename)
{
    return logger_open_file(filename, O_WRONLY | O_CREAT | O_TRUNC);
}

bool logger_open_append(const char *filename)
{
    return logger_open_file(filename, O_WRONLY | O_CREAT | O_APPEND);
}

void logger_write(const char *format, ...)
{
    SOFT_ASSERT_VOID(format != NULL, "Ошибка логгера: формат сообщения = NULL");

    if (log_fd == LOGGER_CLOSED)
    {
        return;
    }

    SOFT_ASSERT_VOID(log_fd >= 0, "Некорректный дескриптор логгера");

    char message[LOGGER_MESSAGE_SIZE];

    // формируем сообщение для журнала
    va_list args;
    va_start(args, format);
    int length = vsnprintf(message, sizeof message, format, args);
    va_end(args);

    if (length < 0)
    {
        fprintf(stderr, "Не удалось создать сообщение для лог-файла\n");
        return;
    }

    // обрезаем сообщение до размера буфера
    if (length >= LOGGER_MESSAGE_SIZE)
    {
        length = LOGGER_MESSAGE_SIZE - 1;
    }

    if (write(log_fd, message, (size_t)length) == -1)
    {
        perror("Не удалось записать сообщение в лог-файла");
    }
}

// закрываем файл журнала
void logger_close(void)
{
    if (log_fd == LOGGER_CLOSED)
    {
        return;
    }

    SOFT_ASSERT_VOID(log_fd >= 0, "Некорректный дескриптор логгера");

    if (close(log_fd) != 0)
    {
        perror("Не удалось закрыть лог-файл");
    }

    log_fd = LOGGER_CLOSED;
}
