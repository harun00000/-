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

echo "Tests passed: $passed/3"
if [ "$passed" -ne 3 ]
then
    exit 1
fi
