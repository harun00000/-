#!/usr/bin/env bash

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)" || exit 1
cd -- "$project_dir" || exit 1

# каждый сценарий выполняется отдельной оболочкой под timeout
run_case(){
    set -e
    local name=$1 session=$2 case_dir=$3
    shift 3
    local coordinator= participants=() shm="/dev/shm/library_7_8_${session}_shm"
    cleanup(){
        trap - EXIT TERM INT
        if [ -n "$coordinator" ]; then kill -TERM "$coordinator" 2>/dev/null || true; fi
        for pid in "${participants[@]}"; do kill -TERM "$pid" 2>/dev/null || true; done
        if [ -n "$coordinator" ]; then wait "$coordinator" 2>/dev/null || true; fi
        for pid in "${participants[@]}"; do wait "$pid" 2>/dev/null || true; done
    }
    trap cleanup EXIT
    trap 'exit 1' TERM INT

    wait_for(){
        local attempt
        for ((attempt=0; attempt<300; ++attempt))
        do
            if "$@"; then return 0; fi
            if [ -n "$coordinator" ] && ! kill -0 "$coordinator" 2>/dev/null; then return 1; fi
            sleep 0.01
        done
        echo "Не дождались: $*" >&2
        return 1
    }
    start_librarian(){
        ./build/library_7_8_librarian --session "$session" &
        participants+=("$!")
        wait_for grep -Fq 'Librarian подключён' "$case_dir/output"
    }
    start_reader(){
        ./build/library_7_8_reader --session "$session" --reader "$1" &
        participants+=("$!")
        wait_for grep -Fq "Reader $1 подключён" "$case_dir/output"
    }
    assert_removed(){
        [ ! -e "$shm" ]
        ! compgen -G "/dev/shm/sem.library_7_8_${session}_*" > /dev/null
        for pid in "${participants[@]}"; do ! kill -0 "$pid" 2>/dev/null; done
    }

    if [ "$name" = launcher ]
    then
        ./run7_8.sh -c tests/boundary_one_book.txt -d 7 -r 5
        return
    fi
    ./build/library_7_8_coordinator --session "$session" "$@" &
    coordinator=$!
    wait_for test -e "/dev/shm/sem.library_7_8_${session}_ready"
    [ -e "$shm" ]
    if [ "$name" = session_errors ]
    then
        if ./build/library_7_8_coordinator --session "$session" "$@" > "$case_dir/rejected" 2>&1; then return 1; fi
        grep -Fq 'Не удалось создать сессию' "$case_dir/rejected"
        if ./build/library_7_8_reader --session "$session" --reader 20 > "$case_dir/rejected" 2>&1; then return 1; fi
        grep -Fq 'индекс Reader должен быть' "$case_dir/rejected"
    fi
    start_librarian
    start_reader 0
    if [ "$name" = registration ]
    then
        # один читатель ещё отсутствует, поэтому ни один модельный день не начинается
        sleep 0.05
        ! grep -Fq 'День 1: начало' "$case_dir/output"
        if ./build/library_7_8_librarian --session "$session" > "$case_dir/rejected" 2>&1; then return 1; fi
        grep -Fq 'Librarian уже подключён' "$case_dir/rejected"
        if ./build/library_7_8_reader --session "$session" --reader 0 > "$case_dir/rejected" 2>&1; then return 1; fi
        grep -Fq 'Reader уже подключён' "$case_dir/rejected"
    fi
    if [ "$name" = startup_TERM ]
    then
        kill -TERM "$coordinator"
    else
        # количество участников совпадает с переданным config
        local config=config.txt args=("$@") reader_count
        for ((idx=0; idx<${#args[@]}; ++idx))
        do
            if [ "${args[idx]}" = -c ]; then config=${args[idx+1]}; fi
        done
        reader_count=$(awk -F= '$1 == "reader" {count++} END {print count+0}' "$config")
        for ((reader=1; reader<reader_count; ++reader)); do start_reader "$reader"; done
        if [[ "$name" = signal_* || "$name" = participant_* ]]
        then
            wait_for grep -Fq 'День 1: завершён' "$case_dir/output"
            for pid in "$coordinator" "${participants[@]}"
            do
                grep -Eq '^Threads:[[:space:]]+1$' "/proc/$pid/status"
                # все участники запущены оболочкой, Coordinator не создаёт детей
                read -r ppid < <(ps -o ppid= -p "$pid")
                [ "$ppid" = "$$" ]
            done
            local children=()
            read -r -a children < "/proc/$coordinator/task/$coordinator/children" || true
            [ "${#children[@]}" -eq 0 ]
            shopt -s nullglob
            local semaphores=(/dev/shm/sem.library_7_8_"${session}"_*)
            [ "${#semaphores[@]}" -eq $((reader_count+5)) ]
            case "$name" in
                signal_TERM) kill -TERM "$coordinator" ;;
                signal_INT) kill -INT "$coordinator" ;;
                participant_TERM) kill -TERM "${participants[0]}" ;;
                participant_INT) kill -INT "${participants[1]}" ;;
            esac
        fi
    fi
    local result=0
    wait "$coordinator" || result=$?
    coordinator=
    if [[ "$name" = participant_* ]]; then [ "$result" -ne 0 ]; else [ "$result" -eq 0 ]; fi
    for pid in "${participants[@]}"
    do
        result=0
        wait "$pid" || result=$?
        if [[ "$name" != participant_* ]]; then [ "$result" -eq 0 ]; fi
    done
    assert_removed
    participants=()
    if [[ "$name" = signal_* || "$name" = startup_TERM ]]
    then
        grep -Fxq 'Получен сигнал завершения. Моделирование остановлено.' "$case_dir/output"
    fi
    grep -Fxq 'Статистика:' library.log
    grep -Fxq 'Завершение работы библиотеки' library.log
    grep -Fxq 'Статистика:' "$case_dir/output"
    grep -Fxq 'Библиотека В.С.Ё.' "$case_dir/output"
}

