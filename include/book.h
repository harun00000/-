#ifndef BOOK_H
#define BOOK_H

enum { BOOK_NAME_SIZE = 167, BOOK_INDEX_NOT_FOUND = -1 };

/**
 * @brief Структура книги.
 */
typedef struct Book{
    int id;
    char name[BOOK_NAME_SIZE];
} Book;

/**
 * @brief Ищет книгу с указанным айди.
 *
 * @param books массив книг.
 * @param count количество книг в массиве.
 * @param id айди нужной книги.
 * @return Индекс найденной книги (BOOK_INDEX_NOT_FOUND если книга не найдена).
 */
int find_book_by_id(const Book books[], int count, int id);

#endif
