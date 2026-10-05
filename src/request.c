#include "request.h"
#include "check.h"

bool request_exists(const Request requests[], int count, int book_id, int reader_id){
    SOFT_ASSERT(requests != NULL, "Указатель на массив заявок = NULL", false);
    SOFT_ASSERT(count >= 0, "Количество элементов < 0", false);
    SOFT_ASSERT(book_id > 0, "Идентификатор книги <= 0", false);
    SOFT_ASSERT(reader_id > 0, "Идентификатор читателя <= 0", false);

    // циклом роходим по активным заявкам и ищем заявку ЭТОГО читателя на ЭТУ книгу
    for (int idx = 0; idx < count; ++idx)
    {
        if (requests[idx].is_active && requests[idx].book_id == book_id && requests[idx].reader_id == reader_id)
        {
            return true;
        }
    }
    return false;
}

bool request_create(Request requests[], int *count, int book_id, int reader_id, int day){
    SOFT_ASSERT(requests != NULL, "Указатель на массив заявок = NULL", false);
    SOFT_ASSERT(count != NULL, "Указатель на количество заявок = NULL", false);
    SOFT_ASSERT(*count >= 0, "Количество заявок < 0", false);
    SOFT_ASSERT(book_id > 0, "Идентификатор книги <= 0", false);
    SOFT_ASSERT(reader_id > 0, "Идентификатор читателя <= 0", false);
    SOFT_ASSERT(day > 0, "День <= 0", false);

    // массив заполнен или такая активная заявка уже есть
    if (*count >= MAX_REQUESTS || request_exists(requests, *count, book_id, reader_id))
    {
        return false;
    }

    // добавляем заявку в конец  
    requests[*count] = (Request){book_id, reader_id, day, true};
    ++*count;
    return true;
}

void request_close(Request *request){
    SOFT_ASSERT_VOID(request != NULL, "Указатель на заявку = NULL");
    request->is_active = false;
}

int request_find_first(const Request requests[], int count, int book_id){
    SOFT_ASSERT(requests != NULL, "Указатель на массив заявок = NULL", REQUEST_INDEX_NOT_FOUND);
    SOFT_ASSERT(count >= 0, "Количество элементов < 0", REQUEST_INDEX_NOT_FOUND);
    SOFT_ASSERT(book_id > 0, "Идентификатор книги <= 0", REQUEST_INDEX_NOT_FOUND);

    // ищем самую раннюю активную заявку на указанную книгу
    int first = REQUEST_INDEX_NOT_FOUND;
    for (int idx = 0; idx < count; ++idx)
    {
        if (requests[idx].is_active && requests[idx].book_id == book_id && (first == REQUEST_INDEX_NOT_FOUND || requests[idx].request_day < requests[first].request_day))
        {
            // при равных днях мы берем в порядке создания
            first = idx;
        }
    }
    return first;
}
