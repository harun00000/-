#!/usr/bin/env bash

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)" || exit 1
cd -- "$project_dir" || exit 1

binary=./build/library_6
test_dir=$(mktemp -d ./build/tests6.XXXXXX) || exit 1
trap 'rm -rf -- "$test_dir"' EXIT
passed=0
total=0

# каждый запуск ограничен timeout, после выхода проверяем shm и именованные семафоры
run_test(){
    local name="$1" days="$2"
    shift 2
    total=$((total + 1))
    if timeout -k 2s 10s bash -c '
        binary=$1
        pid_file=$2
        shift 2
        "$binary" "$@" &
        pid=$!
        printf "%s\n" "$pid" > "$pid_file"
        wait "$pid"
        result=$?
        [ ! -e "/dev/shm/library_6_$(id -u)_$pid" ] || exit 1
        if compgen -G "/dev/shm/sem.library_6_$(id -u)_${pid}_*" > /dev/null
        then
            echo "После завершения остались именованные семафоры" >&2
            exit 1
        fi
        exit "$result"
    ' bash "$binary" "$test_dir/pid" "$@" > "$test_dir/output" 2> "$test_dir/errors" &&
        [ ! -s "$test_dir/errors" ] &&
        grep -Fxq "Библиотека НАЧАЛА РАБОТАТЬ!. Количество дней работы: $days" "$test_dir/output" &&
        grep -Fxq "День $days: завершён" "$test_dir/output" &&
        ! grep -Fxq "День $((days + 1)): начало" "$test_dir/output" &&
        grep -Fxq 'Библиотека В.С.Ё.' "$test_dir/output" &&
        grep -Fxq 'Статистика:' "$test_dir/output" &&
        grep -Fxq 'Завершение работы библиотеки' library.log &&
        grep -Fxq 'Статистика:' library.log
    then
        local output
        output=$(cat "$test_dir/output")
        case "$name" in
            normal_config|default|root_config|script_default)
                grep -Fq 'Приходов читателей: 8' <<< "$output" &&
                grep -Fq 'Создано заявок: 2' <<< "$output" &&
                grep -Fq 'Выполнено заявок: 2' <<< "$output" &&
                grep -Fq 'Возврат:' <<< "$output" || return 1 ;;
            boundary_one_book)
                grep -Fq 'Создано заявок: 2' <<< "$output" &&
                grep -Fq 'Возврат:' <<< "$output" &&
                grep -Fq 'Заявка закрыта' <<< "$output" &&
                grep -Fq 'уведомляет читателя' <<< "$output" &&
                grep -Fq 'Выдача зарезервированной книги:' <<< "$output" || return 1 ;;
            boundary_one_day)
                grep -Fq 'Выдано книг: 1' <<< "$output" &&
                grep -Fq 'Возвращено книг: 0' <<< "$output" || return 1 ;;
            strategy_lifo)
                grep -Fq 'Выбрана заявка №2 (день 3), читатель Виктор. Заявка закрыта' <<< "$output" &&
                grep -Fq 'Книга Курочка ряба зарезервирована за читателем Виктор; владельца нет' <<< "$output" || return 1 ;;
            strategy_fifo)
                grep -Fq 'Выбрана заявка №1 (день 2), читатель Борис. Заявка закрыта' <<< "$output" &&
                grep -Fq 'Книга Курочка ряба зарезервирована за читателем Борис; владельца нет' <<< "$output" || return 1 ;;
            always_wait)
                grep -Fq 'Создана заявка №1: Борис, книга Курочка ряба, день 2' <<< "$output" &&
                ! grep -Fq 'Борис получает книгу Колобок' <<< "$output" &&
                ! grep -Fq 'Борис, книга Колобок' <<< "$output" || return 1 ;;
            on_unavailable)
                grep -Fq 'Борис получает книгу Колобок' <<< "$output" &&
                grep -Fq 'Создана заявка №1: Борис, книга Курочка ряба, день 2' <<< "$output" || return 1 ;;
            max_readers)
                grep -Fq 'Приходов читателей: 500' <<< "$output" &&
                grep -Fq 'Создано заявок: 19' <<< "$output" || return 1 ;;
            reading_override)
                grep -Fq 'День возврата: 6.' <<< "$output" &&
                grep -Fq 'Возвращено книг: 1' <<< "$output" || return 1 ;;
            script_override)
                grep -Fq 'День возврата: 6.' <<< "$output" || return 1 ;;
        esac
        echo "PASS: $name"
        passed=$((passed + 1))
        return 0
    fi
    return 1
}

report_test(){
    if ! run_test "$@"
    then
        echo "FAIL: $1"
        cat "$test_dir/errors" "$test_dir/output"
    fi
}

report_test normal_config 13 -c tests/normal_config.txt
report_test boundary_one_book 9 -c tests/boundary_one_book.txt
report_test boundary_one_day 1 -c tests/boundary_one_day.txt
report_test strategy_lifo 4 -c tests/strategy_lifo.txt
report_test always_wait 2 -c tests/request_always_wait.txt

