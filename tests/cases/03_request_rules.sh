#!/usr/bin/env bash
set -euo pipefail
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)/test_common.sh"

check_fifo()
{
    full_run "$name.txt"
    first=$(grep '; книга зарезервирована, заявка закрыта;' librarian.log | head -1)
    index=1
    [[ $first == "День 4; Reader $index;"* ]] || fail 'неверный выбор заявки'
    grep -q 'получение по собственному резерву' "reader_$index.log"
}

check_lifo()
{
    full_run "$name.txt"
    first=$(grep '; книга зарезервирована, заявка закрыта;' librarian.log | head -1)
    index=2
    [[ $first == "День 4; Reader $index;"* ]] || fail 'неверный выбор заявки'
    grep -q 'получение по собственному резерву' "reader_$index.log"
}

check_always_wait()
{
    full_run "$name.txt"
    grep -q '; книга недоступна; книга 1' reader_1.log
    grep -q '; создана заявка; книга 1;' reader_1.log
    ! grep -q '; получение книги;' reader_1.log
    grep -q 'выдача отложена по always_wait; книга 2' reader_1.log
}

check_on_unavailable()
{
    full_run "$name.txt"
    grep -q '; книга недоступна; книга 1' reader_1.log
    grep -q '; создана заявка; книга 1;' reader_1.log
    grep -q '; получение книги; книга 2;' reader_1.log
}

test_case()
{
    run_subcase fifo check_fifo
    run_subcase lifo check_lifo
    run_subcase always_wait check_always_wait
    run_subcase on_unavailable check_on_unavailable
}

run_case request_rules
