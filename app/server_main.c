#include "server.h"
#include "config.h"
#include "main_utils.h"
#include "check.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

static bool load_config(NetworkOptions *options, Config *config)
{
    SOFT_ASSERT(options != NULL && config != NULL, "Нет данных конфигурации сервера", false);
    // те же встроенные данные, что у старого app/main.c
    *config = (Config){.books = {{1, "Курочка ряба"}, {2, "Убийство в Реутовском экспрессе. 2026г."}},
        .book_count = 2,
        .readers = {{.id = 1,
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
                .visit_count = 3}},
        .reader_count = 3,
        .reading_days = 3,
        .total_days = DEFAULT_TOTAL_DAYS,
        .issue_strategy = ISSUE_FIFO,
        .request_rule = REQUEST_ON_UNAVAILABLE};
    // без файла сохраняем прежние встроенные данные
    if (options->config_filename != NULL && !config_load(options->config_filename, config))
    {
        return false;
    }
    // явно заданные параметры командной строки перекрывают конфигурацию
    if (options->total_days > 0)
    {
        config->total_days = options->total_days;
    }
    if (options->reading_days > 0)
    {
        config->reading_days = options->reading_days;
    }
    options->total_days = config->total_days;
    // проверяем внешние сроки до передачи предметным функциям
    for (int idx = 0; idx < config->reader_count; ++idx)
    {
        const Reader *reader = &config->readers[idx];
        for (int jdx = 0; jdx < reader->visit_count; ++jdx)
        {
            if (reader->visit_days[jdx] <= config->total_days &&
                reader->visit_days[jdx] > INT_MAX - config->reading_days)
            {
                fprintf(stderr, "Ошибка параметров: переполнение дня возврата\n");
                return false;
            }
        }
    }
    return true;
}

int main(int argc, char *argv[])
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    NetworkOptions options;
    if (!network_parse_options(argc, argv, NETWORK_SERVER, &options))
    {
        return EXIT_FAILURE;
    }
    if (options.show_help)
    {
        network_print_help(NETWORK_SERVER);
        return EXIT_SUCCESS;
    }
    Config config;
    if (!load_config(&options, &config) || !setup_signal_handlers())
    {
        return EXIT_FAILURE;
    }
    const Librarian librarian = {.id = 1, .name = "Никита Пичурин"};
    // создаём единственное изменяемое состояние библиотеки
    Simulation simulation;
    simulation_initialization(&simulation, config.books, config.book_count, config.readers, config.reader_count,
        config.reading_days, &librarian, config.issue_strategy, config.request_rule);
    bool success = simulation_validate(&simulation, config.total_days) && server_run(&simulation, &options);
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
