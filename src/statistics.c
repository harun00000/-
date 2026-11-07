#include "statistics.h"
#include "check.h"
#include "logger.h"
#include <stdio.h>

enum
{
    STATISTICS_MESSAGE_SIZE = 567
};

void statistics_initialization(Statistics *statistics)
{
    SOFT_ASSERT_VOID(statistics != NULL, "Указатель на статистику = NULL");
    *statistics = (Statistics){0};
}

void statistics_print(const Statistics *statistics)
{
    SOFT_ASSERT_VOID(statistics != NULL, "Указатель на статистику = NULL");

    SOFT_ASSERT_VOID(statistics->reader_visits >= 0 && statistics->books_given >= 0 &&
                         statistics->books_returned >= 0 && statistics->requests_created >= 0 &&
                         statistics->requests_completed >= 0 && statistics->notifications_sent >= 0,
        "Счётчики статистики не должны быть отрицательными");
    SOFT_ASSERT_VOID(statistics->books_returned <= statistics->books_given, "Возвращено больше книг, чем выдано");
    SOFT_ASSERT_VOID(
        statistics->requests_completed <= statistics->requests_created, "Выполнено больше заявок, чем создано");

    // формируем общий текст для терминала и журнала
    char message[STATISTICS_MESSAGE_SIZE];
    int length = snprintf(message, sizeof message,
        "Статистика:\n"
        "Приходов читателей: %d\n"
        "Выдано книг: %d\n"
        "Возвращено книг: %d\n"
        "Создано заявок: %d\n"
        "Выполнено заявок: %d\n"
        "Отправлено уведомлений: %d\n",
        statistics->reader_visits, statistics->books_given, statistics->books_returned, statistics->requests_created,
        statistics->requests_completed, statistics->notifications_sent);

    if (length < 0 || (size_t)length >= sizeof message)
    {
        fprintf(stderr, "Не удалось подготовить итоговую статистику\n");
        return;
    }

    fputs(message, stdout);
    logger_write("%s", message);
}
