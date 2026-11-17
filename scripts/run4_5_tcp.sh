#!/usr/bin/env bash
set -euo pipefail
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$script_dir"
config_file="$script_dir/config.txt"
port=5050
server_args=()
while (($#)); do
    case "$1" in
        -c|--config)
            [[ $# -ge 2 ]] || { echo 'Нужно имя config' >&2; exit 1; }
            config_file=$2
            shift 2
            ;;
        --port)
            [[ $# -ge 2 ]] || { echo 'Нужен port' >&2; exit 1; }
            port=$2
            shift 2
            ;;
        -d|--days|-r|--reading-days)
            [[ $# -ge 2 ]] || { echo 'Нужно значение опции' >&2; exit 1; }
            server_args+=("$1" "$2")
            shift 2
            ;;
        -h|--help)
            echo 'Использование: ./scripts/run4_5_tcp.sh [-c FILE] [-d N] [-r N] [--port PORT]'
            exit 0
            ;;
        *) echo "Неизвестная опция: $1" >&2; exit 1 ;;
    esac
done
[[ -f "$config_file" ]] || { echo "Не найден config: $config_file" >&2; exit 1; }
reader_count=$(awk '/^reader=/{count++} END {print count+0}' "$config_file")
((reader_count > 0)) || { echo 'В config нет читателей' >&2; exit 1; }
cmake -S . -B build
cmake --build build --target library_server library_librarian_client library_reader_client -j 4
log_dir=$(mktemp -d "$script_dir/build/tcp-run.XXXXXX")
pids=()
cleanup()
{
    for pid in "${pids[@]}"; do
        if kill -0 "$pid" 2>/dev/null; then
            kill -TERM "$pid" 2>/dev/null || true
        fi
    done
    for pid in "${pids[@]}"; do
        wait "$pid" 2>/dev/null || true
    done
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
./build/library_server --host 127.0.0.1 --port "$port" -c "$config_file" "${server_args[@]}" > "$log_dir/server.log" 2>&1 &
server_pid=$!
pids+=("$server_pid")
ready=0
for ((attempt=0; attempt<100; ++attempt)); do
    if grep -q '^READY ' "$log_dir/server.log"; then
        ready=1
        break
    fi
    if ! kill -0 "$server_pid" 2>/dev/null; then
        echo "Сервер не запустился; диагностика: $log_dir/server.log" >&2
        exit 1
    fi
    sleep 0.05
done
((ready)) || { echo 'Истёк timeout готовности сервера' >&2; exit 1; }
./build/library_librarian_client --host 127.0.0.1 --port "$port" > "$log_dir/librarian.log" 2>&1 &
pids+=("$!")
for ((index=0; index<reader_count; ++index)); do
    ./build/library_reader_client --host 127.0.0.1 --port "$port" --reader "$index" > "$log_dir/reader_$index.log" 2>&1 &
    pids+=("$!")
done
printf 'Раздельные результаты: %s\n' "$log_dir"
# проверяем и уже завершившиеся процессы, не теряя их exit codes
remaining=("${pids[@]}")
while ((${#remaining[@]} > 0)); do
    for index in "${!remaining[@]}"; do
        if ! kill -0 "${remaining[$index]}" 2>/dev/null; then
            if ! wait "${remaining[$index]}"; then
                echo "Ошибка участника; см. раздельные логи: $log_dir" >&2
                exit 1
            fi
            unset 'remaining[index]'
        fi
    done
    if ((${#remaining[@]} > 0)); then sleep 0.02; fi
done
printf 'Все %d процессов завершились успешно\n' "${#pids[@]}"
