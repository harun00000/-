#ifndef MAIN_UTILS_H
#define MAIN_UTILS_H

#include <stdbool.h>

/**
 * @brief Структура опций для ввода через консоль.
 */
typedef struct MainOptions{
    const char *config_filename;

    int total_days;
    int reading_days;

    bool has_total_days;
    bool has_reading_days;
    bool show_help;
} MainOptions;

/**
 * @brief Разбирает аргументы командной строки.
 *
 * @param argc количество аргументов.
 * @param argv аргументы командной строки.
 * @param options параметры для заполнения.
 * @return true при успехе, false при ошибке аргументов.
 */
bool parse_main_options(int argc, char *argv[], MainOptions *options);

/**
 * @brief Выводит справку по запуску программы.
 */
void print_help(void);

#endif