sed 's/strategy=lifo/strategy=fifo/' tests/strategy_lifo.txt > "$test_dir/fifo.txt"
sed 's/request_rule=always_wait/request_rule=on_unavailable/' tests/request_always_wait.txt > "$test_dir/on_unavailable.txt"
{
    printf 'reading_days=1\ntotal_days=25\nbook=1;Курочка ряба\n'
    for ((reader=1; reader<=20; ++reader))
    do
        printf 'reader=%d;Читатель%d;1;1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25\n' "$reader" "$reader"
    done
} > "$test_dir/max_readers.txt"

report_test strategy_fifo 4 -c "$test_dir/fifo.txt"
report_test on_unavailable 2 -c "$test_dir/on_unavailable.txt"
report_test max_readers 25 -c "$test_dir/max_readers.txt"
report_test default 13
report_test root_config 13 -c config.txt
report_test days_override 2 -c tests/normal_config.txt -d 2
report_test reading_override 7 -c tests/boundary_one_day.txt -d 7 -r 5

# проверяем запуск скриптом и передачу параметров
binary=./run6.sh
report_test script_default 13
report_test script_override 20 -c config.txt -d 20 -r 5
binary=./build/library_6

# SIGTERM отправляем родителю, SIGINT - всей группе, как при Ctrl+C
# до сигнала проверяем детей, отсутствие рабочих потоков и именованные семафоры
for signal in TERM INT
do
    total=$((total + 1))
    if timeout -k 2s 10s bash -s -- "$binary" "$test_dir" "$signal" <<'SIGNAL_TEST'
        set -e
        binary=$1
        test_dir=$2
        signal=$3
        # без job control фоновый setsid сохраняет pid при exec программы
        set +m
        : > "$test_dir/signal_output"
        : > "$test_dir/signal_errors"
        setsid "$binary" -c tests/signal_config.txt > "$test_dir/signal_output" 2> "$test_dir/signal_errors" &
        pid=$!
        cleanup(){
            kill -KILL -- "-$pid" 2>/dev/null || kill -KILL "$pid" 2>/dev/null || true
            wait "$pid" 2>/dev/null || true
        }
        trap cleanup EXIT
        trap "exit 1" TERM INT

        expected_binary=$(readlink -f "$binary")
        expected_children=3
        children=()
        # ждём exec настоящей программы, создание детей и завершение первого дня
        while true
        do
            if ! kill -0 "$pid" 2>/dev/null
            then
                echo "library_6 (pid=$pid) завершилась до готовности дочерних процессов" >&2
                exit 1
            fi
            if [ "$(readlink "/proc/$pid/exe" 2>/dev/null)" = "$expected_binary" ] &&
                [ -r "/proc/$pid/task/$pid/children" ]
            then
                children=()
                read -r -a children < "/proc/$pid/task/$pid/children" || true
                if [ "${#children[@]}" -eq "$expected_children" ] &&
                    grep -Fq "День 1: завершён" "$test_dir/signal_output"
                then
                    break
                fi
            fi
            sleep 0.01
        done
        [ "${#children[@]}" -eq "$expected_children" ]
        # новая группа должна принадлежать самой программе
        read -r pgid < <(ps -o pgid= -p "$pid")
        [ "$pgid" = "$pid" ]
        for process in "$pid" "${children[@]}"
        do
            grep -Eq "^Threads:[[:space:]]+1$" "/proc/$process/status"
        done
        shm="/dev/shm/library_6_$(id -u)_$pid"
        [ -e "$shm" ]
        semaphore_prefix="/dev/shm/sem.library_6_$(id -u)_${pid}_"
        shopt -s nullglob
        semaphores=("${semaphore_prefix}"*)
        [ "${#semaphores[@]}" -eq 5 ]
        for role in state librarian done reader_0 reader_1
        do
            [ -e "${semaphore_prefix}${role}" ]
        done
        if [ "$signal" = INT ]
        then
            kill -INT -- "-$pid"
        else
            kill -TERM "$pid"
        fi
        wait "$pid"
        [ ! -e "$shm" ]
        semaphores=("${semaphore_prefix}"*)
        [ "${#semaphores[@]}" -eq 0 ]
        for child in "${children[@]}"
        do
            ! kill -0 "$child" 2>/dev/null
        done
        [ ! -s "$test_dir/signal_errors" ]
        grep -Fxq "Получен сигнал завершения. Моделирование остановлено." "$test_dir/signal_output"
        grep -Fxq "Статистика:" "$test_dir/signal_output"
        grep -Fxq "Библиотека В.С.Ё." "$test_dir/signal_output"
        grep -Fxq "Завершение работы библиотеки" library.log
        trap - EXIT
SIGNAL_TEST
    then
        echo "PASS: signal_$signal"
        passed=$((passed + 1))
    else
        echo "FAIL: signal_$signal"
        cat "$test_dir/signal_errors" "$test_dir/signal_output"
    fi
done

echo "Tests passed: $passed/$total"
[ "$passed" -eq "$total" ]
