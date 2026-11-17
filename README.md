# ИДЗ №4 - Сельская библиотека

Реализация ИДЗ №4 на C11, основанная на проекте ИДЗ №3, вариант 18. [Отчёт](ОТЧЁТ.md) · [Требования](i-tasks-req-04.pdf).

## Что изменилось

- `Server`, `Librarian` и каждый `Reader` работают как независимые программы и взаимодействуют по TCP.
- `Server` хранит общее состояние библиотеки и управляет модельными днями.
- В версии на 4-5 баллов рабочие участники показывают только свою часть взаимодействия.
- В версии на 6-7 баллов отдельный `Observer` показывает общий поток событий.
- В версии на 8 баллов поддерживаются несколько `Observer`, их отключение и повторное подключение без остановки модели.
- В версии выше 8 баллов `Reader` и `Librarian` могут повторно подключаться с сохранением прогресса.
- Уже применённая предметная операция после reconnect не выполняется повторно.
- SIGINT/SIGTERM сервера корректно завершают подключённых клиентов.
- Предметная логика, config, CLI и статистика предыдущей версии сохранены.
- Старые реализации ИДЗ №3 также сохранены.

## Сборка и запуск

Нужны Linux, CMake и компилятор C11.

```bash
cmake -S . -B build
cmake --build build
```

Базовый запуск:

```bash
./scripts/run4_5_tcp.sh
```

С параметрами:

```bash
./scripts/run4_5_tcp.sh -c config.txt -d 20 -r 5 --port 5051
```

Скрипт запускает server, Librarian и нужное число Reader. Observer запускается отдельно.

## Независимый запуск

Каждая программа запускается в отдельном терминале.

Сначала server:

```bash
./build/library_server --host 127.0.0.1 --port 5050 -c config.txt
```

Затем Librarian и Reader:

```bash
./build/library_librarian_client --host 127.0.0.1 --port 5050

./build/library_reader_client --host 127.0.0.1 --port 5050 --reader 0
./build/library_reader_client --host 127.0.0.1 --port 5050 --reader 1
./build/library_reader_client --host 127.0.0.1 --port 5050 --reader 2
```

Индексы Reader начинаются с нуля и соответствуют порядку записей в config. Server ждёт Librarian и всех Reader перед первым днём.

Без `-c` используются встроенные данные. Config читает только server; Reader получает личное расписание по сети.

## Observer: уровни 6-7 и 8

```bash
./build/library_observer_client --host 127.0.0.1 --port 5050
```

Observer показывает общий поток событий модели.

Можно запускать несколько Observer одновременно. Их можно отключать и подключать снова без остановки модели. Новый Observer получает события с момента подключения.

## Версия выше 8 баллов

Для восстановления отключённого Reader достаточно снова запустить его с тем же индексом:

```bash
./build/library_reader_client --host 127.0.0.1 --port 5050 --reader 1
```

Для Librarian:

```bash
./build/library_librarian_client --host 127.0.0.1 --port 5050
```

Server сохраняет состояние модели и прогресс роли отдельно от TCP-соединения. После reconnect клиент продолжает текущую фазу без повторного применения уже выполненной операции.

Если нужный участник отключён, модель ждёт его возвращения, но server продолжает обслуживать сеть и Observer.

SIGINT/SIGTERM сервера корректно завершают подключённых клиентов.

## Запуск по сети

На компьютере с server:

```bash
./build/library_server --host 0.0.0.0 --port 5050 -c config.txt
```

На других Linux-компьютерах:

```bash
./build/library_observer_client --host 192.168.1.10 --port 5050
./build/library_librarian_client --host 192.168.1.10 --port 5050
./build/library_reader_client --host 192.168.1.10 --port 5050 --reader 0
```

Адрес `192.168.1.10` нужно заменить на реальный IPv4 server.

## Параметры

Для всех сетевых программ:

- `--host IPv4` - адрес server;
- `--port PORT` - TCP-порт;
- `-h / --help` - справка.

Для Reader:

- `--reader INDEX` - индекс читателя.

Для server:

- `-d / --days N` - число модельных дней;
- `-r / --reading-days N` - срок чтения;
- `-c / --config FILE` - файл конфигурации.

Пример:

```bash
./build/library_server --host 127.0.0.1 --port 5050 -c config.txt -d 20 -r 5
```

Config поддерживает `reading_days`, `strategy=fifo/lifo`, `request_rule=on_unavailable/always_wait`, записи `book` и `reader`, необязательный `total_days`.

FIFO выбирает раннюю заявку, LIFO - позднюю. `on_unavailable` разрешает брать доступные книги, `always_wait` откладывает обычные выдачи при наличии недоступной желаемой книги.

## Тесты

Основной набор:

```bash
./scripts/test4_5_tcp.sh
```

Проверяются:

- обычный запуск;
- FIFO/LIFO;
- правила заявок;
- CLI;
- несколько Observer;
- disconnect/reconnect Observer;
- reconnect Reader/Librarian;
- сохранение прогресса без повторных операций;
- SIGINT/SIGTERM и cleanup.

Результат последнего полного прогона:

```text
18/18 tests passed
```

Документация Doxygen:

```bash
doxygen Doxyfile
```

После сборки HTML-документация будет находиться в каталоге `docs/html/`. Для просмотра откройте:

```bash
xdg-open docs/html/index.html
```

## Реализации

```text
4-5  -> TCP, независимые server и рабочие clients
6-7  -> отдельный Observer с общим потоком событий
8    -> несколько Observer + disconnect/reconnect
>8   -> reconnect Reader/Librarian с сохранением прогресса
       + корректное завершение clients при остановке server
```