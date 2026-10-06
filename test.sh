#!/usr/bin/env bash

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)" || exit 1
cd -- "$project_dir" || exit 1

passed=0
for config in normal_config.txt boundary_one_book.txt boundary_one_day.txt
do
    if ./build/library_sim --config "tests/$config" > /dev/null
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

echo "Tests passed: $passed/4"
if [ "$passed" -ne 4 ]
then
    exit 1
fi
