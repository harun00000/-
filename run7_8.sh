#!/usr/bin/env bash
set -e

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# собираем нужные исполняемые файлы перед запуском
cmake -S "$project_dir" -B "$project_dir/build"
cmake --build "$project_dir/build" --target library_7_8_coordinator library_7_8_librarian library_7_8_reader

config="$project_dir/config.txt"
arguments=("$@")
for ((idx=0; idx<${#arguments[@]}; ++idx))
do
    case "${arguments[idx]}" in
        -c|--config) config="${arguments[idx+1]:-}" ;;
        --config=*) config="${arguments[idx]#*=}" ;;
        -c?*) config="${arguments[idx]#-c}" ;;
        -h|--help) exec "$project_dir/build/library_7_8_coordinator" --help ;;
    esac
done
session="run_${UID}_${BASHPID}_${RANDOM}"
coordinator=
participants=()
# при ошибке останавливаем только процессы, запущенные этим скриптом
cleanup(){
    trap - EXIT INT TERM
    if [ -n "$coordinator" ]; then kill -TERM "$coordinator" 2>/dev/null || true; fi
    for pid in "${participants[@]}"; do kill -TERM "$pid" 2>/dev/null || true; done
    if [ -n "$coordinator" ]; then wait "$coordinator" 2>/dev/null || true; fi
    for pid in "${participants[@]}"; do wait "$pid" 2>/dev/null || true; done
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

"$project_dir/build/library_7_8_coordinator" --session "$session" -c "$project_dir/config.txt" "$@" &
coordinator=$!
# ready появляется последним, после подготовки остальных IPC-объектов
while [ ! -e "/dev/shm/sem.library_7_8_${session}_ready" ]
do
    if ! kill -0 "$coordinator" 2>/dev/null
    then
        wait "$coordinator"
        exit 1
    fi
    sleep 0.01
done
# число Reader берём из config после его проверки Coordinator
reader_count=$(awk -F= '$1 == "reader" {count++} END {print count+0}' "$config")
"$project_dir/build/library_7_8_librarian" --session "$session" &
participants+=("$!")
for ((reader=0; reader<reader_count; ++reader))
do
    "$project_dir/build/library_7_8_reader" --session "$session" --reader "$reader" &
    participants+=("$!")
done
result=0
wait "$coordinator" || result=$?
coordinator=
for pid in "${participants[@]}"; do wait "$pid" || result=$?; done
participants=()
exit "$result"
