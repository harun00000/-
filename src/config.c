#include "config.h"
#include "check.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { CONFIG_LINE_SIZE = 1000 };

// cтроку в число и проверяем корректность
static bool parse_number(const char *text, int minimum, int *value){
    SOFT_ASSERT(text != NULL, "Указатель на строку = NULL", false);
    SOFT_ASSERT(value != NULL, "Указатель на число = NULL", false);

    char *end;
    errno = 0;
    long number = strtol(text, &end, 10);

    // если строка не число, число не считалось полностью или вышло за границы
    if (end == text || *end != '\0' || errno == ERANGE || number < minimum || number > INT_MAX)
    {
        return false;
    }

    *value = (int)number;
    return true;
}

static bool split_fields(char *text, char *fields[], int count){
    SOFT_ASSERT(text != NULL, "Указатель на строку = NULL", false);
    SOFT_ASSERT(fields != NULL, "Указатель на массив полей = NULL", false);
    SOFT_ASSERT(count > 0, "Количество полей <= 0", false);

    // делим строку на нужное количество частей через ;
    for (int idx = 0; idx < count; ++idx)
    {
        fields[idx] = text;
        char *separator = strchr(text, ';');

        // если это не последнее поле, разделитель обязательно должен быть
        if (idx + 1 < count)
        {
            if (separator == NULL)
            {
                return false;
            }

            *separator = '\0';
            text = separator + 1;
        } else if (separator != NULL)
        {
            return false;
        }

        // пустых полей быть не должно
        if (fields[idx][0] == '\0')
        {
            return false;
        }
    }

    return true;
}

static bool parse_list(char *text, int values[], int capacity, int *count){
    SOFT_ASSERT(text != NULL, "Указатель на строку = NULL", false);
    SOFT_ASSERT(values != NULL, "Указатель на массив значений = NULL", false);
    SOFT_ASSERT(count != NULL, "Указатель на количество элементов = NULL", false);
    SOFT_ASSERT(capacity > 0, "Вместимость массива <= 0", false);

    *count = 0;

    // разбираем список чисел, разделённых запятыми
    while (true)
    {
        char *separator = strchr(text, ',');

        // временно заканчиваем строку на текущем числе
        if (separator != NULL)
        {
            *separator = '\0';
        }

        // чекаем, что число корректное и помещается в массив
        if (*count >= capacity || !parse_number(text, 1, &values[*count]))
        {
            return false;
        }

        ++*count;

        if (separator == NULL)
        {
            return true;
        }

        text = separator + 1;
    }
}

