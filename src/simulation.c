#include "simulation.h"
#include "check.h"
#include <stdio.h>
#include <stdlib.h>

static Reader *find_reader(Simulation *simulation, int reader_id){

    // циклом проходим по всем читателям и ищем читателя с нужным id
    for (int idx = 0; idx < simulation->reader_count; ++idx)
    {
        if (simulation->readers[idx].id == reader_id)
        {
            return &simulation->readers[idx];
        }
    }

    fprintf(stderr, "Ошибка: читатель %d не найден\n", reader_id);
    exit(EXIT_FAILURE);
}

void simulation_initialization(Simulation *simulation, const Book books[], int book_count,
     const Reader readers[], int reader_count, int reading_days, const Librarian *librarian){
    SOFT_ASSERT_VOID(simulation != NULL, "Указатель на симуляцию = NULL");
    SOFT_ASSERT_VOID(books != NULL, "Указатель на массив книг = NULL");
    SOFT_ASSERT_VOID(readers != NULL, "Указатель на массив читателей = NULL");
    SOFT_ASSERT_VOID(book_count > 0 && book_count <= MAX_BOOKS, "Количество книг <= 0 или выходит за допустимые границы");
    SOFT_ASSERT_VOID(reader_count > 0 && reader_count <= MAX_READERS, 
        "Количество читателей <= 0 или выходит за допустимые границы");
    SOFT_ASSERT_VOID(reading_days > 0, "Срок чтения <= 0");
    SOFT_ASSERT_VOID(librarian != NULL, "Указатель на библиотекаря = NULL");
    SOFT_ASSERT_VOID(librarian->id > 0, "Идентификатор библиотекаря <= 0");

    // проверяем корректность книг и отсутствие одинаковых айдишников
    for (int idx = 0; idx < book_count; ++idx)
    {
        SOFT_ASSERT_VOID(books[idx].id > 0, "Идентификатор книги <= 0");
        SOFT_ASSERT_VOID(find_book_by_id(books, idx, books[idx].id) == BOOK_INDEX_NOT_FOUND, 
        "Идентификаторы книг повторяются");
    }

    // проверяем читателей, их расписания и желаемые книги
    for (int idx = 0; idx < reader_count; ++idx)
    {
        SOFT_ASSERT_VOID(readers[idx].id > 0, "Идентификатор читателя <= 0");
        SOFT_ASSERT_VOID(readers[idx].owned_count == 0, "В начале симуляции у читателя есть книга");
        SOFT_ASSERT_VOID(readers[idx].wanted_count >= MIN_WANTED_BOOKS && readers[idx].wanted_count <= MAX_WANTED_BOOKS, 
            "Читатель должен выбрать от 1 до 3 книг");
        SOFT_ASSERT_VOID(readers[idx].visit_count >= 0 && readers[idx].visit_count <= READER_VISIT_CAPACITY,
             "Количество посещений выходит за допустимые границы");

        // чтоб айди не повторялось
        for (int jdx = 0; jdx < idx; ++jdx)
        {
            SOFT_ASSERT_VOID(readers[idx].id != readers[jdx].id, "Идентификаторы читателей повторяются");
        }

        // что все желаемые книги существуют в библиотеке
        for (int jdx = 0; jdx < readers[idx].wanted_count; ++jdx)
        {
            SOFT_ASSERT_VOID(find_book_by_id(books, book_count, readers[idx].wanted_books[jdx]) >= 0,
                "В списке желаемых книг указан неизвестный идентификатор");
        }
    }

    // заполняем симуляцию, переносим туда count и reading_days
    *simulation = (Simulation){0};
    simulation->librarian = *librarian;
    simulation->book_count = book_count;
    simulation->reader_count = reader_count;
    simulation->reading_days = reading_days;


    // копируем книги и отмечаем, что у них пока нет владельца и резерва
    for (int idx = 0; idx < book_count; ++idx)
    {
        simulation->books[idx] = books[idx];
        simulation->owner_ids[idx] = NO_READER;
        simulation->reserved_reader_ids[idx] = NO_READER;
    }

    // копируем читателей 
    for (int idx = 0; idx < reader_count; ++idx)
    {
        simulation->readers[idx] = readers[idx];
    }
}

