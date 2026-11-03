#include "reader.h"
#include "check.h"

bool is_reader_visit_on_said_day(const Reader *reader, int day){
    SOFT_ASSERT(reader != NULL, "Указатель на читателя = NULL", false);
    SOFT_ASSERT(reader->visit_count >= 0 && reader->visit_count <= READER_VISIT_CAPACITY, 
        "Количество посещений выходит за границы массива", false);
    SOFT_ASSERT(day > 0, "День <= 0", false);

    // проходимся циклом по дням посещения и смотрим совпадает ли день посещения библиотеки
    for (int idx = 0; idx < reader->visit_count; ++idx)
    {
        if (reader->visit_days[idx] == day)
        {
            return true;
        }
    }
    return false;
}

bool reader_has_book(const Reader *reader, int book_id){
    SOFT_ASSERT(reader != NULL, "Указатель на читателя = NULL", false);
    SOFT_ASSERT(reader->owned_count >= 0 && reader->owned_count <= READER_BOOK_CAPACITY, 
        "Количество книг у читателя выходит за границы массива", false);
    SOFT_ASSERT(book_id > 0, "Идентификатор книги <= 0", false);

    // циклом проходимся по имеющимся у читателя книгам и проверяем та ли эта книга, которую мы хотим
    for (int idx = 0; idx < reader->owned_count; ++idx)
    {
        if (reader->owned_books[idx] == book_id)
        {
            return true;
        }
    }
    return false;
}

bool reader_add_book(Reader *reader, int book_id, int return_day){
    SOFT_ASSERT(reader != NULL, "Указатель на читателя = NULL", false);
    SOFT_ASSERT(reader->owned_count >= 0, "Количество книг < 0", false);
    SOFT_ASSERT(book_id > 0, "Идентификатор книги <= 0", false);
    SOFT_ASSERT(return_day > 0, "День возврата <= 0", false);

    // если массив книг заполнен или у читателя УЖЕ ЕСТЬ эта книга, то не можем добавить книгу:(
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

void reader_remove_book(Reader *reader, int book_id){
    SOFT_ASSERT_VOID(reader != NULL, "Указатель на читателя = NULL.");
    SOFT_ASSERT_VOID(reader->owned_count >= 0 && reader->owned_count <= READER_BOOK_CAPACITY, 
        "Количество книг у читателя выходит за границы массива");
    SOFT_ASSERT_VOID(book_id > 0, "Идентификатор книги <= 0");

    // ищем нужную читателю книгу
    for (int idx = 0; idx < reader->owned_count; ++idx)
    {
        if (reader->owned_books[idx] == book_id)
        {

            // сдвигаем все последующие после неё элементы влево
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

void reader_delete_wish(Reader *reader, int book_id){
    SOFT_ASSERT_VOID(reader != NULL, "Указатель на читателя = NULL");
    SOFT_ASSERT_VOID(reader->wanted_count >= 0 && reader->wanted_count <= READER_BOOK_CAPACITY, 
        "Количество желаемых книг выходит за границы массива");
    SOFT_ASSERT_VOID(book_id > 0, "Идентификатор книги <= 0");

    // проходим по списку желаемых книг и удаляем все совпадения
    int idx = 0;
    while (idx < reader->wanted_count)
    {
        if (reader->wanted_books[idx] == book_id)
        {

            // сдвигаем оставшиеся элементы влево после удаления книги
            for (int jdx = idx; jdx + 1 < reader->wanted_count; ++jdx)
            {
                reader->wanted_books[jdx] = reader->wanted_books[jdx + 1];
            }
            --reader->wanted_count;
        } else
        {
            ++idx;
        }
    }
}
