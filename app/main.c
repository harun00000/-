#include "simulation.h"
#ifdef LIBRARY_PROCESS_4_5
#include "process_4_5.h"
#elif defined(LIBRARY_PROCESS_6)
#include "process_6.h"
#endif
#include "main_utils.h"
#include "config.h"
#include "logger.h"
#include <stdlib.h>

enum
{
    READING_DAYS = 3
};

int main(int argc, char *argv[])
{
    // разбираем аргументы командной строки
    MainOptions options;
    if (!parse_main_options(argc, argv, &options))
    {
        return EXIT_FAILURE;
    }

    // выводим справку по запросу
    if (options.show_help)
    {
        print_help();
        return EXIT_SUCCESS;
    }

    if (!setup_signal_handlers())
    {
        return EXIT_FAILURE;
    }

    // стандартные данные для запуска без конфига
    const Librarian librarian = {.id = 1, .name = "Никита Пичурин"};
    const Book books[] = {{1, "Курочка ряба"}, {2, "Убийство в Реутовском экспрессе. 2026г."}};
    const Reader readers[] = {{.id = 1,
                                  .name = "Даниил Подлягин",
                                  .wanted_books = {1, 2},
                                  .wanted_count = 2,
                                  .visit_days = {1, 5},
                                  .visit_count = 2},
        {.id = 2,
            .name = "Никита Жилин",
            .wanted_books = {1},
            .wanted_count = 1,
            .visit_days = {2, 3, 6},
            .visit_count = 3},
        {.id = 3,
            .name = "Сергей Крылосов",
            .wanted_books = {1},
            .wanted_count = 1,
            .visit_days = {2, 4, 10},
            .visit_count = 3}};

    // сначала выбираем стандартные данные
    const Book *selected_books = books;
    const Reader *selected_readers = readers;
    int book_count = (int)(sizeof books / sizeof books[0]);
    int reader_count = (int)(sizeof readers / sizeof readers[0]);
    int reading_days = READING_DAYS;
    int total_days = DEFAULT_TOTAL_DAYS;
    IssueStrategy issue_strategy = ISSUE_FIFO;
    RequestRule request_rule = REQUEST_ON_UNAVAILABLE;
    Config config;

    // загружаем данные из указанного конфига
    if (options.config_filename != NULL)
    {
        if (!config_load(options.config_filename, &config))
        {
            return EXIT_FAILURE;
        }

        selected_books = config.books;
        selected_readers = config.readers;
        book_count = config.book_count;
        reader_count = config.reader_count;
        reading_days = config.reading_days;
        total_days = config.total_days;
        issue_strategy = config.issue_strategy;
        request_rule = config.request_rule;
    }

    // параметры командной строки имеют приоритет над конфигом
    if (options.has_total_days)
    {
        total_days = options.total_days;
    }

    if (options.has_reading_days)
    {
        reading_days = options.reading_days;
    }

    // создаем и запускаем симуляцию
    logger_open("library.log");
    Simulation simulation;
    simulation_initialization(&simulation, selected_books, book_count, selected_readers, reader_count, reading_days,
        &librarian, issue_strategy, request_rule);
#ifdef LIBRARY_PROCESS_4_5
    bool success = process_4_5_run(&simulation, total_days);
#elif defined(LIBRARY_PROCESS_6)
    bool success = process_6_run(&simulation, total_days);
#else
    bool success = simulation_run(&simulation, total_days);
#endif
    logger_close();
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
