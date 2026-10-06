#ifndef CONFIG_H
#define CONFIG_H

#include "simulation.h"

/**
 * @brief Исходные данные симуляции из файла.
 */
typedef struct Config{
    Book books[MAX_BOOKS];
    int book_count;

    Reader readers[MAX_READERS];
    int reader_count;

    int reading_days;
    int total_days;
} Config;

/**
 * @brief Загружает данные симуляции из текстового файла.
 *
 * @param filename имя файла.
 * @param config данные для заполнения; при ошибке могут быть заполнены частично.
 * @return true при успешной загрузке, false при ошибке.
 */
bool config_load(const char *filename, Config *config);

#endif
