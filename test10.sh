#!/usr/bin/env bash

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)" || exit 1
cd -- "$project_dir" || exit 1

# каждый сценарий выполняется отдельной оболочкой под timeout
run_case(){
    set -e
    local name=$1 session=$2 case_dir=$3
    shift 3
    local coordinator= participants=()
    local ipc_check="$IPC10_CHECK"
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
        ./build/library_10_librarian --session "$session" &
        participants+=("$!")
        wait_for grep -Fq 'Librarian подключён' "$case_dir/output"
    }
    start_reader(){
        ./build/library_10_reader --session "$session" --reader "$1" &
        participants+=("$!")
        wait_for grep -Fq "Reader $1 подключён" "$case_dir/output"
    }
    assert_removed(){
        "$ipc_check" "$session" "$reader_count" absent
        ! compgen -G "/dev/shm/sem.library_10_${session}_*" > /dev/null
        for pid in "${participants[@]}"; do ! kill -0 "$pid" 2>/dev/null; done
    }

    if [ "$name" = cli_errors ]
    then
        for arguments in '--session bad/name' '--session aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' '--delay-ms -1 --session ok' ''
        do
            if ./build/library_10_coordinator $arguments > "$case_dir/rejected" 2>&1; then return 1; fi
            grep -Fq 'Ошибка:' "$case_dir/rejected"
            ! grep -Fq 'Condition ' "$case_dir/rejected"
        done
        # допустимое целое число ещё может переполнить дату возврата
        if ./build/library_10_coordinator --session "$session" -c tests/boundary_one_day.txt -r 2147483647 > "$case_dir/rejected" 2>&1; then return 1; fi
        grep -Fq 'Ошибка параметров:' "$case_dir/rejected"
        ! grep -Fq 'Condition ' "$case_dir/rejected"
        if ./build/library_10_reader --session "$session" --reader -1 > "$case_dir/rejected" 2>&1; then return 1; fi
        ! grep -Fq 'Condition ' "$case_dir/rejected"
        return
    fi
    if [ "$name" = two_sessions ]
    then
        local second="${session}_b" second_pid
        ./build/library_10_coordinator --session "$session" -c tests/signal_config.txt --delay-ms 0 &
        coordinator=$!
        ./build/library_10_coordinator --session "$second" -c tests/signal_config.txt --delay-ms 0 > "$case_dir/second" &
        second_pid=$!
        participants+=("$second_pid")
        wait_for test -e "/dev/shm/sem.library_10_${session}_ready"
        wait_for test -e "/dev/shm/sem.library_10_${second}_ready"
        "$ipc_check" "$session" 2 present
        "$ipc_check" "$second" 2 present
        for current in "$session" "$second"
        do
            ./build/library_10_librarian --session "$current" >> "$case_dir/participants" &
            participants+=("$!")
            for reader in 0 1
            do
                ./build/library_10_reader --session "$current" --reader "$reader" >> "$case_dir/participants" &
                participants+=("$!")
            done
        done
        wait_for grep -Fq 'День 1: завершён' "$case_dir/output"
        wait_for grep -Fq 'День 1: завершён' "$case_dir/second"
        kill -TERM "$coordinator" "$second_pid"
        wait "$coordinator"
        coordinator=
        for pid in "${participants[@]}"; do wait "$pid"; done
        "$ipc_check" "$session" 2 absent
        "$ipc_check" "$second" 2 absent
        ! compgen -G "/dev/shm/sem.library_10_${session}_*" > /dev/null
        for pid in "${participants[@]}"; do ! kill -0 "$pid" 2>/dev/null; done
        participants=()
        return
    fi
    if [ "$name" = launcher ]
    then
        ./run10.sh -c tests/boundary_one_book.txt -d 7 -r 5 --delay-ms 0 --print-ipc
        local launcher_session launcher_readers
        launcher_session=$(sed -n 's|^IPC queue: /library_10_\(.*\)_requests$|\1|p' "$case_dir/output")
        [ -n "$launcher_session" ]
        launcher_readers=$(awk -F= '$1 == "reader" {count++} END {print count+0}' tests/boundary_one_book.txt)
        "$ipc_check" "$launcher_session" "$launcher_readers" absent
        ! compgen -G "/dev/shm/sem.library_10_${launcher_session}_*" > /dev/null
        ! pgrep -f "library_10_(coordinator|librarian|reader) --session ${launcher_session}( |$)" > /dev/null
        return
    fi
    ./build/library_10_coordinator --session "$session" "$@" &
    coordinator=$!
    wait_for test -e "/dev/shm/sem.library_10_${session}_ready"
    local config=config.txt args=("$@") reader_count
    for ((idx=0; idx<${#args[@]}; ++idx))
    do
        if [ "${args[idx]}" = -c ]; then config=${args[idx+1]}; fi
    done
    local config_path
    config_path=$(realpath "$config")
    reader_count=$(awk -F= '$1 == "reader" {count++} END {print count+0}' "$config")
    "$ipc_check" "$session" "$reader_count" present
    if [ "$name" = session_errors ]
    then
        if ./build/library_10_coordinator --session "$session" "$@" > "$case_dir/rejected" 2>&1; then return 1; fi
        grep -Fq 'Не удалось создать очередь' "$case_dir/rejected"
        if ./build/library_10_reader --session "$session" --reader 20 > "$case_dir/rejected" 2>&1; then return 1; fi
        grep -Fq 'индекс Reader вне диапазона' "$case_dir/rejected"
    fi
    start_librarian
    start_reader 0
    if [ "$name" = registration ]
    then
        # один читатель ещё отсутствует, поэтому ни один модельный день не начинается
        sleep 0.05
        ! grep -Fq 'День 1: начало' "$case_dir/output"
        if ./build/library_10_librarian --session "$session" > "$case_dir/rejected" 2>&1; then return 1; fi
        grep -Fq 'Librarian уже подключён' "$case_dir/rejected"
        if ./build/library_10_reader --session "$session" --reader 0 > "$case_dir/rejected" 2>&1; then return 1; fi
        grep -Fq 'Reader уже подключён' "$case_dir/rejected"
    fi
    if [ "$name" = startup_TERM ]
    then
        kill -TERM "$coordinator"
    else
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
            local semaphores=(/dev/shm/sem.library_10_"${session}"_*)
            [ "${#semaphores[@]}" -eq $((reader_count+3)) ]
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
    if [ "$name" = statistics_match ]
    then
        grep -E '^(Приходов читателей|Выдано книг|Возвращено книг|Создано заявок|Выполнено заявок|Отправлено уведомлений):' "$case_dir/output" > "$case_dir/statistics"
        for binary in library_sim library_4_5 library_6
        do
            (cd "$case_dir"; timeout 10s "$project_dir/build/$binary" -c "$config_path") > "$case_dir/reference"
            grep -E '^(Приходов читателей|Выдано книг|Возвращено книг|Создано заявок|Выполнено заявок|Отправлено уведомлений):' "$case_dir/reference" > "$case_dir/reference_stats"
            diff -u "$case_dir/reference_stats" "$case_dir/statistics"
        done
        (cd "$case_dir"; timeout 10s "$project_dir/run7_8.sh" -c "$config_path") > "$case_dir/reference"
        grep -E '^(Приходов читателей|Выдано книг|Возвращено книг|Создано заявок|Выполнено заявок|Отправлено уведомлений):' "$case_dir/reference" > "$case_dir/reference_stats"
        diff -u "$case_dir/reference_stats" "$case_dir/statistics"
    fi
    if [ "$name" = delay_zero ]
    then
        grep -E '^(Приходов читателей|Выдано книг|Возвращено книг|Создано заявок|Выполнено заявок|Отправлено уведомлений):' "$case_dir/output" > "$case_dir/statistics"
        mkdir "$case_dir/default"
        bash "$0" --case delay_default "${session}_d" "$case_dir/default" -c "$config_path" > "$case_dir/default/output"
        grep -E '^(Приходов читателей|Выдано книг|Возвращено книг|Создано заявок|Выполнено заявок|Отправлено уведомлений):' "$case_dir/default/output" > "$case_dir/reference_stats"
        diff -u "$case_dir/reference_stats" "$case_dir/statistics"
    fi
    if [ "$name" = print_ipc ]
    then
        [ "$(grep -c '^IPC queue: /library_10_' "$case_dir/output")" -eq $((reader_count+3)) ]
        [ "$(grep -c '^IPC semaphore: /library_10_' "$case_dir/output")" -eq $((reader_count+3)) ]
    fi
    grep -Fxq 'Библиотека В.С.Ё.' "$case_dir/output"
}

if [ "${1:-}" = --case ]
then
    shift
    run_case "$@"
    exit $?
fi

test_dir=$(mktemp -d ./build/tests10.XXXXXX) || exit 1
trap 'rm -rf -- "$test_dir"' EXIT
# проверка через API работает и в среде без смонтированного /dev/mqueue
cat > "$test_dir/check_queues.c" <<'CHECK_QUEUES'
#define _POSIX_C_SOURCE 200809L
#include "ipc_10.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char *argv[])
{
    if (argc != 4)
        return 1;
    int readers = atoi(argv[2]);
    int present = strcmp(argv[3], "present") == 0;
    if (readers < 1 || readers > MAX_READERS)
        return 1;
    const char *roles[] = {"requests", "registration", "librarian"};
    for (int idx = 0; idx < readers + 3; ++idx)
    {
        char name[IPC_10_NAME_SIZE];
        if (idx < 3)
            snprintf(name, sizeof name, "/library_10_%s_%s", argv[1], roles[idx]);
        else
            snprintf(name, sizeof name, "/library_10_%s_reader_%d", argv[1], idx - 3);
        mqd_t queue = mq_open(name, O_RDONLY | O_NONBLOCK);
        if (queue == (mqd_t)-1)
        {
            if (present || errno != ENOENT)
            {
                perror(name);
                return 1;
            }
        }
        else
        {
            struct mq_attr attributes;
            int valid =
                present && mq_getattr(queue, &attributes) == 0 && attributes.mq_msgsize == (long)sizeof(Ipc10Message);
            if (mq_close(queue) != 0 || !valid)
            {
                fprintf(stderr, "Неожиданная очередь %s\n", name);
                return 1;
            }
        }
    }
    return 0;
}
CHECK_QUEUES
cc -std=c11 -Wall -Wextra -Wpedantic -DLIBRARY_PROCESS_10 -Iinclude "$test_dir/check_queues.c" -o "$test_dir/check_queues" -lrt || exit 1
export IPC10_CHECK="$project_dir/$test_dir/check_queues"
passed=0
total=0
report_test(){
    local name=$1 days=$2
    shift 2
    total=$((total+1))
    local case_dir="$test_dir/$name" session="t_${UID}_${BASHPID}_${total}_${RANDOM}"
    mkdir "$case_dir"
    if timeout -k 2s 20s bash "$0" --case "$name" "$session" "$case_dir" "$@" > "$case_dir/output" 2> "$case_dir/errors"
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

# разные дни заявок позволяют точно сравнить статистику при любом порядке процессов
sed 's/;2,4,10$/;3,4,10/' tests/normal_config.txt > "$test_dir/ordered.txt"

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
report_test statistics_match 13 -c "$test_dir/ordered.txt"
report_test delay_zero 13 -c "$test_dir/ordered.txt" --delay-ms 0
report_test print_ipc 9 -c tests/boundary_one_book.txt --print-ipc
report_test two_sessions 0
report_test cli_errors 0
report_test launcher 7

echo "Tests passed: $passed/$total"
[ "$passed" -eq "$total" ]
