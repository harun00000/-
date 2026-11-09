# ИДЗ №3 - Сельская библиотека

Реализация ИДЗ №3 на C11, основанная на проекте ИДЗ №2, вариант 18. [Отчёт](ОТЧЁТ.md) · [Требования](i-tasks-req-03.pdf).

## Что изменилось

- `Reader` и `Librarian` теперь работают как отдельные процессы.
- В версиях 4-5 и 6 один родительский процесс управляет модельными днями и дочерними процессами.
- В версиях 4-5, 6 и 7-8 общее состояние библиотеки хранится в POSIX shared memory.
- Для версии на 4-5 баллов используются неименованные POSIX-семафоры в shared memory.
- Для версии на 6 баллов используются именованные POSIX-семафоры.
- В версии на 7-8 баллов Coordinator, Librarian и Reader запускаются как независимые программы с общей `--session`. Используются POSIX shared memory и именованные POSIX-семафоры.
- В финальной версии выше 8 баллов независимые процессы используют POSIX message queues и именованные POSIX-семафоры, без shared memory.
- Предметная логика, config, CLI, логирование и статистика предыдущей версии сохранены.
- SIGINT/SIGTERM корректно останавливают моделирование с завершением участников и удалением IPC-ресурсов.

## Сборка и запуск

Нужны Linux, CMake и компилятор C11 с поддержкой POSIX shared memory, message queues и семафоров.

Сборка:

```bash
cmake -S . -B build
cmake --build build
```

Версия на 4-5 баллов:

```bash
./run4_5.sh
```

Версия на 6 баллов:

```bash
./run6.sh
```

Скрипты по умолчанию загружают `config.txt` из корня проекта.

Или вручную:

```bash
./build/library_4_5
./build/library_6
```

При прямом запуске без `-c` используются встроенные данные. Чтобы загрузить конфигурацию, укажите `-c config.txt`.

## Версия на 7-8 баллов

Демонстрационный запуск всех участников:

```bash
./run7_8.sh -c config.txt -d 20 -r 5
```

Для независимого запуска выполните каждую команду в отдельном терминале из каталога проекта. Сначала запустите Coordinator и дождитесь сообщения о готовности сессии:

```bash
./build/library_7_8_coordinator --session demo -c config.txt
./build/library_7_8_librarian --session demo
./build/library_7_8_reader --session demo --reader 0
./build/library_7_8_reader --session demo --reader 1
./build/library_7_8_reader --session demo --reader 2
```

Индексы Reader начинаются с нуля; их количество определяется config. Coordinator ждёт всех участников перед первым днём. Для остановки отправьте SIGINT или SIGTERM Coordinator. Имя session: от 1 до 40 символов `A-Z`, `a-z`, `0-9`, `_`, `-`.

## Финальная версия выше 8 баллов

```bash
./run10.sh -c config.txt
./run10.sh -c config.txt -d 20 --delay-ms 200 --print-ipc
```

Для независимого запуска используйте отдельные терминалы. Сначала дождитесь готовности Coordinator:

```bash
./build/library_10_coordinator --session demo -c config.txt --print-ipc
./build/library_10_librarian --session demo
./build/library_10_reader --session demo --reader 0
./build/library_10_reader --session demo --reader 1
./build/library_10_reader --session demo --reader 2
```

Количество Reader берётся из config. `--delay-ms N` задаёт только задержку отображения между днями, по умолчанию `0`. `--print-ipc` один раз показывает имена IPC. Эти опции доступны Coordinator.

## Параметры

- `-d / --days N` - количество модельных дней.
- `-r / --reading-days N` - срок чтения книги.
- `-c / --config FILE` - файл конфигурации.
- `-h / --help` - справка.

Config поддерживает `reading_days`, `strategy=fifo/lifo`, `request_rule=on_unavailable/always_wait`, записи `book` и `reader`, необязательный `total_days` (по умолчанию 13). Пример - [config.txt](config.txt). Параметры CLI имеют приоритет.

FIFO выбирает раннюю заявку, LIFO - позднюю; `on_unavailable` разрешает брать доступные книги, `always_wait` откладывает обычные выдачи при наличии недоступной желаемой книги. Свои резервы обрабатываются первыми.

```bash
./run4_5.sh -c config.txt -d 20 -r 5
./run6.sh -c config.txt -d 20 -r 5
```

## Тесты

Старая pthread-версия:

```bash
./test.sh
```

Версия на 4-5 баллов:

```bash
./test4_5.sh
```

Версия на 6 баллов:

```bash
./test6.sh
```

Версия на 7-8 баллов:

```bash
./test7_8.sh
```

Финальная версия:

```bash
./test10.sh
```

Проверяются обычные сценарии, FIFO/LIFO, `always_wait`, `on_unavailable`, CLI-параметры, максимальное число читателей и завершение по сигналам. Проверки используют Bash, `grep` и `timeout`.

## Реализации

```text
4-5  -> shared memory + unnamed semaphore
6    -> shared memory + named semaphore
7-8  -> независимые процессы + shared memory + named semaphore
8-10   -> независимые процессы + POSIX message queues + named semaphore