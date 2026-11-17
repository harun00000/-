#!/usr/bin/env bash
set -euo pipefail
project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$project_dir"
[[ $# == 0 ]] || { echo 'Использование: ./scripts/test4_5_tcp.sh' >&2; exit 1; }
cmake -S . -B build
cmake --build build -j 4
shopt -s nullglob
cases=("$project_dir"/tests/cases/*.sh)
((${#cases[@]} == 18)) || { echo 'Ожидалось ровно 18 test cases' >&2; exit 1; }
runner_pid=
cleanup_runner()
{
    if [[ -n "$runner_pid" ]]; then
        kill -TERM "$runner_pid" 2>/dev/null || true
        wait "$runner_pid" 2>/dev/null || true
    fi
}
trap cleanup_runner EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
passed=0
for case_file in "${cases[@]}"; do
    case_name=${case_file##*/}
    case_name=${case_name#*_}
    case_name=${case_name%.sh}
    printf '[%d/18] %s\n' "$((passed + 1))" "$case_name"
    TCP_TEST_TIMED=1 timeout --kill-after=5s 25s bash "$case_file" &
    runner_pid=$!
    if wait "$runner_pid"; then
        runner_pid=
        passed=$((passed + 1))
    else
        result=$?
        runner_pid=
        printf 'FAIL: %s (exit code %d)\n' "$case_name" "$result" >&2
        exit "$result"
    fi
done
printf '%d/18 tests passed\n' "$passed"
