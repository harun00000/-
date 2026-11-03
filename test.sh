#!/usr/bin/env bash

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)" || exit 1
cd -- "$project_dir" || exit 1

passed=0
for config in normal_config.txt boundary_one_book.txt boundary_one_day.txt
do
    if output=$(./build/library_sim --config "tests/$config") &&
        { [ "$config" != normal_config.txt ] ||
          printf '%s\n' "$output" | grep -Fxq 'Библиотека НАЧАЛА РАБОТАТЬ!. Количество дней работы: 13'; }
    then
        echo "PASS: $config"
        passed=$((passed + 1))
    else
        echo "FAIL: $config"
    fi
done

if output=$(./build/library_sim --config tests/strategy_lifo.txt) &&
    printf '%s\n' "$output" | grep -Fq 'Книга Курочка ряба зарезервирована за читателем Виктор; владельца нет'
then
    echo "PASS: strategy_lifo.txt"
    passed=$((passed + 1))
else
    echo "FAIL: strategy_lifo.txt"
fi

if output=$(./build/library_sim --config tests/request_always_wait.txt) &&
    printf '%s\n' "$output" | grep -Fq 'Создана заявка №1: Борис, книга Курочка ряба, день 2' &&
    ! printf '%s\n' "$output" | grep -Fq 'Борис получает книгу Колобок' &&
    ! printf '%s\n' "$output" | grep -Fq 'Борис, книга Колобок'
then
    echo "PASS: request_always_wait.txt"
    passed=$((passed + 1))
else
    echo "FAIL: request_always_wait.txt"
fi

echo "Tests passed: $passed/5"
if [ "$passed" -ne 5 ]
then
    exit 1
fi
