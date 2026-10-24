#define _POSIX_C_SOURCE 200809L
#include "main_utils.h"
#include "check.h"
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

volatile sig_atomic_t stop_requested = 0;

static void handle_stop_signal(int signal_number){
    (void)signal_number;
    stop_requested = 1;
}

bool setup_signal_handlers(void){
    // обработчик только запрашивает остановку
    struct sigaction action = {0};
    action.sa_handler = handle_stop_signal;
    sigemptyset(&action.sa_mask);
    sigaddset(&action.sa_mask, SIGINT);
    sigaddset(&action.sa_mask, SIGTERM);
    if (sigaction(SIGINT, &action, NULL) != 0 || sigaction(SIGTERM, &action, NULL) != 0)
    {
        perror("sigaction");
        return false;
    }
    return true;
}

static bool parse_positive_number(const char *text, int *value){
    SOFT_ASSERT(text != NULL, "Указатель на строку числа = NULL", false);
    SOFT_ASSERT(value != NULL, "Указатель на результат разбора числа = NULL", false);

    char *end;
    errno = 0;
    long number = strtol(text, &end, 10);

    // проверяем, что строка полностью является корректным положительным числом
    if (end == text || *end != '\0' || errno == ERANGE || number <= 0 || number > INT_MAX)
    {
        return false;
    }
    *value = (int)number;
    return true;
}

bool parse_main_options(int argc, char *argv[], MainOptions *options){
    SOFT_ASSERT(argc >= 1, "Количество аргументов должно быть не меньше 1", false);
    SOFT_ASSERT(argv != NULL, "Указатель на аргументы = NULL", false);
    SOFT_ASSERT(options != NULL, "Указатель на параметры запуска = NULL", false);

    *options = (MainOptions){0};
    static const struct option long_options[] = {      // описываем длинные версии аргументов командной строки
        {"days", required_argument, NULL, 'd'},
        {"reading-days", required_argument, NULL, 'r'},
        {"config", required_argument, NULL, 'c'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    opterr = 0;
    optind = 1;
    int option = 0;

    // по очереди обрабатываем все переданные аргументы
    while ((option = getopt_long(argc, argv, ":d:r:c:h", long_options, NULL)) != -1)
    {
        switch (option)
        {
            case 'd':
                if (!parse_positive_number(optarg, &options->total_days))
                {
                    fprintf(stderr, "Ошибка: --days требует целое число от 1 до %d\n", INT_MAX);
                    return false;
                }
                options->has_total_days = true;
                break;

            case 'r':
                if (!parse_positive_number(optarg, &options->reading_days))
                {
                    fprintf(stderr, "Ошибка: --reading-days требует целое число от 1 до %d\n", INT_MAX);
                    return false;
                }
                options->has_reading_days = true;
                break;

            case 'c':
                if (optarg[0] == '\0')
                {
                    fprintf(stderr, "Ошибка: --config требует имя файла\n");
                    return false;
                }
                options->config_filename = optarg;
                break;

            case 'h':
                options->show_help = true;
                break;

            case ':':
                fprintf(stderr, "Ошибка: для опции %s требуется значение\n", argv[optind - 1]);
                return false;

            default:
                fprintf(stderr, "Ошибка: неизвестная или неверно заданная опция %s\n", argv[optind - 1]);
                return false;
        }
    }

    // после обработки известных чекаем лишние аргументов (если такие есть)
    if (optind < argc)
    {
        fprintf(stderr, "Ошибка: лишний аргумент %s\n", argv[optind]);
        return false;
    }
    return true;
}

void print_help(void){
    printf("Использование:\n"
        "  ./build/library_sim [опции]\n"
        "\n"
        "Опции:\n"
        "  -d, --days N          количество дней моделирования\n"
        "  -r, --reading-days N  срок чтения книги\n"
        "  -c, --config FILE     загрузить данные из файла\n"
        "  -h, --help            показать справку\n");
}
