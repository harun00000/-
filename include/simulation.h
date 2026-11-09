#ifndef SIMULATION_H
#define SIMULATION_H

#include "book.h"
#include "reader.h"
#include "request.h"
#include "librarian.h"
#include "statistics.h"
#if !defined(LIBRARY_PROCESS_4_5) && !defined(LIBRARY_PROCESS_6) && !defined(LIBRARY_PROCESS_7_8) &&                   \
    !defined(LIBRARY_PROCESS_10)
#include <pthread.h>
#endif
#if !defined(LIBRARY_PROCESS_6) && !defined(LIBRARY_PROCESS_7_8) && !defined(LIBRARY_PROCESS_10)
#include <semaphore.h>
#endif

enum
{
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
typedef struct Simulation
{
    Statistics statistics;
    Librarian librarian;
    Book books[MAX_BOOKS];
    int book_count;

    Reader readers[MAX_READERS];
    int reader_count;

    Request requests[MAX_REQUESTS];
    int request_count;

    // индексы соответствуют массиву books
    int owner_ids[MAX_BOOKS];           // айди владельцев; -1 если владельца нет
    int reserved_reader_ids[MAX_BOOKS]; // читатели с резервом; -1 если резерва нет
    int reservation_days[MAX_BOOKS];    // дни создания соответствующих резервов
    int reading_days;
    IssueStrategy issue_strategy;
    RequestRule request_rule;

    int current_day;
    bool finished;
#if !defined(LIBRARY_PROCESS_4_5) && !defined(LIBRARY_PROCESS_6) && !defined(LIBRARY_PROCESS_7_8) &&                   \
    !defined(LIBRARY_PROCESS_10)
    pthread_mutex_t mutex;
#endif
#if !defined(LIBRARY_PROCESS_6) && !defined(LIBRARY_PROCESS_7_8) && !defined(LIBRARY_PROCESS_10)
    sem_t librarian_start;
    sem_t reader_start[MAX_READERS];
    sem_t phase_done;
#endif
} Simulation;

/** @brief Результат попытки получить желаемую книгу. */
typedef enum BookAttempt
{
    BOOK_RECEIVED,
    BOOK_WAITING,
    BOOK_REQUEST_NEEDED
} BookAttempt;

#ifdef LIBRARY_PROCESS_10
enum
{
    BOOK_NOT_WANTED = -1,
    BOOK_AVAILABLE,
    BOOK_UNAVAILABLE
};

/** @brief Отмечает приход читателя; вызывающий код проверяет расписание. */
void simulation_reader_begin(Simulation *simulation, int reader_index, int day);

/** @brief Выдаёт доступные резервы перед обработкой обычных желаний. */
void simulation_reader_take_reservations(Simulation *simulation, int reader_index, int day);

/** @brief Возвращает доступность желаемой книги без изменения состояния. */
int simulation_reader_book_status(const Simulation *simulation, int reader_index, int book_id);

/** @brief Выполняет одну попытку выдачи в рамках последовательного посещения. */
BookAttempt simulation_reader_try_book(
    Simulation *simulation, int reader_index, int book_id, int day, bool wait_for_books);

/** @brief Создаёт новую заявку после отказа в выдаче. */
void simulation_reader_request_book(Simulation *simulation, int reader_index, int book_id, int day);
#endif

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
 * @param request_rule правило формирования заявок.
 */
void simulation_initialization(Simulation *simulation, const Book books[], int book_count, const Reader readers[],
    int reader_count, int reading_days, const Librarian *librarian, IssueStrategy issue_strategy,
    RequestRule request_rule);

#if !defined(LIBRARY_PROCESS_4_5) && !defined(LIBRARY_PROCESS_6) && !defined(LIBRARY_PROCESS_7_8) &&                   \
    !defined(LIBRARY_PROCESS_10)
/**
 * @brief Работу библиотеки с первого дня по указанный день.
 *
 * @param simulation симуляция.
 * @param total_days количество дней работы.
 * @return true при успешном завершении, false при ошибке запуска или очистки.
 */
bool simulation_run(Simulation *simulation, int total_days);
#endif

/** @brief Проверяет состояние перед запуском потоков или процессов. */
bool simulation_validate(const Simulation *simulation, int total_days);

/** @brief Обрабатывает возвраты; вызывающий код защищает общее состояние. */
void simulation_process_returns(Simulation *simulation, int day);

/** @brief Обрабатывает посещение; вызывающий код защищает общее состояние. */
void simulation_process_reader(Simulation *simulation, int reader_index, int day);

/** @brief Выводит итог после завершения всех работников. */
void simulation_print_results(Simulation *simulation);

#endif
