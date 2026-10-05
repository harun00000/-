#ifndef LIBRARIAN_H
#define LIBRARIAN_H

#include "book.h"
#include "reader.h"
#include <stdio.h>

enum { LIBRARIAN_NAME_SIZE = 67 };

/**
 * @brief Структура библиотекаря.
 */
typedef struct Librarian{
    int id;
    char name[LIBRARIAN_NAME_SIZE];
} Librarian;

/**
 * @brief Библиотекарь уведомляет читателя о возврате ожидаемой книги.
 *
 * @param librarian библиотекарь.
 * @param reader читатель, ожидающий книгу.
 * @param book возвращённая книга.
 */
static inline void librarian_notify(const Librarian *librarian, const Reader *reader, const Book *book){
    printf("Библиотекарь %s уведомляет читателя %s: книга %s возвращена\n", librarian->name, reader->name, book->name);
}

#endif
