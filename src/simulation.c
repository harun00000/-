#define _POSIX_C_SOURCE 200809L
#include "simulation.h"
#include "main_utils.h"
#include "check.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

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
     const Reader readers[], int reader_count, int reading_days, const Librarian *librarian, IssueStrategy issue_strategy, RequestRule request_rule){
    SOFT_ASSERT_VOID(simulation != NULL, "Указатель на симуляцию = NULL");
    SOFT_ASSERT_VOID(books != NULL, "Указатель на массив книг = NULL");
    SOFT_ASSERT_VOID(readers != NULL, "Указатель на массив читателей = NULL");
    SOFT_ASSERT_VOID(book_count > 0 && book_count <= MAX_BOOKS, "Количество книг <= 0 или выходит за допустимые границы");
    SOFT_ASSERT_VOID(reader_count > 0 && reader_count <= MAX_READERS, 
        "Количество читателей <= 0 или выходит за допустимые границы");
    SOFT_ASSERT_VOID(reading_days > 0, "Срок чтения <= 0");
    SOFT_ASSERT_VOID(librarian != NULL, "Указатель на библиотекаря = NULL");
    SOFT_ASSERT_VOID(librarian->id > 0, "Идентификатор библиотекаря <= 0");
    SOFT_ASSERT_VOID(issue_strategy == ISSUE_FIFO || issue_strategy == ISSUE_LIFO, "Неизвестная стратегия выдачи");
    SOFT_ASSERT_VOID(request_rule == REQUEST_ON_UNAVAILABLE || request_rule == REQUEST_ALWAYS_WAIT,
        "Неизвестное правило формирования заявок");

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
    statistics_initialization(&simulation->statistics);
    simulation->librarian = *librarian;
    simulation->book_count = book_count;
    simulation->reader_count = reader_count;
    simulation->reading_days = reading_days;
    simulation->issue_strategy = issue_strategy;
    simulation->request_rule = request_rule;


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
            ++simulation->statistics.notifications_sent;
        }
    }
}