static void notify_waiting_readers(Simulation *simulation, const Book *book){
    // проходимся по запросам и уведомляем читателей 
    for (int idx = 0; idx < simulation->request_count; ++idx)
    {
        const Request *request = &simulation->requests[idx];
        if (request->is_active && request->book_id == book->id)
        {
            Reader *reader = find_reader(simulation, request->reader_id);
            librarian_notify(&simulation->librarian, reader, book);
        }
    }
}

static void reserve_after_return(Simulation *simulation, int book_index, int day){
    SOFT_ASSERT_VOID(simulation != NULL, "Указатель на симуляцию = NULL");
    SOFT_ASSERT_VOID(book_index >= 0 && book_index < simulation->book_count, "Индекс книги выходит за границы массива");

    // находим книгу и первую активную заявку на неё
    const Book *book = &simulation->books[book_index];
    notify_waiting_readers(simulation, book);
    int first = request_find_first(simulation->requests, simulation->request_count, book->id);

    // если заявок нет, книга свободна!!
    if (first == REQUEST_INDEX_NOT_FOUND)
    {
        printf("Активных заявок нет: книга %s свободна\n", book->name);
        return;
    }

    // закрываем первую заявку и резервируем книгу за выбранным читателем
    Request *request = &simulation->requests[first];
    Reader *reader = find_reader(simulation, request->reader_id);
    request_close(request);
    simulation->reserved_reader_ids[book_index] = reader->id;
    simulation->reservation_days[book_index] = day;

    // выводим информацию в консоль
    printf("Выбрана заявка №%d (день %d), читатель %s. Заявка закрыта\n", first + 1, request->request_day, reader->name);
    printf("Книга %s зарезервирована за читателем %s; владельца нет\n", book->name, reader->name);
    printf("Получение возможно при посещении в более поздний день\n");
}

static void returning(Simulation *simulation, int day){
    // проходим циклом по читателям и смотрим, какие книги им пора вернуть
    for (int idx = 0; idx < simulation->reader_count; ++idx)
    {
        Reader *reader = &simulation->readers[idx];
        int jdx = 0;

        // проверяем все книги, которые сейчас находятся у читателя
        while (jdx < reader->owned_count)
        {
            // если день возврата не наступил, чекаем следующую книгу
            if (reader->return_days[jdx] > day)
            {
                ++jdx;
                continue;
            }

            // если день возврата наступил
            int book_id = reader->owned_books[jdx];
            int index = find_book_by_id(simulation->books, simulation->book_count, book_id);

            SOFT_ASSERT_VOID(index >= 0, "У читателя указан неизвестный айди книги");
            SOFT_ASSERT_VOID(simulation->owner_ids[index] == reader->id, 
                "Владелец возвращаемой книги не совпадает с читателем");
            
            // то возвращаем книгу и печатаем информацию в консоль
            reader_remove_book(reader, book_id);
            simulation->owner_ids[index] = NO_READER;
            printf("  Возврат: %s возвращает книгу «%s».\n", reader->name, simulation->books[index].name);
            reserve_after_return(simulation, index, day);
        }
    }
}

