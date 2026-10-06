#include "statistics.h"
#include "check.h"
#include "logger.h"
#include <stdio.h>

enum { STATISTICS_MESSAGE_SIZE = 567 };

void statistics_initialization(Statistics *statistics){
    SOFT_ASSERT_VOID(statistics != NULL, "Указатель на статистику = NULL");
    *statistics = (Statistics){0};
}

void statistics_print(const Statistics *statistics){
    SOFT_ASSERT_VOID(statistics != NULL, "Указатель на статистику = NULL");

    // Формируем один текст для вывода в терминал и журнал.
    char message[STATISTICS_MESSAGE_SIZE];
    int length = snprintf(message, sizeof message,
        "Статистика:\n"
        "Приходов читателей: %d\n"
        "Выдано книг: %d\n"
        "Возвращено книг: %d\n"
        "Создано заявок: %d\n"
        "Выполнено заявок: %d\n"
        "Отправлено уведомлений: %d\n",
        statistics->reader_visits, statistics->books_given, statistics->books_returned,
        statistics->requests_created, statistics->requests_completed, statistics->notifications_sent);

    if (length < 0 || (size_t)length >= sizeof message)
    {
        fprintf(stderr, "Не удалось подготовить итоговую статистику\n");
        return;
    }
    
    fputs(message, stdout);
    logger_write("%s", message);
}
