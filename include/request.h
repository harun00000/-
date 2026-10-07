#ifndef REQUEST_H
#define REQUEST_H

#include <stdbool.h>

enum { REQUEST_INDEX_NOT_FOUND = -1, MAX_REQUESTS = 300 };

typedef enum IssueStrategy{
    ISSUE_FIFO,
    ISSUE_LIFO
} IssueStrategy;

typedef enum RequestRule{
    REQUEST_ON_UNAVAILABLE,
    REQUEST_ALWAYS_WAIT
} RequestRule;

/**
 * @brief Заявка читателя на получение книги.
 */
typedef struct Request{
    int book_id;
    int reader_id;
    int request_day;
    bool is_active;
} Request;

/**
 * @brief Проверяет, есть ли активная заявка читателя на книгу.
 *
 * @param requests массив заявок.
 * @param count количество заявок в массиве.
 * @param book_id айди книги.
 * @param reader_id айди читателя.
 * @return true, если активная заявка есть; false, если нет.
 */
bool request_exists(const Request requests[], int count, int book_id, int reader_id);
/**
 * @brief Создаёт заявку читателя на книгу.
 *
 * @param requests массив заявок.
 * @param count указатель на число заявок, при успехе увеличивается на один.
 * @param book_id айди книги.
 * @param reader_id айди читателя.
 * @param day день создания заявки.
 * @return true, если заявка создана; false, если нет места или уже есть активная заявка.
 */
bool request_create(Request requests[], int *count, int book_id, int reader_id, int day);
/**
 * @brief Деактивирует заявку:(.
 *
 * @param request заявка.
 */
void request_close(Request *request);

/**
 * @brief Находит самую раннюю активную заявку на книгу.
 *
 * @param requests массив заявок.
 * @param count количество заявок в массиве.
 * @param book_id айди книги.
 * @return Индекс заявки (REQUEST_INDEX_NOT_FOUND если активных заявок нет).
 */
int request_find_first(const Request requests[], int count, int book_id);

/**
 * @brief Выбирает активную заявку на книгу по заданной стратегии.
 *
 * @param requests массив заявок.
 * @param count количество заявок.
 * @param book_id айди книги.
 * @param strategy FIFO для ранней заявки, LIFO для поздней.
 * @return Индекс заявки или REQUEST_INDEX_NOT_FOUND, если заявок нет.
 */
int request_find(const Request requests[], int count, int book_id, IssueStrategy strategy);

#endif