static bool give_a_book(Simulation *simulation, Reader *reader, int index, int day){
    SOFT_ASSERT(simulation != NULL, "Указатель на симуляцию = NULL", false);
    SOFT_ASSERT(reader != NULL, "Указатель на читателя = NULL", false);
    SOFT_ASSERT(index >= 0 && index < simulation->book_count, "Индекс книги выходит за границы массива", false);
    
    // берём книгу
    const Book *book = &simulation->books[index];
    int reserved_for = simulation->reserved_reader_ids[index];

    // если книга уже занята или уже есть у читателя, выдать её нельзя
    if (simulation->owner_ids[index] != NO_READER || reader_has_book(reader, book->id))
    {
        return false;
    }

    // если книга зарезервирована, получить её может только нужный читатель
    // и ТОЛЬКО в более поздний день после резервирования
    if (reserved_for != NO_READER && (reserved_for != reader->id || day <= simulation->reservation_days[index]))
    {
        return false;
    }

    // считаем день возврата и добавляем книгу читателю
    int return_day = day + simulation->reading_days;
    if (!reader_add_book(reader, book->id, return_day))
    {
        fprintf(stderr, "Не удалось добавить книгу читателю\n");
        exit(EXIT_FAILURE);
    }

    // назначаем нового владельца и снимаем резерв
    simulation->owner_ids[index] = reader->id;
    simulation->reserved_reader_ids[index] = NO_READER;
    simulation->reservation_days[index] = 0;
    reader_delete_wish(reader, book->id); // удаляем из списка желаний после выдачи

    printf("  %s: %s получает книгу %s. День возврата: %d.%s\n", reserved_for == NO_READER ? 
        "Выдача свободной книги" : "Выдача зарезервированной книги", reader->name, book->name, return_day,
        reserved_for == NO_READER ? "" : " Резервирование снято");
    return true;
}

static void reservation(Simulation *simulation, Reader *reader, int day){
    // циклом ищем книги, которые зарезервированы
    // за читателем и уже можно получить
    for (int idx = 0; idx < simulation->book_count; ++idx)
    {
        if (simulation->reserved_reader_ids[idx] == reader->id && simulation->reservation_days[idx] < day)
        {
            printf("%s пытается получить желаемую книгу %s по своему резерву\n", reader->name, simulation->books[idx].name);
            give_a_book(simulation, reader, idx, day);
        }
    }
}

static void wishes(Simulation *simulation, Reader *reader, int day){

     // проходим по всем желаемым книгам читателя
    int idx = 0;
    while (idx < reader->wanted_count)
    {
        int book_id = reader->wanted_books[idx];
        int index = find_book_by_id(simulation->books, simulation->book_count, book_id);

        SOFT_ASSERT_VOID(index >= 0, "У читателя указан неизвестный айди книги");
        
        const Book *book = &simulation->books[index];
        printf("%s пытается получить желаемую книгу %s\n", reader->name, book->name);

        // если книга уже есть у него, то удаляем её из списка желаний
        if (reader_has_book(reader, book_id))
        {
            printf("Книга уже у читателя; повторная выдача невозможна\n");
            reader_delete_wish(reader, book_id);
            continue;
        }

        // пробуем выдать книгу сразу
        if (give_a_book(simulation, reader, index, day))
        {
            continue;
        }

        // если выдать книгу нельзя, определяем причину почему не можем(
        if (simulation->owner_ids[index] != NO_READER)
        {
            Reader *owner = find_reader(simulation, simulation->owner_ids[index]);
            printf("Книга недоступна: занята, находится у читателя %s\n", owner->name);
        } else
        {
            Reader *reserved = find_reader(simulation, simulation->reserved_reader_ids[index]);
            printf("Книга недоступна: зарезервирована за читателем %s\n", reserved->name);
        }

        // проверяем, нужно ли создавать новую заявку
        if (simulation->reserved_reader_ids[index] == reader->id)
        {
            printf("Ваш резерв уже оформлен, получение только в более поздний день\n");
        } else if (request_exists(simulation->requests, simulation->request_count, book_id, reader->id))
        {
            printf("Активная заявка уже есть, повторная не создаётся\n");
        } else
        {
            // если заявки ещё нет, создаём её
            if (!request_create(simulation->requests, &simulation->request_count, book_id, reader->id, day))
            {
                fprintf(stderr, "Ошибка: не удалось создать заявку\n");
                exit(EXIT_FAILURE);
            }
            printf("Создана заявка №%d: %s, книга %s, день %d\n", simulation->request_count, reader->name, book->name, day);
        }

        ++idx;
    }
}

