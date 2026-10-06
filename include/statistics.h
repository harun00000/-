#ifndef STATISTICS_H
#define STATISTICS_H

/**
 * @brief Счётчики событий симуляции.
 */
typedef struct Statistics{
    int reader_visits;
    int books_given;
    int books_returned;
    int requests_created;
    int requests_completed;
    int notifications_sent;
} Statistics;

/**
 * @brief Обнуляет счётчики статистики.
 *
 * @param statistics статистика.
 */
void statistics_initialization(Statistics *statistics);

/**
 * @brief Выводит итоговую статистику в терминал и лог-файл.
 *
 * @param statistics статистика симуляции.
 */
void statistics_print(const Statistics *statistics);

#endif
