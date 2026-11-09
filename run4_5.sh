#!/usr/bin/env bash
set -e

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# собираем нужные исполняемые файлы перед запуском
cmake -S "$project_dir" -B "$project_dir/build"
cmake --build "$project_dir/build" --target library_4_5
exec "$project_dir/build/library_4_5" -c "$project_dir/config.txt" "$@"
