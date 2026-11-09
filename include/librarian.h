#ifndef LIBRARIAN_H
#define LIBRARIAN_H

#include "book.h"
#include "reader.h"
#include "logger.h"
#include "check.h"
#include <stdio.h>
#include <string.h>

enum
{
    LIBRARIAN_NAME_SIZE = 67
};

/**
 * @brief Структура библиотекаря.
 */
typedef struct Librarian
{
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
static inline void librarian_notify(const Librarian *librarian, const Reader *reader, const Book *book)
{
    SOFT_ASSERT_VOID(librarian != NULL, "Указатель на библиотекаря = NULL");
    SOFT_ASSERT_VOID(reader != NULL, "Указатель на читателя = NULL");
    SOFT_ASSERT_VOID(book != NULL, "Указатель на книгу = NULL");
    SOFT_ASSERT_VOID(
        librarian->id > 0 && reader->id > 0 && book->id > 0, "Неположительный идентификатор участника уведомления");
    SOFT_ASSERT_VOID(memchr(librarian->name, '\0', sizeof librarian->name) != NULL &&
                         memchr(reader->name, '\0', sizeof reader->name) != NULL &&
                         memchr(book->name, '\0', sizeof book->name) != NULL,
        "Имя участника уведомления не завершено нулём");
    printf("Библиотекарь %s уведомляет читателя %s: книга %s возвращена\n", librarian->name, reader->name, book->name);
    logger_write(
        "Библиотекарь %s уведомляет читателя %s: книга %s возвращена\n", librarian->name, reader->name, book->name);
}

#endif