static void print_unfulfilled_requests(Simulation *simulation){
    bool has_active_requests = false;

    // циклооом проходим по всем заявкам и ищем те, которые остались активными
    for (int idx = 0; idx < simulation->request_count; ++idx)
    {
        const Request *request = &simulation->requests[idx];
        if (!request->is_active)
        {
            continue;
        }

        // находим читателя и книгу из заявки
        Reader *reader = find_reader(simulation, request->reader_id);
        int index = find_book_by_id(simulation->books, simulation->book_count, request->book_id);
        SOFT_ASSERT_VOID(index >= 0, "В заявке указан неизвестный айди книги");

        if (!has_active_requests)
        {
            printf("Невыполненные заявки:\n");
            has_active_requests = true;
        }
        printf("Читатель %s, книга %s, день создания заявки %d\n", reader->name, 
            simulation->books[index].name, request->request_day);
    }

    // если все заявки были выполнены
    if (!has_active_requests)
    {
        printf("Невыполненных заявок нет\n");
    }
}

static void print_unreceived_reservations(Simulation *simulation){
    bool has_reservations = false;

    // проходим по всем книгам и ищем те, которые остались в резерве
    for (int idx = 0; idx < simulation->book_count; ++idx)
    {
        if (simulation->reserved_reader_ids[idx] != NO_READER)
        {
            Reader *reader = find_reader(simulation, simulation->reserved_reader_ids[idx]);

            // заголовок выводим только один раз, если хотя бы один резерв найден
            if (!has_reservations)
            {
                printf("Не полученные к концу моделирования зарезервированные книги:\n");
                has_reservations = true;
            }
            printf("Читатель %s, книга %s\n", reader->name, simulation->books[idx].name);
        }
    }
}

void simulation_run(Simulation *simulation, int total_days){
    SOFT_ASSERT_VOID(simulation != NULL, "Указатель на симуляцию = NULL");
    SOFT_ASSERT_VOID(simulation->book_count > 0 && simulation->book_count <= MAX_BOOKS, 
        "Количество книг симуляции выходит за допустимые границы");
    SOFT_ASSERT_VOID(simulation->reader_count > 0 && simulation->reader_count <= MAX_READERS, 
        "Количество читателей симуляции выходит за допустимые границы");
    SOFT_ASSERT_VOID(simulation->request_count >= 0 && simulation->request_count <= MAX_REQUESTS, 
        "Количество заявок симуляции выходит за допустимые границы");
    SOFT_ASSERT_VOID(simulation->reading_days > 0, "Срок чтения должен быть положительным");
    SOFT_ASSERT_VOID(total_days >= 0, "Количество дней симуляции не должно быть отрицательным");

    // запускаем библиотеку 
    printf("Библиотека НАЧАЛА РАБОТАТЬ!. Количество дней работы: %d\n", total_days);
    for (int day = FIRST_SIMULATION_DAY; day <= total_days; ++day)
    {
        printf("\nДень %d: начало\n", day);

        returning(simulation, day); // обрабатываем возвраты книг

        // проверяем, кто из читателей приходит в библиотеку сегодня
        for (int idx = 0; idx < simulation->reader_count; ++idx)
        {
            Reader *reader = &simulation->readers[idx];
            if (is_reader_visit_on_said_day(reader, day))
            {
                printf("  Приход читателя: %s (id %d)\n", reader->name, reader->id);

                // обрабатываем его резервы, потом обычные желания
                reservation(simulation, reader, day);
                wishes(simulation, reader, day);
            }
        }
        printf("День %d: завершён\n", day);
    }
    print_unfulfilled_requests(simulation);
    print_unreceived_reservations(simulation);
    printf("\nБиблиотека В.С.Ё.\n");
}
