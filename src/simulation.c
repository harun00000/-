#define _POSIX_C_SOURCE 200809L
#include "simulation.h"
#include "main_utils.h"
#include "check.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <limits.h>

static Reader *find_reader(Simulation *simulation, int reader_id)
{
    SOFT_ASSERT(simulation != NULL, "Указатель на симуляцию = NULL", NULL);
    SOFT_ASSERT(simulation->reader_count > 0 && simulation->reader_count <= MAX_READERS,
        "Количество читателей выходит за границы массива", NULL);
    SOFT_ASSERT(reader_id > 0, "Идентификатор читателя <= 0", NULL);

    // ищем читателя по id
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

void simulation_initialization(Simulation *simulation, const Book books[], int book_count, const Reader readers[],
    int reader_count, int reading_days, const Librarian *librarian, IssueStrategy issue_strategy,
    RequestRule request_rule)
{
    SOFT_ASSERT_VOID(simulation != NULL, "Указатель на симуляцию = NULL");
    SOFT_ASSERT_VOID(books != NULL, "Указатель на массив книг = NULL");
    SOFT_ASSERT_VOID(readers != NULL, "Указатель на массив читателей = NULL");
    SOFT_ASSERT_VOID(
        book_count > 0 && book_count <= MAX_BOOKS, "Количество книг <= 0 или выходит за допустимые границы");
    SOFT_ASSERT_VOID(
        reader_count > 0 && reader_count <= MAX_READERS, "Количество читателей <= 0 или выходит за допустимые границы");
    SOFT_ASSERT_VOID(reading_days > 0, "Срок чтения <= 0");
    SOFT_ASSERT_VOID(librarian != NULL, "Указатель на библиотекаря = NULL");
    SOFT_ASSERT_VOID(librarian->id > 0, "Идентификатор библиотекаря <= 0");
    SOFT_ASSERT_VOID(issue_strategy == ISSUE_FIFO || issue_strategy == ISSUE_LIFO, "Неизвестная стратегия выдачи");
    SOFT_ASSERT_VOID(request_rule == REQUEST_ON_UNAVAILABLE || request_rule == REQUEST_ALWAYS_WAIT,
        "Неизвестное правило формирования заявок");

    SOFT_ASSERT_VOID(
        memchr(librarian->name, '\0', sizeof librarian->name) != NULL, "Имя библиотекаря не завершено нулём");

    // проверяем книги и уникальность их айди
    for (int idx = 0; idx < book_count; ++idx)
    {
        SOFT_ASSERT_VOID(books[idx].id > 0, "Идентификатор книги <= 0");
        SOFT_ASSERT_VOID(
            memchr(books[idx].name, '\0', sizeof books[idx].name) != NULL, "Название книги не завершено нулём");
        SOFT_ASSERT_VOID(
            find_book_by_id(books, idx, books[idx].id) == BOOK_INDEX_NOT_FOUND, "Идентификаторы книг повторяются");
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

        SOFT_ASSERT_VOID(
            memchr(readers[idx].name, '\0', sizeof readers[idx].name) != NULL, "Имя читателя не завершено нулём");
        for (int jdx = 0; jdx < readers[idx].visit_count; ++jdx)
        {
            SOFT_ASSERT_VOID(readers[idx].visit_days[jdx] >= FIRST_SIMULATION_DAY, "День посещения <= 0");
        }

        // проверяем уникальность айди читателя
        for (int jdx = 0; jdx < idx; ++jdx)
        {
            SOFT_ASSERT_VOID(readers[idx].id != readers[jdx].id, "Идентификаторы читателей повторяются");
        }

        // проверяем наличие желаемых книг в библиотеке
        for (int jdx = 0; jdx < readers[idx].wanted_count; ++jdx)
        {
            SOFT_ASSERT_VOID(find_book_by_id(books, book_count, readers[idx].wanted_books[jdx]) >= 0,
                "В списке желаемых книг указан неизвестный идентификатор");
        }
    }

    // заполняем исходное состояние симуляции
    *simulation = (Simulation){0};
    statistics_initialization(&simulation->statistics);
    simulation->librarian = *librarian;
    simulation->book_count = book_count;
    simulation->reader_count = reader_count;
    simulation->reading_days = reading_days;
    simulation->issue_strategy = issue_strategy;
    simulation->request_rule = request_rule;

    // копируем книги без владельцев и резервов
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

static void notify_waiting_readers(Simulation *simulation, const Book *book)
{
    SOFT_ASSERT_VOID(simulation != NULL, "Указатель на симуляцию = NULL");
    SOFT_ASSERT_VOID(simulation->reader_count > 0 && simulation->reader_count <= MAX_READERS,
        "Количество читателей выходит за границы массива");
    SOFT_ASSERT_VOID(simulation->request_count >= 0 && simulation->request_count <= MAX_REQUESTS,
        "Количество заявок выходит за границы массива");
    SOFT_ASSERT_VOID(book != NULL, "Указатель на книгу = NULL");
    SOFT_ASSERT_VOID(book->id > 0, "Идентификатор книги <= 0");

    // уведомляем читателей с активными заявками
    for (int idx = 0; idx < simulation->request_count; ++idx)
    {
        const Request *request = &simulation->requests[idx];
        if (request->is_active && request->book_id == book->id)
        {
            Reader *reader = find_reader(simulation, request->reader_id);
            SOFT_ASSERT_VOID(reader != NULL, "Читатель из заявки не найден");
            SOFT_ASSERT_VOID(
                simulation->statistics.notifications_sent >= 0 && simulation->statistics.notifications_sent < INT_MAX,
                "Счётчик статистики вне допустимого диапазона");
            librarian_notify(&simulation->librarian, reader, book);
            ++simulation->statistics.notifications_sent;
        }
    }
}

static void reserve_after_return(Simulation *simulation, int book_index, int day)
{
    SOFT_ASSERT_VOID(simulation != NULL, "Указатель на симуляцию = NULL");
    SOFT_ASSERT_VOID(simulation->book_count > 0 && simulation->book_count <= MAX_BOOKS,
        "Количество книг выходит за границы массива");
    SOFT_ASSERT_VOID(simulation->reader_count > 0 && simulation->reader_count <= MAX_READERS,
        "Количество читателей выходит за границы массива");
    SOFT_ASSERT_VOID(simulation->request_count >= 0 && simulation->request_count <= MAX_REQUESTS,
        "Количество заявок выходит за границы массива");
    SOFT_ASSERT_VOID(day >= FIRST_SIMULATION_DAY, "Некорректный день симуляции");
    SOFT_ASSERT_VOID(book_index >= 0 && book_index < simulation->book_count, "Индекс книги выходит за границы массива");
    SOFT_ASSERT_VOID(simulation->owner_ids[book_index] == NO_READER, "Резервируется книга, у которой есть владелец");
    SOFT_ASSERT_VOID(
        simulation->reserved_reader_ids[book_index] == NO_READER && simulation->reservation_days[book_index] == 0,
        "Возвращённая книга уже имеет резерв");
    SOFT_ASSERT_VOID(simulation->issue_strategy == ISSUE_FIFO || simulation->issue_strategy == ISSUE_LIFO,
        "Неизвестная стратегия выдачи");

    // находим книгу и первую активную заявку на неё
    const Book *book = &simulation->books[book_index];
    notify_waiting_readers(simulation, book);
    int first = request_find(simulation->requests, simulation->request_count, book->id, simulation->issue_strategy);

    // без активных заявок книга остаётся свободной
    if (first == REQUEST_INDEX_NOT_FOUND)
    {
        printf("Активных заявок нет: книга %s свободна\n", book->name);
        return;
    }

    // закрываем выбранную заявку и резервируем книгу
    SOFT_ASSERT_VOID(first >= 0 && first < simulation->request_count, "Индекс найденной заявки вне массива");
    Request *request = &simulation->requests[first];
    SOFT_ASSERT_VOID(
        request->is_active && request->book_id == book->id, "Выбрана закрытая заявка или заявка на другую книгу");
    SOFT_ASSERT_VOID(request->request_day >= FIRST_SIMULATION_DAY && request->request_day <= day,
        "Некорректный день создания выбранной заявки");
    Reader *reader = find_reader(simulation, request->reader_id);
    SOFT_ASSERT_VOID(reader != NULL, "Читатель из заявки не найден");
    SOFT_ASSERT_VOID(!reader_has_book(reader, book->id), "Резервируемая книга уже есть у читателя");
    SOFT_ASSERT_VOID(
        simulation->statistics.requests_completed >= 0 && simulation->statistics.requests_completed < INT_MAX,
        "Счётчик статистики вне допустимого диапазона");
    request_close(request);
    ++simulation->statistics.requests_completed;
    simulation->reserved_reader_ids[book_index] = reader->id;
    simulation->reservation_days[book_index] = day;

    // выводим информацию в консоль
    printf(
        "Выбрана заявка №%d (день %d), читатель %s. Заявка закрыта\n", first + 1, request->request_day, reader->name);
    printf("Книга %s зарезервирована за читателем %s; владельца нет\n", book->name, reader->name);
    logger_write("Резервирование: книга %s, читатель %s, день %d\n", book->name, reader->name, day);
    printf("Получение возможно при посещении в более поздний день\n");
}

static void returning(Simulation *simulation, int day)
{
    SOFT_ASSERT_VOID(simulation != NULL, "Указатель на симуляцию = NULL");
    SOFT_ASSERT_VOID(simulation->book_count > 0 && simulation->book_count <= MAX_BOOKS,
        "Количество книг выходит за границы массива");
    SOFT_ASSERT_VOID(simulation->reader_count > 0 && simulation->reader_count <= MAX_READERS,
        "Количество читателей выходит за границы массива");
    SOFT_ASSERT_VOID(day >= FIRST_SIMULATION_DAY, "Некорректный день симуляции");

    // проверяем сроки возврата книг у читателей
    for (int idx = 0; idx < simulation->reader_count; ++idx)
    {
        Reader *reader = &simulation->readers[idx];
        SOFT_ASSERT_VOID(reader->owned_count >= 0 && reader->owned_count <= READER_BOOK_CAPACITY,
            "Количество книг у читателя выходит за границы массива");
        int jdx = 0;

        // проверяем выданные читателю книги
        while (jdx < reader->owned_count)
        {
            SOFT_ASSERT_VOID(reader->return_days[jdx] >= FIRST_SIMULATION_DAY, "День возврата <= 0");
            // пропускаем книги, срок возврата которых ещё не наступил
            if (reader->return_days[jdx] > day)
            {
                ++jdx;
                continue;
            }

            // находим книгу для возврата
            int book_id = reader->owned_books[jdx];
            int index = find_book_by_id(simulation->books, simulation->book_count, book_id);

            SOFT_ASSERT_VOID(index >= 0 && index < simulation->book_count, "У читателя указан неизвестный айди книги");
            SOFT_ASSERT_VOID(
                simulation->owner_ids[index] == reader->id, "Владелец возвращаемой книги не совпадает с читателем");

            SOFT_ASSERT_VOID(
                simulation->reserved_reader_ids[index] == NO_READER && simulation->reservation_days[index] == 0,
                "Книга одновременно выдана и зарезервирована");

            // возвращаем книгу и выводим событие
            SOFT_ASSERT_VOID(reader_has_book(reader, book_id), "Возвращаемая книга отсутствует у читателя");
            SOFT_ASSERT_VOID(
                simulation->statistics.books_returned >= 0 && simulation->statistics.books_returned < INT_MAX,
                "Счётчик статистики вне допустимого диапазона");
            reader_remove_book(reader, book_id);
            ++simulation->statistics.books_returned;
            simulation->owner_ids[index] = NO_READER;
            printf("  Возврат: %s возвращает книгу «%s».\n", reader->name, simulation->books[index].name);
            logger_write(
                "Возврат: %s возвращает книгу %s, день %d\n", reader->name, simulation->books[index].name, day);
            reserve_after_return(simulation, index, day);
        }
    }
}

static bool give_a_book(Simulation *simulation, Reader *reader, int index, int day)
{
    SOFT_ASSERT(simulation != NULL, "Указатель на симуляцию = NULL", false);
    SOFT_ASSERT(reader != NULL, "Указатель на читателя = NULL", false);
    SOFT_ASSERT(simulation->book_count > 0 && simulation->book_count <= MAX_BOOKS,
        "Количество книг выходит за границы массива", false);
    SOFT_ASSERT(index >= 0 && index < simulation->book_count, "Индекс книги выходит за границы массива", false);
    SOFT_ASSERT(
        day >= FIRST_SIMULATION_DAY && simulation->reading_days > 0, "Некорректный день или срок чтения", false);
    SOFT_ASSERT(
        reader->id > 0 && find_reader(simulation, reader->id) == reader, "Читатель не принадлежит симуляции", false);
    SOFT_ASSERT(reader->owned_count >= 0 && reader->owned_count <= READER_BOOK_CAPACITY && reader->wanted_count >= 0 &&
                    reader->wanted_count <= MAX_WANTED_BOOKS,
        "Количество книг читателя выходит за границы массива", false);
    SOFT_ASSERT(simulation->owner_ids[index] == NO_READER || simulation->reserved_reader_ids[index] == NO_READER,
        "Книга одновременно выдана и зарезервирована", false);
    SOFT_ASSERT(simulation->reserved_reader_ids[index] == NO_READER
                    ? simulation->reservation_days[index] == 0
                    : (simulation->reservation_days[index] >= FIRST_SIMULATION_DAY &&
                          simulation->reservation_days[index] <= day),
        "День резервирования не согласован с резервом", false);

    // берём книгу
    const Book *book = &simulation->books[index];
    int reserved_for = simulation->reserved_reader_ids[index];
    SOFT_ASSERT(simulation->reader_count > 0 && simulation->reader_count <= MAX_READERS,
        "Количество читателей вне массива", false);
    bool owner_found = simulation->owner_ids[index] == NO_READER;
    bool reserve_found = reserved_for == NO_READER;
    for (int idx = 0; idx < simulation->reader_count; ++idx)
    {
        owner_found = owner_found || simulation->readers[idx].id == simulation->owner_ids[index];
        reserve_found = reserve_found || simulation->readers[idx].id == reserved_for;
    }
    SOFT_ASSERT(owner_found && reserve_found, "Владелец или читатель с резервом не найден", false);
    SOFT_ASSERT(reader_has_book(reader, book->id) == (simulation->owner_ids[index] == reader->id),
        "Список книг читателя не согласован с владельцем", false);

    // проверяем, можно ли выдать книгу
    if (simulation->owner_ids[index] != NO_READER || reader_has_book(reader, book->id))
    {
        return false;
    }

    // резерв доступен только выбранному читателю после дня резервирования
    if (reserved_for != NO_READER && (reserved_for != reader->id || day <= simulation->reservation_days[index]))
    {
        return false;
    }

    // считаем день возврата и добавляем книгу читателю
    SOFT_ASSERT(day <= INT_MAX - simulation->reading_days, "Переполнение дня возврата", false);
    int return_day = day + simulation->reading_days;
    SOFT_ASSERT(simulation->statistics.books_given >= 0 && simulation->statistics.books_given < INT_MAX,
        "Счётчик статистики вне допустимого диапазона", false);
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
    ++simulation->statistics.books_given;

    printf("  %s: %s получает книгу %s. День возврата: %d.%s\n",
        reserved_for == NO_READER ? "Выдача свободной книги" : "Выдача зарезервированной книги", reader->name,
        book->name, return_day, reserved_for == NO_READER ? "" : " Резервирование снято");
    logger_write(
        "Выдача: %s получает книгу %s, день %d, день возврата %d\n", reader->name, book->name, day, return_day);
    return true;
}

static void reservation(Simulation *simulation, Reader *reader, int day)
{
    SOFT_ASSERT_VOID(simulation != NULL, "Указатель на симуляцию = NULL");
    SOFT_ASSERT_VOID(simulation->book_count > 0 && simulation->book_count <= MAX_BOOKS,
        "Количество книг выходит за границы массива");
    SOFT_ASSERT_VOID(reader != NULL, "Указатель на читателя = NULL");
    SOFT_ASSERT_VOID(day >= FIRST_SIMULATION_DAY, "Некорректный день симуляции");
    SOFT_ASSERT_VOID(
        reader->id > 0 && find_reader(simulation, reader->id) == reader, "Читатель не принадлежит симуляции");

    // выдаём доступные резервы читателя
    for (int idx = 0; idx < simulation->book_count; ++idx)
    {
        if (simulation->reserved_reader_ids[idx] == reader->id && simulation->reservation_days[idx] < day)
        {
            SOFT_ASSERT_VOID(
                simulation->owner_ids[idx] == NO_READER && simulation->reservation_days[idx] >= FIRST_SIMULATION_DAY,
                "Некорректное состояние резерва");
            printf("%s пытается получить желаемую книгу %s по своему резерву\n", reader->name,
                simulation->books[idx].name);
            give_a_book(simulation, reader, idx, day);
        }
    }
}

static void wishes(Simulation *simulation, Reader *reader, int day)
{
    SOFT_ASSERT_VOID(simulation != NULL, "Указатель на симуляцию = NULL");
    SOFT_ASSERT_VOID(simulation->book_count > 0 && simulation->book_count <= MAX_BOOKS,
        "Количество книг выходит за границы массива");
    SOFT_ASSERT_VOID(simulation->request_count >= 0 && simulation->request_count <= MAX_REQUESTS,
        "Количество заявок выходит за границы массива");
    SOFT_ASSERT_VOID(reader != NULL, "Указатель на читателя = NULL");
    SOFT_ASSERT_VOID(day >= FIRST_SIMULATION_DAY, "Некорректный день симуляции");
    SOFT_ASSERT_VOID(
        reader->id > 0 && find_reader(simulation, reader->id) == reader, "Читатель не принадлежит симуляции");
    SOFT_ASSERT_VOID(reader->wanted_count >= 0 && reader->wanted_count <= MAX_WANTED_BOOKS,
        "Количество желаемых книг выходит за границы массива");
    SOFT_ASSERT_VOID(
        simulation->request_rule == REQUEST_ON_UNAVAILABLE || simulation->request_rule == REQUEST_ALWAYS_WAIT,
        "Неизвестное правило формирования заявок");

    bool wait_for_books = false;
    if (simulation->request_rule == REQUEST_ALWAYS_WAIT)
    {
        // проверяем весь список до выдачи обычных желаемых книг
        for (int idx = 0; idx < reader->wanted_count; ++idx)
        {
            int index = find_book_by_id(simulation->books, simulation->book_count, reader->wanted_books[idx]);
            SOFT_ASSERT_VOID(index >= 0 && index < simulation->book_count, "У читателя указан неизвестный айди книги");

            if ((simulation->owner_ids[index] != NO_READER && simulation->owner_ids[index] != reader->id) ||
                (simulation->reserved_reader_ids[index] != NO_READER &&
                    simulation->reserved_reader_ids[index] != reader->id))
            {
                wait_for_books = true;
                break;
            }
        }
    }

    // обрабатываем желания читателя
    int idx = 0;
    while (idx < reader->wanted_count)
    {
        int book_id = reader->wanted_books[idx];
        int index = find_book_by_id(simulation->books, simulation->book_count, book_id);

        SOFT_ASSERT_VOID(index >= 0 && index < simulation->book_count, "У читателя указан неизвестный айди книги");

        const Book *book = &simulation->books[index];
        printf("%s пытается получить желаемую книгу %s\n", reader->name, book->name);

        // удаляем из желаний уже выданную книгу
        if (reader_has_book(reader, book_id))
        {
            printf("Книга уже у читателя; повторная выдача невозможна\n");
            reader_delete_wish(reader, book_id);
            continue;
        }

        if (wait_for_books && simulation->owner_ids[index] == NO_READER &&
            simulation->reserved_reader_ids[index] == NO_READER)
        {
            ++idx;
            continue;
        }

        // пробуем выдать книгу сразу
        if (give_a_book(simulation, reader, index, day))
        {
            continue;
        }

        // сообщаем причину отказа в выдаче
        if (simulation->owner_ids[index] != NO_READER)
        {
            Reader *owner = find_reader(simulation, simulation->owner_ids[index]);
            SOFT_ASSERT_VOID(owner != NULL, "Владелец книги не найден");
            printf("Книга недоступна: занята, находится у читателя %s\n", owner->name);
            logger_write(
                "Книга недоступна: %s находится у читателя %s, запросил %s\n", book->name, owner->name, reader->name);
        }
        else
        {
            SOFT_ASSERT_VOID(simulation->reserved_reader_ids[index] != NO_READER,
                "Отказ в выдаче свободной незарезервированной книги");
            Reader *reserved = find_reader(simulation, simulation->reserved_reader_ids[index]);
            SOFT_ASSERT_VOID(reserved != NULL, "Читатель с резервом не найден");
            printf("Книга недоступна: зарезервирована за читателем %s\n", reserved->name);
            logger_write("Книга недоступна: %s зарезервирована за читателем %s, запросил %s\n", book->name,
                reserved->name, reader->name);
        }

        // проверяем, нужно ли создавать новую заявку
        if (simulation->reserved_reader_ids[index] == reader->id)
        {
            printf("Ваш резерв уже оформлен, получение только в более поздний день\n");
        }
        else if (request_exists(simulation->requests, simulation->request_count, book_id, reader->id))
        {
            printf("Активная заявка уже есть, повторная не создаётся\n");
        }
        else
        {
            // если заявки ещё нет, создаём её
            SOFT_ASSERT_VOID(
                simulation->statistics.requests_created >= 0 && simulation->statistics.requests_created < INT_MAX,
                "Счётчик статистики вне допустимого диапазона");
            if (!request_create(simulation->requests, &simulation->request_count, book_id, reader->id, day))
            {
                fprintf(stderr, "Ошибка: не удалось создать заявку\n");
                exit(EXIT_FAILURE);
            }
            printf("Создана заявка №%d: %s, книга %s, день %d\n", simulation->request_count, reader->name, book->name,
                day);
            ++simulation->statistics.requests_created;
            logger_write("Создана заявка №%d: %s, книга %s, день %d\n", simulation->request_count, reader->name,
                book->name, day);
        }

        ++idx;
    }
}

static void print_unfulfilled_requests(Simulation *simulation)
{
    SOFT_ASSERT_VOID(simulation != NULL, "Указатель на симуляцию = NULL");
    SOFT_ASSERT_VOID(simulation->book_count > 0 && simulation->book_count <= MAX_BOOKS,
        "Количество книг выходит за границы массива");
    SOFT_ASSERT_VOID(simulation->reader_count > 0 && simulation->reader_count <= MAX_READERS,
        "Количество читателей выходит за границы массива");
    SOFT_ASSERT_VOID(simulation->request_count >= 0 && simulation->request_count <= MAX_REQUESTS,
        "Количество заявок выходит за границы массива");

    bool has_active_requests = false;

    // выводим оставшиеся активные заявки
    for (int idx = 0; idx < simulation->request_count; ++idx)
    {
        const Request *request = &simulation->requests[idx];
        if (!request->is_active)
        {
            continue;
        }

        // находим читателя и книгу из заявки
        Reader *reader = find_reader(simulation, request->reader_id);
        SOFT_ASSERT_VOID(reader != NULL, "Читатель из заявки не найден");
        SOFT_ASSERT_VOID(request->request_day >= FIRST_SIMULATION_DAY, "День создания заявки <= 0");
        int index = find_book_by_id(simulation->books, simulation->book_count, request->book_id);
        SOFT_ASSERT_VOID(index >= 0 && index < simulation->book_count, "В заявке указан неизвестный айди книги");

        if (!has_active_requests)
        {
            printf("Невыполненные заявки:\n");
            has_active_requests = true;
        }
        printf("Читатель %s, книга %s, день создания заявки %d\n", reader->name, simulation->books[index].name,
            request->request_day);
    }

    // если все заявки были выполнены
    if (!has_active_requests)
    {
        printf("Невыполненных заявок нет\n");
    }
}

static void print_unreceived_reservations(Simulation *simulation)
{
    SOFT_ASSERT_VOID(simulation != NULL, "Указатель на симуляцию = NULL");
    SOFT_ASSERT_VOID(simulation->book_count > 0 && simulation->book_count <= MAX_BOOKS,
        "Количество книг выходит за границы массива");
    SOFT_ASSERT_VOID(simulation->reader_count > 0 && simulation->reader_count <= MAX_READERS,
        "Количество читателей выходит за границы массива");

    bool has_reservations = false;

    // выводим оставшиеся резервы
    for (int idx = 0; idx < simulation->book_count; ++idx)
    {
        if (simulation->reserved_reader_ids[idx] != NO_READER)
        {
            Reader *reader = find_reader(simulation, simulation->reserved_reader_ids[idx]);

            SOFT_ASSERT_VOID(reader != NULL, "Читатель с резервом не найден");
            SOFT_ASSERT_VOID(
                simulation->owner_ids[idx] == NO_READER && simulation->reservation_days[idx] >= FIRST_SIMULATION_DAY,
                "Некорректное состояние резерва");

            // выводим заголовок перед первым резервом
            if (!has_reservations)
            {
                printf("Не полученные к концу моделирования зарезервированные книги:\n");
                has_reservations = true;
            }
            printf("Читатель %s, книга %s\n", reader->name, simulation->books[idx].name);
        }
    }
}

#if !defined(LIBRARY_PROCESS_4_5) && !defined(LIBRARY_PROCESS_6)
typedef struct ReaderThreadArgs
{
    Simulation *simulation;
    int reader_index;
} ReaderThreadArgs;

static void check_pthread(int error, const char *operation)
{
    SOFT_ASSERT_VOID(operation != NULL, "Указатель на название pthread-операции = NULL");

    if (error != 0)
    {
        fprintf(stderr, "%s: %s\n", operation, strerror(error));
        exit(EXIT_FAILURE);
    }
}

static void wait_semaphore(sem_t *semaphore)
{
    SOFT_ASSERT_VOID(semaphore != NULL, "Указатель на семафор = NULL");

    while (sem_wait(semaphore) != 0)
    {
        if (errno != EINTR)
        {
            perror("sem_wait");
            exit(EXIT_FAILURE);
        }
    }
}

static void post_semaphore(sem_t *semaphore)
{
    SOFT_ASSERT_VOID(semaphore != NULL, "Указатель на семафор = NULL");

    if (sem_post(semaphore) != 0)
    {
        perror("sem_post");
        exit(EXIT_FAILURE);
    }
}

static void *librarian_thread(void *arg)
{
    SOFT_ASSERT(arg != NULL, "Указатель на аргумент потока библиотекаря = NULL", NULL);

    Simulation *simulation = arg;
    while (true)
    {
        wait_semaphore(&simulation->librarian_start);
        check_pthread(pthread_mutex_lock(&simulation->mutex), "pthread_mutex_lock");
        if (simulation->finished)
        {
            check_pthread(pthread_mutex_unlock(&simulation->mutex), "pthread_mutex_unlock");
            break;
        }

        returning(simulation, simulation->current_day);
        check_pthread(pthread_mutex_unlock(&simulation->mutex), "pthread_mutex_unlock");
        post_semaphore(&simulation->phase_done);
    }
    return NULL;
}

static void *reader_thread(void *arg)
{
    SOFT_ASSERT(arg != NULL, "Указатель на аргументы потока читателя = NULL", NULL);

    ReaderThreadArgs *args = arg;
    SOFT_ASSERT(args->simulation != NULL, "Указатель на симуляцию = NULL", NULL);
    Simulation *simulation = args->simulation;
    SOFT_ASSERT(simulation->reader_count > 0 && simulation->reader_count <= MAX_READERS,
        "Количество читателей выходит за границы массива", NULL);
    SOFT_ASSERT(args->reader_index >= 0 && args->reader_index < simulation->reader_count,
        "Индекс читателя выходит за границы массива", NULL);
    while (true)
    {
        wait_semaphore(&simulation->reader_start[args->reader_index]);
        check_pthread(pthread_mutex_lock(&simulation->mutex), "pthread_mutex_lock");
        if (simulation->finished)
        {
            check_pthread(pthread_mutex_unlock(&simulation->mutex), "pthread_mutex_unlock");
            break;
        }

        simulation_process_reader(simulation, args->reader_index, simulation->current_day);
        check_pthread(pthread_mutex_unlock(&simulation->mutex), "pthread_mutex_unlock");
        post_semaphore(&simulation->phase_done);
    }
    return NULL;
}

#endif

bool simulation_validate(const Simulation *simulation, int total_days)
{
    SOFT_ASSERT(simulation != NULL, "Указатель на симуляцию = NULL", false);
    SOFT_ASSERT(simulation->book_count > 0 && simulation->book_count <= MAX_BOOKS,
        "Количество книг симуляции выходит за допустимые границы", false);
    SOFT_ASSERT(simulation->reader_count > 0 && simulation->reader_count <= MAX_READERS,
        "Количество читателей симуляции выходит за допустимые границы", false);
    SOFT_ASSERT(simulation->request_count >= 0 && simulation->request_count <= MAX_REQUESTS,
        "Количество заявок симуляции выходит за допустимые границы", false);
    SOFT_ASSERT(simulation->reading_days > 0, "Срок чтения должен быть положительным", false);
    SOFT_ASSERT(total_days >= 0, "Количество дней симуляции не должно быть отрицательным", false);

    SOFT_ASSERT(simulation->issue_strategy == ISSUE_FIFO || simulation->issue_strategy == ISSUE_LIFO,
        "Неизвестная стратегия выдачи", false);
    SOFT_ASSERT(simulation->request_rule == REQUEST_ON_UNAVAILABLE || simulation->request_rule == REQUEST_ALWAYS_WAIT,
        "Неизвестное правило формирования заявок", false);
    SOFT_ASSERT(simulation->librarian.id > 0 &&
                    memchr(simulation->librarian.name, '\0', sizeof simulation->librarian.name) != NULL,
        "Некорректный библиотекарь", false);
    SOFT_ASSERT(simulation->statistics.reader_visits >= 0 && simulation->statistics.books_given >= 0 &&
                    simulation->statistics.books_returned >= 0 && simulation->statistics.requests_created >= 0 &&
                    simulation->statistics.requests_completed >= 0 && simulation->statistics.notifications_sent >= 0,
        "Счётчики статистики не должны быть отрицательными", false);
    SOFT_ASSERT(simulation->statistics.books_returned <= simulation->statistics.books_given &&
                    simulation->statistics.requests_completed <= simulation->statistics.requests_created,
        "Счётчики завершённых событий превышают счётчики начатых", false);

    for (int idx = 0; idx < simulation->reader_count; ++idx)
    {
        const Reader *reader = &simulation->readers[idx];
        SOFT_ASSERT(
            reader->id > 0 && memchr(reader->name, '\0', sizeof reader->name) != NULL, "Некорректный читатель", false);
        SOFT_ASSERT(reader->owned_count >= 0 && reader->owned_count <= READER_BOOK_CAPACITY &&
                        reader->wanted_count >= 0 && reader->wanted_count <= MAX_WANTED_BOOKS &&
                        reader->visit_count >= 0 && reader->visit_count <= READER_VISIT_CAPACITY,
            "Размеры массивов читателя вне допустимых границ", false);
        for (int jdx = 0; jdx < idx; ++jdx)
        {
            SOFT_ASSERT(reader->id != simulation->readers[jdx].id, "Идентификаторы читателей повторяются", false);
        }
        for (int jdx = 0; jdx < reader->visit_count; ++jdx)
        {
            SOFT_ASSERT(reader->visit_days[jdx] >= FIRST_SIMULATION_DAY, "День посещения <= 0", false);
        }
        for (int jdx = 0; jdx < reader->wanted_count; ++jdx)
        {
            SOFT_ASSERT(find_book_by_id(simulation->books, simulation->book_count, reader->wanted_books[jdx]) !=
                            BOOK_INDEX_NOT_FOUND,
                "В списке желаемых книг указан неизвестный идентификатор", false);
        }
        for (int jdx = 0; jdx < reader->owned_count; ++jdx)
        {
            int index = find_book_by_id(simulation->books, simulation->book_count, reader->owned_books[jdx]);
            SOFT_ASSERT(index >= 0 && index < simulation->book_count, "У читателя указана неизвестная книга", false);
            SOFT_ASSERT(simulation->owner_ids[index] == reader->id && reader->return_days[jdx] >= FIRST_SIMULATION_DAY,
                "Книга читателя не согласована с владельцем или днём возврата", false);
            for (int kdx = 0; kdx < jdx; ++kdx)
            {
                SOFT_ASSERT(reader->owned_books[jdx] != reader->owned_books[kdx],
                    "Одна книга находится у читателя дважды", false);
            }
        }
    }
    for (int idx = 0; idx < simulation->book_count; ++idx)
    {
        const Book *book = &simulation->books[idx];
        int owner_id = simulation->owner_ids[idx];
        int reserved_id = simulation->reserved_reader_ids[idx];
        SOFT_ASSERT(book->id > 0 && memchr(book->name, '\0', sizeof book->name) != NULL, "Некорректная книга", false);
        SOFT_ASSERT(find_book_by_id(simulation->books, idx, book->id) == BOOK_INDEX_NOT_FOUND,
            "Идентификаторы книг повторяются", false);
        SOFT_ASSERT(owner_id == NO_READER || owner_id > 0, "Некорректный идентификатор владельца", false);
        SOFT_ASSERT(reserved_id == NO_READER || reserved_id > 0, "Некорректный идентификатор резерва", false);
        SOFT_ASSERT(
            owner_id == NO_READER || reserved_id == NO_READER, "Книга одновременно выдана и зарезервирована", false);
        SOFT_ASSERT(reserved_id == NO_READER ? simulation->reservation_days[idx] == 0
                                             : simulation->reservation_days[idx] >= FIRST_SIMULATION_DAY,
            "День резервирования не согласован с резервом", false);
        bool owner_found = owner_id == NO_READER;
        bool reserved_found = reserved_id == NO_READER;
        for (int jdx = 0; jdx < simulation->reader_count; ++jdx)
        {
            const Reader *reader = &simulation->readers[jdx];
            if (reader->id == owner_id)
            {
                SOFT_ASSERT(reader_has_book(reader, book->id), "У владельца отсутствует выданная книга", false);
                owner_found = true;
            }
            if (reader->id == reserved_id)
            {
                SOFT_ASSERT(!reader_has_book(reader, book->id), "Зарезервированная книга уже есть у читателя", false);
                reserved_found = true;
            }
        }
        SOFT_ASSERT(owner_found && reserved_found, "Владелец или читатель с резервом не найден", false);
    }
    for (int idx = 0; idx < simulation->request_count; ++idx)
    {
        const Request *request = &simulation->requests[idx];
        SOFT_ASSERT(request->reader_id > 0 && request->request_day >= FIRST_SIMULATION_DAY,
            "Некорректные данные заявки", false);
        SOFT_ASSERT(
            find_book_by_id(simulation->books, simulation->book_count, request->book_id) != BOOK_INDEX_NOT_FOUND,
            "В заявке указана неизвестная книга", false);
        bool reader_found = false;
        for (int jdx = 0; jdx < simulation->reader_count; ++jdx)
        {
            if (simulation->readers[jdx].id == request->reader_id)
            {
                reader_found = true;
            }
        }
        SOFT_ASSERT(reader_found, "Читатель из заявки не найден", false);
        for (int jdx = 0; jdx < idx; ++jdx)
        {
            SOFT_ASSERT(!request->is_active || !simulation->requests[jdx].is_active ||
                            request->reader_id != simulation->requests[jdx].reader_id ||
                            request->book_id != simulation->requests[jdx].book_id,
                "Повторная активная заявка читателя на одну книгу", false);
        }
    }

    return true;
}

#if !defined(LIBRARY_PROCESS_4_5) && !defined(LIBRARY_PROCESS_6)
bool simulation_run(Simulation *simulation, int total_days)
{
    if (!simulation_validate(simulation, total_days))
    {
        return false;
    }

    pthread_t librarian;
    pthread_t readers[MAX_READERS];
    ReaderThreadArgs args[MAX_READERS];
    int created_readers = 0;
    bool created_librarian = false;
    bool success = false;
    bool signals_blocked = false;
    bool stop_workers = false;
    sigset_t stop_signals;
    sigset_t previous_mask;

    int error = pthread_mutex_init(&simulation->mutex, NULL);
    if (error != 0)
    {
        fprintf(stderr, "pthread_mutex_init: %s\n", strerror(error));
        return false;
    }

    // запоминаем только успешно созданные семафоры
    sem_t *semaphores[MAX_READERS + 2] = {&simulation->librarian_start, &simulation->phase_done};
    for (int idx = 0; idx < simulation->reader_count; ++idx)
    {
        semaphores[idx + 2] = &simulation->reader_start[idx];
    }
    int initialized = 0;
    do
    {
        for (; initialized < simulation->reader_count + 2; ++initialized)
        {
            if (sem_init(semaphores[initialized], 0, 0) != 0)
            {
                perror("sem_init");
                break;
            }
        }

        if (initialized != simulation->reader_count + 2)
        {
            break;
        }

        simulation->finished = false;

        // новые потоки наследуют маску, сигналы получает главный поток
        sigemptyset(&stop_signals);
        sigaddset(&stop_signals, SIGINT);
        sigaddset(&stop_signals, SIGTERM);
        error = pthread_sigmask(SIG_BLOCK, &stop_signals, &previous_mask);
        if (error != 0)
        {
            fprintf(stderr, "pthread_sigmask: %s\n", strerror(error));
            break;
        }
        signals_blocked = true;
        stop_workers = true;
        error = pthread_create(&librarian, NULL, librarian_thread, simulation);
        if (error != 0)
        {
            fprintf(stderr, "pthread_create (библиотекарь): %s\n", strerror(error));
            break;
        }
        created_librarian = true;
        for (int idx = 0; idx < simulation->reader_count; ++idx)
        {
            args[idx] = (ReaderThreadArgs){simulation, idx};
            error = pthread_create(&readers[idx], NULL, reader_thread, &args[idx]);
            if (error != 0)
            {
                fprintf(stderr, "pthread_create (читатель): %s\n", strerror(error));
                break;
            }
            ++created_readers;
        }
        if (created_readers != simulation->reader_count)
        {
            break;
        }
        check_pthread(pthread_sigmask(SIG_SETMASK, &previous_mask, NULL), "pthread_sigmask");
        signals_blocked = false;

        // запускаем библиотеку
        printf("Библиотека НАЧАЛА РАБОТАТЬ!. Количество дней работы: %d\n", total_days);
        logger_write("Начало работы библиотеки: %d дней, срок чтения %d\n", total_days, simulation->reading_days);
        for (int day = FIRST_SIMULATION_DAY; day <= total_days; ++day)
        {
            if (stop_requested)
            {
                break;
            }
            check_pthread(pthread_mutex_lock(&simulation->mutex), "pthread_mutex_lock");
            simulation->current_day = day;
            check_pthread(pthread_mutex_unlock(&simulation->mutex), "pthread_mutex_unlock");
            printf("\nДень %d: начало\n", day);

            // сначала библиотекарь обрабатывает все возвраты
            post_semaphore(&simulation->librarian_start);
            wait_semaphore(&simulation->phase_done);

            // запускаем читателей и ждём окончания всех посещений
            for (int idx = 0; idx < simulation->reader_count; ++idx)
            {
                post_semaphore(&simulation->reader_start[idx]);
            }
            for (int idx = 0; idx < simulation->reader_count; ++idx)
            {
                wait_semaphore(&simulation->phase_done);
            }
            printf("День %d: завершён\n", day);
        }
        success = true;
    } while (false);

    if (stop_workers)
    {
        if (signals_blocked)
        {
            check_pthread(pthread_sigmask(SIG_SETMASK, &previous_mask, NULL), "pthread_sigmask");
        }
        // завершаем все созданные потоки, включая частичный запуск
        check_pthread(pthread_mutex_lock(&simulation->mutex), "pthread_mutex_lock");
        simulation->finished = true;
        check_pthread(pthread_mutex_unlock(&simulation->mutex), "pthread_mutex_unlock");
        if (created_librarian)
        {
            post_semaphore(&simulation->librarian_start);
        }
        for (int idx = 0; idx < created_readers; ++idx)
        {
            post_semaphore(&simulation->reader_start[idx]);
        }
        if (created_librarian)
        {
            check_pthread(pthread_join(librarian, NULL), "pthread_join (библиотекарь)");
        }
        for (int idx = 0; idx < created_readers; ++idx)
        {
            check_pthread(pthread_join(readers[idx], NULL), "pthread_join (читатель)");
        }
    }

    for (int idx = 0; idx < initialized; ++idx)
    {
        if (sem_destroy(semaphores[idx]) != 0)
        {
            perror("sem_destroy");
            success = false;
        }
    }
    error = pthread_mutex_destroy(&simulation->mutex);
    if (error != 0)
    {
        fprintf(stderr, "pthread_mutex_destroy: %s\n", strerror(error));
        success = false;
    }
    if (!success)
    {
        return false;
    }

    simulation_print_results(simulation);
    return true;
}
#endif

void simulation_print_results(Simulation *simulation)
{
    SOFT_ASSERT_VOID(simulation != NULL, "Указатель на симуляцию = NULL");
    SOFT_ASSERT_VOID(simulation->book_count > 0 && simulation->book_count <= MAX_BOOKS &&
                         simulation->reader_count > 0 && simulation->reader_count <= MAX_READERS &&
                         simulation->request_count >= 0 && simulation->request_count <= MAX_REQUESTS,
        "Размеры массивов итогового состояния вне допустимых границ");

    if (stop_requested)
    {
        printf("Получен сигнал завершения. Моделирование остановлено.\n");
        logger_write("Получен сигнал завершения. Моделирование остановлено.\n");
    }
    print_unfulfilled_requests(simulation);
    print_unreceived_reservations(simulation);
    printf("\nБиблиотека В.С.Ё.\n");
    logger_write("Завершение работы библиотеки\n");
    statistics_print(&simulation->statistics);
}

void simulation_process_returns(Simulation *simulation, int day)
{
    SOFT_ASSERT_VOID(simulation != NULL, "Указатель на симуляцию = NULL");
    SOFT_ASSERT_VOID(
        simulation->request_count >= 0 && simulation->request_count <= MAX_REQUESTS, "Количество заявок вне массива");
    SOFT_ASSERT_VOID(simulation->issue_strategy == ISSUE_FIFO || simulation->issue_strategy == ISSUE_LIFO,
        "Неизвестная стратегия выдачи");
    returning(simulation, day);
}

void simulation_process_reader(Simulation *simulation, int reader_index, int day)
{
    SOFT_ASSERT_VOID(simulation != NULL, "Указатель на симуляцию = NULL");
    SOFT_ASSERT_VOID(simulation->reader_count > 0 && simulation->reader_count <= MAX_READERS,
        "Количество читателей выходит за границы массива");
    SOFT_ASSERT_VOID(
        reader_index >= 0 && reader_index < simulation->reader_count, "Индекс читателя выходит за границы массива");
    SOFT_ASSERT_VOID(day >= FIRST_SIMULATION_DAY, "Некорректный день симуляции");

    Reader *reader = &simulation->readers[reader_index];
    SOFT_ASSERT_VOID(
        reader->id > 0 && memchr(reader->name, '\0', sizeof reader->name) != NULL, "Некорректный читатель");
    SOFT_ASSERT_VOID(reader->owned_count >= 0 && reader->owned_count <= READER_BOOK_CAPACITY &&
                         reader->wanted_count >= 0 && reader->wanted_count <= MAX_WANTED_BOOKS &&
                         reader->visit_count >= 0 && reader->visit_count <= READER_VISIT_CAPACITY,
        "Размеры массивов читателя вне допустимых границ");
    SOFT_ASSERT_VOID(
        simulation->request_rule == REQUEST_ON_UNAVAILABLE || simulation->request_rule == REQUEST_ALWAYS_WAIT,
        "Неизвестное правило формирования заявок");
    if (is_reader_visit_on_said_day(reader, day))
    {
        SOFT_ASSERT_VOID(simulation->statistics.reader_visits >= 0 && simulation->statistics.reader_visits < INT_MAX,
            "Счётчик посещений вне допустимого диапазона");
        printf("  Приход читателя: %s (id %d)\n", reader->name, reader->id);
        ++simulation->statistics.reader_visits;
        logger_write("Приход читателя: %s (id %d), день %d\n", reader->name, reader->id, day);

        // обрабатываем его резервы, потом обычные желания
        reservation(simulation, reader, day);
        wishes(simulation, reader, day);
    }
}
