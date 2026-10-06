#ifndef LOGGER_H
#define LOGGER_H

#include <stdbool.h>

/**
 * @brief Открывает новый файл с логами, стирая его прошлое.
 *
 * @param filename имя файла для логов.
 * @return true при успехе, false при ошибке открытия.
 */
bool logger_open(const char *filename);

/**
 * @brief Записывает сообщение в лог файл.
 *
 * @param format формат сообщения, как у printf.
 * @param ... значения для подстановки в сообщение.
 */
void logger_write(const char *format, ...);

/**
 * @brief Закрывает файл с логами.
 */
void logger_close(void);

#endif
