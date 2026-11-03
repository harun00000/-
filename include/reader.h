#ifndef READER_H
#define READER_H

#include <stdbool.h>

enum { READER_NAME_SIZE = 67, READER_BOOK_CAPACITY = 15, READER_VISIT_CAPACITY = 25 };

/**
 * @brief Структура читателя.
 */
typedef struct Reader{
    int id;
    char name[READER_NAME_SIZE];

    int wanted_books[READER_BOOK_CAPACITY]; // книги, которые читатель хочет
    int wanted_count;

    int owned_books[READER_BOOK_CAPACITY]; // книги, которые у него есть
    int return_days[READER_BOOK_CAPACITY]; // дни, в кот. он должен вернуть их
    int owned_count;

    int visit_days[READER_VISIT_CAPACITY]; // дни посещений
    int visit_count;
} Reader;

/**
 * @brief Проверяет, запланировано ли посещение в указанный день.
 *
 * @param reader читатель.
 * @param day день посещения библиотеки.
 * @return true, если посещение запланировано; false, если нет.
 */
bool is_reader_visit_on_said_day(const Reader *reader, int day);

/**
 * @brief Проверяет, есть ли указанная книга у читателя.
 *
 * @param reader читатель.
 * @param book_id айди книги.
 * @return true, если книга у читателя; false, если нет.
 */
bool reader_has_book(const Reader *reader, int book_id);

/**
 * @brief Добавляет книгу в список книг у читателя.
 *
 * @param reader читатель.
 * @param book_id айди книги.
 * @param return_day день возврата книги.
 * @return true, если книга добавлена; false, если нет места или книга уже у читателя.
 */
bool reader_add_book(Reader *reader, int book_id, int return_day);

/**
 * @brief Удаляет книгу из списка книг у читателя, если она там есть.
 *
 * @param reader читатель.
 * @param book_id айди книги.
 */
void reader_remove_book(Reader *reader, int book_id);

/**
 * @brief Удаляет указанную книгу из списка желаемых книг.
 *
 * @param reader читатель.
 * @param book_id айди полученной книги.
 */
void reader_delete_wish(Reader *reader, int book_id);

#endif
