#ifndef SIMULATION_H
#define SIMULATION_H

#include "book.h"
#include "reader.h"
#include "request.h"
#include "librarian.h"
#include "statistics.h"

enum{
    MAX_BOOKS = 15,
    MAX_READERS = 20,
    MIN_WANTED_BOOKS = 1,
    MAX_WANTED_BOOKS = 3,

    NO_READER = -1,
    FIRST_SIMULATION_DAY = 1
};

/**
 * @brief Структура "Симуляция" - состояния библиотеки и срок чтения книг.
 */
typedef struct Simulation{
    Statistics statistics;
    Librarian librarian;
    Book books[MAX_BOOKS];
    int book_count;

    Reader readers[MAX_READERS];
    int reader_count;

    Request requests[MAX_REQUESTS];
    int request_count;

    // Индексы этих массивов должны совпадать с индексами в books
    int owner_ids[MAX_BOOKS];           // айди владельцев; -1 если владельца нет
    int reserved_reader_ids[MAX_BOOKS]; // читатели с резервом; -1 если резерва нет
    int reservation_days[MAX_BOOKS];    // дни создания соответствующих резервов
    int reading_days;
    IssueStrategy issue_strategy;
} Simulation;

/**
 * @brief Подготавливает симуляцию с заданными книгами и читателями.
 *
 * @param simulation симуляция.
 * @param books массив книг.
 * @param book_count количество книг.
 * @param readers массив читателей без книг на руках.
 * @param reader_count количество читателей.
 * @param reading_days положительное число дней чтения одной книги.
 * @param librarian библиотекарь.
 * @param issue_strategy стратегия выдачи возвращённых книг.
 */
void simulation_initialization(Simulation *simulation, const Book books[], int book_count, 
    const Reader readers[], int reader_count, int reading_days, const Librarian *librarian, IssueStrategy issue_strategy);

/**
 * @brief Работу библиотеки с первого дня по указанный день.
 *
 * @param simulation симуляция.
 * @param total_days количество дней работы.
 */
void simulation_run(Simulation *simulation, int total_days);

#endif
