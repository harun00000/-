#include "book.h"
#include "check.h"

int find_book_by_id(const Book books[], int count, int id){
    SOFT_ASSERT(books != NULL, "Указатель = NULL", BOOK_INDEX_NOT_FOUND);
    SOFT_ASSERT(count >= 0, "Количество элементов < 0", BOOK_INDEX_NOT_FOUND);
    SOFT_ASSERT(id > 0, "Идентификатор книги <= 0", BOOK_INDEX_NOT_FOUND);
    
    // просто проходимся циклом и сравниваем айди
    for (int idx = 0; idx < count; ++idx)
    {
        if (books[idx].id == id)
        {
            return idx;
        }
    }
    return BOOK_INDEX_NOT_FOUND;
}
