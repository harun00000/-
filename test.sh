#!/usr/bin/env bash

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)" || exit 1
cd -- "$project_dir" || exit 1

binary="${SIM_BINARY:-./build/library_sim}"
test_dir=$(mktemp -d ./build/tests.XXXXXX) || exit 1
trap 'rm -rf -- "$test_dir"' EXIT
passed=0
total=0

run_test(){
    local name="$1" days="$2"
    shift 2
    total=$((total + 1))
    if timeout 10s "$binary" "$@" > "$test_dir/output" 2> "$test_dir/errors" &&
        [ ! -s "$test_dir/errors" ] &&
        grep -Fxq "Библиотека НАЧАЛА РАБОТАТЬ!. Количество дней работы: $days" "$test_dir/output" &&
        grep -Fxq "День $days: завершён" "$test_dir/output" &&
        grep -Fxq 'Библиотека В.С.Ё.' "$test_dir/output" &&
        grep -Fxq 'Статистика:' "$test_dir/output"
    then
        local output
        output=$(cat "$test_dir/output")
        case "$name" in
            normal_config.txt|default)
                grep -Fq 'Приходов читателей: 8' <<< "$output" &&
                grep -Fq 'Создано заявок: 2' <<< "$output" &&
                grep -Fq 'Выполнено заявок: 2' <<< "$output" &&
                grep -Fq 'Возврат:' <<< "$output" || return 1 ;;
            boundary_one_book.txt)
                grep -Fq 'Создано заявок: 2' <<< "$output" &&
                grep -Fq 'Возврат:' <<< "$output" &&
                grep -Fq 'Заявка закрыта' <<< "$output" &&
                grep -Fq 'Выдача зарезервированной книги:' <<< "$output" || return 1 ;;
            boundary_one_day.txt)
                grep -Fq 'Выдано книг: 1' <<< "$output" &&
                grep -Fq 'Возвращено книг: 0' <<< "$output" || return 1 ;;
            strategy_lifo.txt)
                grep -Fq 'Выбрана заявка №2 (день 3), читатель Виктор. Заявка закрыта' <<< "$output" &&
                grep -Fq 'Книга Курочка ряба зарезервирована за читателем Виктор; владельца нет' <<< "$output" || return 1 ;;
            strategy_fifo)
                grep -Fq 'Выбрана заявка №1 (день 2), читатель Борис. Заявка закрыта' <<< "$output" &&
                grep -Fq 'Книга Курочка ряба зарезервирована за читателем Борис; владельца нет' <<< "$output" || return 1 ;;
            request_always_wait.txt)
                grep -Fq 'Создана заявка №1: Борис, книга Курочка ряба, день 2' <<< "$output" &&
                ! grep -Fq 'Борис получает книгу Колобок' <<< "$output" &&
                ! grep -Fq 'Борис, книга Колобок' <<< "$output" || return 1 ;;
            on_unavailable)
                grep -Fq 'Борис получает книгу Колобок' <<< "$output" &&
                grep -Fq 'Создана заявка №1: Борис, книга Курочка ряба, день 2' <<< "$output" || return 1 ;;
            max_readers)
                grep -Fq 'Приходов читателей: 500' <<< "$output" &&
                grep -Fq 'Создано заявок: 19' <<< "$output" || return 1 ;;
        esac
        echo "PASS: $name"
        passed=$((passed + 1))
        return 0
    fi
    return 1
}

for config in normal_config.txt boundary_one_book.txt boundary_one_day.txt strategy_lifo.txt request_always_wait.txt
do
    days=13
    case "$config" in
        boundary_one_book.txt) days=9 ;;
        boundary_one_day.txt) days=1 ;;
        strategy_lifo.txt) days=4 ;;
        request_always_wait.txt) days=2 ;;
    esac
    if ! run_test "$config" "$days" --config "tests/$config"
    then
        echo "FAIL: $config"
        cat "$test_dir/errors" "$test_dir/output"
    fi
done

# Те же исходные данные с другим правилом и другой стратегией
sed 's/strategy=lifo/strategy=fifo/' tests/strategy_lifo.txt > "$test_dir/fifo.txt"
sed 's/request_rule=always_wait/request_rule=on_unavailable/' tests/request_always_wait.txt > "$test_dir/on_unavailable.txt"
{
    printf 'reading_days=1\ntotal_days=25\nbook=1;Курочка ряба\n'
    for ((reader=1; reader<=20; ++reader))
    do
        printf 'reader=%d;Читатель%d;1;1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25\n' "$reader" "$reader"
    done
} > "$test_dir/max_readers.txt"

run_test strategy_fifo 4 -c "$test_dir/fifo.txt" || echo 'FAIL: strategy_fifo'
run_test on_unavailable 2 -c "$test_dir/on_unavailable.txt" || echo 'FAIL: on_unavailable'
run_test max_readers 25 -c "$test_dir/max_readers.txt" || echo 'FAIL: max_readers'
run_test default 13 || echo 'FAIL: default'

# Ждём начала работы и прерываем длинное моделирование.
total=$((total + 1))
if timeout -k 1s 10s bash -c '
    set -e
    "$1" -c tests/signal_config.txt > "$2/signal_output" 2> "$2/signal_errors" &
    pid=$!
    trap "kill -KILL $pid 2>/dev/null || true; wait $pid 2>/dev/null || true" EXIT
    while ! grep -Fq "День 1: завершён" "$2/signal_output"
    do
        kill -0 "$pid" || exit 1
        sleep 0.01
    done
    kill -TERM "$pid"
    wait "$pid"
    trap - EXIT
    [ ! -s "$2/signal_errors" ]
    grep -Fxq "Получен сигнал завершения. Моделирование остановлено." "$2/signal_output"
    grep -Fxq "Статистика:" "$2/signal_output"
    grep -Fxq "Библиотека В.С.Ё." "$2/signal_output"
' bash "$binary" "$test_dir"
then
    echo 'PASS: signal'
    passed=$((passed + 1))
else
    echo 'FAIL: signal'
    cat "$test_dir/signal_errors" "$test_dir/signal_output"
fi

echo "Tests passed: $passed/$total"
if [ "$passed" -ne "$total" ]
then
    exit 1
fi