bool config_load(const char *filename, Config *config){
    SOFT_ASSERT(filename != NULL, "Указатель на имя файла = NULL", false);
    SOFT_ASSERT(config != NULL, "Указатель на Config = NULL", false);

    // открываем файл 
    FILE *file = fopen(filename, "r");
    if (file == NULL)
    {
        fprintf(stderr, "Не удалось открыть файл конфигурации: %s\n", filename);
        return false;
    }

    // очищаем Config перед заполнением
    *config = (Config){0};
    config->total_days = DEFAULT_TOTAL_DAYS;
    config->issue_strategy = ISSUE_FIFO;
    config->request_rule = REQUEST_ON_UNAVAILABLE;
    char line[CONFIG_LINE_SIZE];
    int line_number = 0;
    bool has_reading_days = false;
    bool has_total_days = false;
    const char *error = NULL;

    // разбираем построчно
    while (fgets(line, sizeof line, file) != NULL)
    {
        ++line_number;
        size_t length = strlen(line);

        // проверяем, что строка полностью поместилась в буффер
        if (length == sizeof line - 1 && line[length - 1] != '\n')
        {
            error = "слишком длинная строка";
            break;
        }

        // убираем перенос строки и пропускаем пустые строки
        line[strcspn(line, "\r\n")] = '\0';
        if (line[strspn(line, " \t")] == '\0')
        {
            continue;
        }

        // делим строку на ключ и значение по =
        char *value = strchr(line, '=');
        if (value == NULL)
        {
            error = "ожидается запись ключ=значение";
            break;
        }
        *value++ = '\0';

        // читаем параметры для заполнения симуляции
        if (strcmp(line, "reading_days") == 0)
        {
            if (has_reading_days || !parse_number(value, 1, &config->reading_days))
            {
                error = "reading_days должен быть положительным целым числом и задаваться один раз";
                break;
            }
            has_reading_days = true;

        } else if (strcmp(line, "total_days") == 0)
        {
            if (has_total_days || !parse_number(value, 1, &config->total_days))
            {
                error = "total_days должен быть положительным целым числом и задаваться один раз";
                break;
            }
            has_total_days = true;

        } else if (strcmp(line, "strategy") == 0)
        {
            if (strcmp(value, "fifo") == 0)
            {
                config->issue_strategy = ISSUE_FIFO;
            } else if (strcmp(value, "lifo") == 0)
            {
                config->issue_strategy = ISSUE_LIFO;
            } else
            {
                error = "strategy должен быть fifo или lifo";
                break;
            }

        } else if (strcmp(line, "request_rule") == 0)
        {
            if (strcmp(value, "on_unavailable") == 0)
            {
                config->request_rule = REQUEST_ON_UNAVAILABLE;
            } else if (strcmp(value, "always_wait") == 0)
            {
                config->request_rule = REQUEST_ALWAYS_WAIT;
            } else
            {
                error = "request_rule должен быть on_unavailable или always_wait";
                break;
            }

        } else if (strcmp(line, "book") == 0)
        {
            char *fields[2];
            Book book = {0};
            if (config->book_count >= MAX_BOOKS || !split_fields(value, fields, 2) ||
                !parse_number(fields[0], 1, &book.id) || strlen(fields[1]) >= sizeof book.name)
            {
                error = "неверная запись book=id;название, слишком длинное название или превышен MAX_BOOKS";
                break;
            }

            if (find_book_by_id(config->books, config->book_count, book.id) != BOOK_INDEX_NOT_FOUND)
            {
                error = "идентификаторы книг повторяются";
                break;
            }

            strcpy(book.name, fields[1]);
            config->books[config->book_count++] = book;

        } else if (strcmp(line, "reader") == 0)
        {
            char *fields[4];
            Reader reader = {0};
            if (config->reader_count >= MAX_READERS || !split_fields(value, fields, 4) ||
                !parse_number(fields[0], 1, &reader.id) || strlen(fields[1]) >= sizeof reader.name ||
                !parse_list(fields[2], reader.wanted_books, MAX_WANTED_BOOKS, &reader.wanted_count) ||
                reader.wanted_count < MIN_WANTED_BOOKS ||
                !parse_list(fields[3], reader.visit_days, READER_VISIT_CAPACITY, &reader.visit_count))
            {
                error = "неверная запись reader=id;имя;желаемые_книги;дни_посещений или превышены допустимые размеры";
                break;
            }

            for (int idx = 0; idx < config->reader_count; ++idx)
            {
                if (config->readers[idx].id == reader.id)
                {
                    error = "идентификаторы читателей повторяются";
                    break;
                }
            }

            if (error != NULL)
            {
                break;
            }
            strcpy(reader.name, fields[1]);
            config->readers[config->reader_count++] = reader;

        } else
        {
            error = "неизвестный ключ конфигурации";
            break;
        }
    }

     // проверяем ошибки чтения и закрытия файла
    if (error == NULL && ferror(file))
    {
        error = "не удалось прочитать файл";
    }

    int close_result = fclose(file);
    if (error == NULL && close_result != 0)
    {
        error = "не удалось закрыть файл";
    }

    if (error != NULL)
    {
        fprintf(stderr, "Ошибка конфигурации %s, строка %d: %s\n", filename, line_number, error);
        return false;
    }

    // проверяем, что все обязательные данные есть
    if (!has_reading_days || config->book_count == 0 || config->reader_count == 0)
    {
        fprintf(stderr, "Ошибка конфигурации: нужны reading_days, книги и читатели\n");
        return false;
    }

     // проверяем, что все желаемые книги реально есть в библиотеке
    for (int idx = 0; idx < config->reader_count; ++idx)
    {
        const Reader *reader = &config->readers[idx];
        for (int jdx = 0; jdx < reader->wanted_count; ++jdx)
        {
            if (find_book_by_id(config->books, config->book_count, reader->wanted_books[jdx]) == BOOK_INDEX_NOT_FOUND)
            {
                fprintf(stderr, "Ошибка конфигурации: читатель %d выбрал неизвестную книгу %d\n", reader->id, reader->wanted_books[jdx]);
                return false;
            }
        }
    }
    return true;
}
