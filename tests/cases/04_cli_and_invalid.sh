#!/usr/bin/env bash
set -euo pipefail
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)/test_common.sh"

check_cli_days()
{
    full_run days.txt -d 1
    grep -q 'days=1;' server.log
    grep -q 'День 1: серверная обработка завершена' server.log
    ! grep -q 'День 2:' server.log
}

check_cli_reading_days()
{
    full_run one_reader_one_book.txt -r 2
    grep -q 'reading_days=2;' server.log
    grep -q '; получение книги; книга 1; день возврата 3' reader_0.log
}

check_invalid_ip()
{
    start_process invalid_ip "$build_dir/library_reader_client" --host 256.1.2.3 --port 5050 --reader 0
    wait_process "$last_pid" failure
    grep -q 'Ошибка IPv4' invalid_ip.log
}

check_invalid_port()
{
    local number
    for number in 0 65536; do
        start_process "invalid_port_$number" "$build_dir/library_reader_client" --host 127.0.0.1 --port "$number" --reader 0
        wait_process "$last_pid" failure
        grep -q 'Ошибка:' "invalid_port_$number.log"
    done
}

check_invalid_reader()
{
    start_server one_reader_one_book.txt
    start_client 1 invalid_reader
    wait_process "$last_pid" failure
    grep -q 'Ошибка регистрации' invalid_reader.log
    kill -0 "$server_pid"
    start_client librarian
    start_client 0
    for pid in "${pids[@]}"; do
        [[ -n ${statuses[$pid]+set} ]] || wait_process "$pid" success
    done
    check_model 1
}

test_case()
{
    run_subcase days check_cli_days
    run_subcase reading_days check_cli_reading_days
    run_subcase invalid_ip check_invalid_ip
    run_subcase invalid_port check_invalid_port
    run_subcase invalid_reader_index check_invalid_reader
}

run_case cli_and_invalid