if [ "${1:-}" = --case ]
then
    shift
    run_case "$@"
    exit $?
fi

test_dir=$(mktemp -d ./build/tests7_8.XXXXXX) || exit 1
trap 'rm -rf -- "$test_dir"' EXIT
passed=0
total=0
report_test(){
    local name=$1 days=$2
    shift 2
    total=$((total+1))
    local case_dir="$test_dir/$name" session="t_${UID}_${BASHPID}_${total}_${RANDOM}"
    mkdir "$case_dir"
    if timeout -k 2s 12s bash "$0" --case "$name" "$session" "$case_dir" "$@" > "$case_dir/output" 2> "$case_dir/errors"
    then
        local valid=true
        if [[ "$name" != participant_* ]] && [ -s "$case_dir/errors" ]; then valid=false; fi
        if [ "$days" -gt 0 ]
        then
            grep -Fxq "День $days: завершён" "$case_dir/output" || valid=false
            if grep -Fxq "День $((days+1)): начало" "$case_dir/output"; then valid=false; fi
        fi
        case "$name" in
            normal_config|root_config)
                grep -Fq 'Приходов читателей: 8' "$case_dir/output" &&
                grep -Fq 'Выполнено заявок: 2' "$case_dir/output" &&
                grep -Fq 'Приход читателя:' library.log &&
                grep -Fq 'Резервирование:' library.log || valid=false ;;
            boundary_one_book)
                grep -Fq 'Выдача зарезервированной книги:' "$case_dir/output" &&
                grep -Fq 'уведомляет читателя' "$case_dir/output" || valid=false ;;
            boundary_one_day)
                grep -Fq 'Выдано книг: 1' "$case_dir/output" &&
                grep -Fq 'Возвращено книг: 0' "$case_dir/output" || valid=false ;;
            strategy_lifo)
                grep -Fq 'Выбрана заявка №2 (день 3), читатель Виктор. Заявка закрыта' "$case_dir/output" || valid=false ;;
            strategy_fifo)
                grep -Fq 'Выбрана заявка №1 (день 2), читатель Борис. Заявка закрыта' "$case_dir/output" || valid=false ;;
            always_wait)
                if grep -Fq 'Борис получает книгу Колобок' "$case_dir/output"; then valid=false; fi
                grep -Fq 'Создана заявка №1: Борис, книга Курочка ряба, день 2' "$case_dir/output" || valid=false ;;
            on_unavailable)
                grep -Fq 'Борис получает книгу Колобок' "$case_dir/output" || valid=false ;;
            reading_override)
                grep -Fq 'День возврата: 6.' "$case_dir/output" &&
                grep -Fq 'Возвращено книг: 1' "$case_dir/output" || valid=false ;;
            max_readers)
                grep -Fq 'Приходов читателей: 500' "$case_dir/output" &&
                grep -Fq 'Создано заявок: 19' "$case_dir/output" || valid=false ;;
        esac
        if "$valid"
        then
            echo "PASS: $name"
            passed=$((passed+1))
            return
        fi
    fi
    echo "FAIL: $name"
    cat "$case_dir/errors" "$case_dir/output" "$case_dir/rejected" 2>/dev/null
}

sed 's/strategy=lifo/strategy=fifo/' tests/strategy_lifo.txt > "$test_dir/fifo.txt"
sed 's/request_rule=always_wait/request_rule=on_unavailable/' tests/request_always_wait.txt > "$test_dir/on_unavailable.txt"
{
    printf 'reading_days=1\ntotal_days=25\nbook=1;Курочка ряба\n'
    for ((reader=1; reader<=20; ++reader))
    do
        printf 'reader=%d;Читатель%d;1;1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25\n' "$reader" "$reader"
    done
} > "$test_dir/max_readers.txt"

report_test normal_config 13 -c tests/normal_config.txt
report_test boundary_one_day 1 -c tests/boundary_one_day.txt
report_test boundary_one_book 9 -c tests/boundary_one_book.txt
report_test strategy_lifo 4 -c tests/strategy_lifo.txt
report_test strategy_fifo 4 -c "$test_dir/fifo.txt"
report_test always_wait 2 -c tests/request_always_wait.txt
report_test on_unavailable 2 -c "$test_dir/on_unavailable.txt"
report_test days_override 2 -c tests/normal_config.txt -d 2
report_test reading_override 7 -c tests/boundary_one_day.txt -d 7 -r 5
report_test root_config 13 -c config.txt
report_test max_readers 25 -c "$test_dir/max_readers.txt"
report_test registration 9 -c tests/boundary_one_book.txt
report_test session_errors 9 -c tests/boundary_one_book.txt
report_test startup_TERM 0 -c tests/signal_config.txt
report_test signal_TERM 0 -c tests/signal_config.txt
report_test signal_INT 0 -c tests/signal_config.txt
report_test participant_TERM 0 -c tests/signal_config.txt
report_test participant_INT 0 -c tests/signal_config.txt
report_test launcher 7

echo "Tests passed: $passed/$total"
[ "$passed" -eq "$total" ]