static void reserve_after_return(Simulation *simulation, int book_index, int day){
    SOFT_ASSERT_VOID(simulation != NULL, "Указатель на симуляцию = NULL");
    SOFT_ASSERT_VOID(book_index >= 0 && book_index < simulation->book_count, "Индекс книги выходит за границы массива");

    // находим книгу и первую активную заявку на неё
    const Book *book = &simulation->books[book_index];
    notify_waiting_readers(simulation, book);
    int first = request_find(simulation->requests, simulation->request_count, book->id, simulation->issue_strategy);

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
    ++simulation->statistics.requests_completed;
    simulation->reserved_reader_ids[book_index] = reader->id;
    simulation->reservation_days[book_index] = day;

    // выводим информацию в консоль
    printf("Выбрана заявка №%d (день %d), читатель %s. Заявка закрыта\n", first + 1, request->request_day, reader->name);
    printf("Книга %s зарезервирована за читателем %s; владельца нет\n", book->name, reader->name);
    logger_write("Резервирование: книга %s, читатель %s, день %d\n", book->name, reader->name, day);
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
            ++simulation->statistics.books_returned;
            simulation->owner_ids[index] = NO_READER;
            printf("  Возврат: %s возвращает книгу «%s».\n", reader->name, simulation->books[index].name);
            logger_write("Возврат: %s возвращает книгу %s, день %d\n", reader->name, simulation->books[index].name, day);
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
    ++simulation->statistics.books_given;

    printf("  %s: %s получает книгу %s. День возврата: %d.%s\n", reserved_for == NO_READER ? 
        "Выдача свободной книги" : "Выдача зарезервированной книги", reader->name, book->name, return_day,
        reserved_for == NO_READER ? "" : " Резервирование снято");
    logger_write("Выдача: %s получает книгу %s, день %d, день возврата %d\n", reader->name, book->name, day, return_day);
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
    bool wait_for_books = false;
    if (simulation->request_rule == REQUEST_ALWAYS_WAIT)
    {
        // проверяем весь список до выдачи обычных желаемых книг
        for (int idx = 0; idx < reader->wanted_count; ++idx)
        {
            int index = find_book_by_id(simulation->books, simulation->book_count, reader->wanted_books[idx]);
            SOFT_ASSERT_VOID(index >= 0, "У читателя указан неизвестный айди книги");

            if ((simulation->owner_ids[index] != NO_READER && simulation->owner_ids[index] != reader->id) ||
                (simulation->reserved_reader_ids[index] != NO_READER && simulation->reserved_reader_ids[index] != reader->id))
            {
                wait_for_books = true;
                break;
            }
        }
    }

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

        if (wait_for_books && simulation->owner_ids[index] == NO_READER && simulation->reserved_reader_ids[index] == NO_READER)
        {
            ++idx;
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
            logger_write("Книга недоступна: %s находится у читателя %s, запросил %s\n", book->name, owner->name, reader->name);
        } else
        {
            Reader *reserved = find_reader(simulation, simulation->reserved_reader_ids[index]);
            printf("Книга недоступна: зарезервирована за читателем %s\n", reserved->name);
            logger_write("Книга недоступна: %s зарезервирована за читателем %s, запросил %s\n", book->name, reserved->name, reader->name);
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
            ++simulation->statistics.requests_created;
            logger_write("Создана заявка №%d: %s, книга %s, день %d\n", simulation->request_count, reader->name, book->name, day);
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

typedef struct ReaderThreadArgs{
    Simulation *simulation;
    int reader_index;
} ReaderThreadArgs;

static void check_pthread(int error, const char *operation){
    if (error != 0)
    {
        fprintf(stderr, "%s: %s\n", operation, strerror(error));
        exit(EXIT_FAILURE);
    }
}

static void wait_semaphore(sem_t *semaphore){
    while (sem_wait(semaphore) != 0)
    {
        if (errno != EINTR)
        {
            perror("sem_wait");
            exit(EXIT_FAILURE);
        }
    }
}

static void post_semaphore(sem_t *semaphore){
    if (sem_post(semaphore) != 0)
    {
        perror("sem_post");
        exit(EXIT_FAILURE);
    }
}

static void *librarian_thread(void *arg){
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

static void *reader_thread(void *arg){
    ReaderThreadArgs *args = arg;
    Simulation *simulation = args->simulation;
    Reader *reader = &simulation->readers[args->reader_index];
    while (true)
    {
        wait_semaphore(&simulation->reader_start[args->reader_index]);
        check_pthread(pthread_mutex_lock(&simulation->mutex), "pthread_mutex_lock");
        if (simulation->finished)
        {
            check_pthread(pthread_mutex_unlock(&simulation->mutex), "pthread_mutex_unlock");
            break;
        }

        int day = simulation->current_day;
        if (is_reader_visit_on_said_day(reader, day))
        {
            printf("  Приход читателя: %s (id %d)\n", reader->name, reader->id);
            ++simulation->statistics.reader_visits;
            logger_write("Приход читателя: %s (id %d), день %d\n", reader->name, reader->id, day);

            // обрабатываем его резервы, потом обычные желания
            reservation(simulation, reader, day);
            wishes(simulation, reader, day);
        }
        check_pthread(pthread_mutex_unlock(&simulation->mutex), "pthread_mutex_unlock");
        post_semaphore(&simulation->phase_done);
    }
    return NULL;
}

bool simulation_run(Simulation *simulation, int total_days){
    SOFT_ASSERT(simulation != NULL, "Указатель на симуляцию = NULL", false);
    SOFT_ASSERT(simulation->book_count > 0 && simulation->book_count <= MAX_BOOKS, 
        "Количество книг симуляции выходит за допустимые границы", false);
    SOFT_ASSERT(simulation->reader_count > 0 && simulation->reader_count <= MAX_READERS, 
        "Количество читателей симуляции выходит за допустимые границы", false);
    SOFT_ASSERT(simulation->request_count >= 0 && simulation->request_count <= MAX_REQUESTS, 
        "Количество заявок симуляции выходит за допустимые границы", false);
    SOFT_ASSERT(simulation->reading_days > 0, "Срок чтения должен быть положительным", false);
    SOFT_ASSERT(total_days >= 0, "Количество дней симуляции не должно быть отрицательным", false);

    pthread_t librarian;
    pthread_t readers[MAX_READERS];
    ReaderThreadArgs args[MAX_READERS];
    int created_readers = 0;
    bool created_librarian = false;
    bool success = false;
    bool signals_blocked = false;
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
    for (; initialized < simulation->reader_count + 2; ++initialized)
    {
        if (sem_init(semaphores[initialized], 0, 0) != 0)
        {
            perror("sem_init");
            goto destroy_semaphores;
        }
    }

    simulation->finished = false;

    // новые потоки наследуют маску, сигналы будет получать главный поток
    sigemptyset(&stop_signals);
    sigaddset(&stop_signals, SIGINT);
    sigaddset(&stop_signals, SIGTERM);
    error = pthread_sigmask(SIG_BLOCK, &stop_signals, &previous_mask);
    if (error != 0)
    {
        fprintf(stderr, "pthread_sigmask: %s\n", strerror(error));
        goto destroy_semaphores;
    }
    signals_blocked = true;
    error = pthread_create(&librarian, NULL, librarian_thread, simulation);
    if (error != 0)
    {
        fprintf(stderr, "pthread_create (библиотекарь): %s\n", strerror(error));
        goto stop_threads;
    }
    created_librarian = true;
    for (int idx = 0; idx < simulation->reader_count; ++idx)
    {
        args[idx] = (ReaderThreadArgs){simulation, idx};
        error = pthread_create(&readers[idx], NULL, reader_thread, &args[idx]);
        if (error != 0)
        {
            fprintf(stderr, "pthread_create (читатель): %s\n", strerror(error));
            goto stop_threads;
        }
        ++created_readers;
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

        // будим каждого читателя один раз и ждём окончания посещений
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

stop_threads:
    if (signals_blocked)
    {
        check_pthread(pthread_sigmask(SIG_SETMASK, &previous_mask, NULL), "pthread_sigmask");
    }
    // завершаем и те потоки, которые успели создаться при ошибке
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

destroy_semaphores:
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
    return true;
}
