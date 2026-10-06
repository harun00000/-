# Сельская библиотека

## Быстрый запуск

```bash
chmod +x run.sh
./run.sh
```

## Примеры запуска с параметрами

```bash
./build/library_sim -d 20 -r 5
```

```bash
./build/library_sim --config config.txt
```

```bash
./build/library_sim --config config.txt -d 30 -r 5
```

## Формат конфигурационного файла

Пример `config.txt`:

```text
reading_days=3
total_days=13

book=1;Курочка ряба
book=2;Убийство в Реутовском экспрессе. 2026г.

reader=1;Даниил Подлягин;1,2;1,5
reader=2;Никита Жилин;1;2,3,6
reader=3;Сергей Крылосов;1;2,4,10
```

Формат читателя:

```text
reader=id;имя;желаемые_книги;дни_посещений
```

## Doxygen

Сгенерировать документацию:

```bash
doxygen Doxyfile
```

Открыть документацию:

```bash
google-chrome docs/html/index.html
```