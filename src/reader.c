#include "reader.h"
#include "check.h"

bool is_reader_visit_on_said_day(const Reader *reader, int day)
{
    SOFT_ASSERT(reader != NULL, "Указатель на читателя = NULL", false);
    SOFT_ASSERT(reader->visit_count >= 0 && reader->visit_count <= READER_VISIT_CAPACITY,
        "Количество посещений выходит за границы массива", false);
    SOFT_ASSERT(day > 0, "День <= 0", false);

    // проверяем день по расписанию читателя
    for (int idx = 0; idx < reader->visit_count; ++idx)
    {
        SOFT_ASSERT(reader->visit_days[idx] > 0, "День посещения <= 0", false);
        if (reader->visit_days[idx] == day)
        {
            return true;
        }
    }
    return false;
}

bool reader_has_book(const Reader *reader, int book_id)
{
    SOFT_ASSERT(reader != NULL, "Указатель на читателя = NULL", false);
    SOFT_ASSERT(reader->owned_count >= 0 && reader->owned_count <= READER_BOOK_CAPACITY,
        "Количество книг у читателя выходит за границы массива", false);
    SOFT_ASSERT(book_id > 0, "Идентификатор книги <= 0", false);

    // ищем книгу среди выданных читателю
    for (int idx = 0; idx < reader->owned_count; ++idx)
    {
        SOFT_ASSERT(reader->owned_books[idx] > 0 && reader->return_days[idx] > 0,
            "Некорректная книга или день возврата у читателя", false);
        for (int jdx = idx + 1; jdx < reader->owned_count; ++jdx)
        {
            SOFT_ASSERT(
                reader->owned_books[idx] != reader->owned_books[jdx], "Одна книга находится у читателя дважды", false);
        }
        if (reader->owned_books[idx] == book_id)
        {
            return true;
        }
    }
    return false;
}

bool reader_add_book(Reader *reader, int book_id, int return_day)
{
    SOFT_ASSERT(reader != NULL, "Указатель на читателя = NULL", false);
    SOFT_ASSERT(reader->owned_count >= 0 && reader->owned_count <= READER_BOOK_CAPACITY,
        "Количество книг у читателя выходит за границы массива", false);
    SOFT_ASSERT(book_id > 0, "Идентификатор книги <= 0", false);
    SOFT_ASSERT(return_day > 0, "День возврата <= 0", false);

    for (int idx = 0; idx < reader->owned_count; ++idx)
    {
        SOFT_ASSERT(reader->owned_books[idx] > 0 && reader->return_days[idx] > 0,
            "Некорректная книга или день возврата у читателя", false);
        for (int jdx = idx + 1; jdx < reader->owned_count; ++jdx)
        {
            SOFT_ASSERT(
                reader->owned_books[idx] != reader->owned_books[jdx], "Одна книга находится у читателя дважды", false);
        }
    }

    // не добавляем книгу повторно или в заполненный массив
    if (reader->owned_count >= READER_BOOK_CAPACITY || reader_has_book(reader, book_id))
    {
        return false;
    }

    // добавляем книгу в конец
    reader->owned_books[reader->owned_count] = book_id;
    reader->return_days[reader->owned_count] = return_day;
    ++reader->owned_count;
    return true;
}

void reader_remove_book(Reader *reader, int book_id)
{
    SOFT_ASSERT_VOID(reader != NULL, "Указатель на читателя = NULL.");
    SOFT_ASSERT_VOID(reader->owned_count >= 0 && reader->owned_count <= READER_BOOK_CAPACITY,
        "Количество книг у читателя выходит за границы массива");
    SOFT_ASSERT_VOID(book_id > 0, "Идентификатор книги <= 0");

    // ищем нужную читателю книгу
    for (int idx = 0; idx < reader->owned_count; ++idx)
    {
        SOFT_ASSERT_VOID(reader->owned_books[idx] > 0 && reader->return_days[idx] > 0,
            "Некорректная книга или день возврата у читателя");
        for (int jdx = idx + 1; jdx < reader->owned_count; ++jdx)
        {
            SOFT_ASSERT_VOID(
                reader->owned_books[idx] != reader->owned_books[jdx], "Одна книга находится у читателя дважды");
        }
        if (reader->owned_books[idx] == book_id)
        {

            // сдвигаем оставшиеся книги влево
            for (int jdx = idx; jdx + 1 < reader->owned_count; ++jdx)
            {
                reader->owned_books[jdx] = reader->owned_books[jdx + 1];
                reader->return_days[jdx] = reader->return_days[jdx + 1];
            }
            --reader->owned_count;
            return;
        }
    }
}

void reader_delete_wish(Reader *reader, int book_id)
{
    SOFT_ASSERT_VOID(reader != NULL, "Указатель на читателя = NULL");
    SOFT_ASSERT_VOID(reader->wanted_count >= 0 && reader->wanted_count <= READER_BOOK_CAPACITY,
        "Количество желаемых книг выходит за границы массива");
    SOFT_ASSERT_VOID(book_id > 0, "Идентификатор книги <= 0");

    // проходим по списку желаемых книг и удаляем все совпадения
    int idx = 0;
    while (idx < reader->wanted_count)
    {
        SOFT_ASSERT_VOID(reader->wanted_books[idx] > 0, "Идентификатор желаемой книги <= 0");
        if (reader->wanted_books[idx] == book_id)
        {

            // сдвигаем оставшиеся желания влево
            for (int jdx = idx; jdx + 1 < reader->wanted_count; ++jdx)
            {
                reader->wanted_books[jdx] = reader->wanted_books[jdx + 1];
            }
            --reader->wanted_count;
        }
        else
        {
            ++idx;
        }
    }
}
