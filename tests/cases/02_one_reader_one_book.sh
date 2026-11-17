#!/usr/bin/env bash
set -euo pipefail
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)/test_common.sh"

test_case()
{
    full_run one_reader_one_book.txt
    grep -Fxq 'Выдано книг: 1' server.log
    grep -q '; получение книги; книга 1;' reader_0.log
}

run_case one_reader_one_book
