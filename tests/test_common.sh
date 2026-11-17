#!/usr/bin/env bash
set -euo pipefail
project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build_dir="$project_dir/build"
config_dir="$project_dir/tests/configs"

fail()
{
    echo "FAIL: $*; логи: ${case_dir:-не созданы}" >&2
    exit 1
}

# закрываем и дожидаемся только собственных процессов текущего сценария
cleanup_case()
{
    local pid attempt alive
    local raw_descriptor
    for raw_descriptor in "${raw_fds[@]}"; do exec {raw_descriptor}>&-; done
    raw_fds=()
    for pid in "${pids[@]}"; do
        if [[ -z ${statuses[$pid]+set} ]]; then
            kill -TERM "$pid" 2>/dev/null || true
        fi
    done
    for ((attempt=0; attempt<100; ++attempt)); do
        alive=0
        for pid in "${pids[@]}"; do
            if [[ -z ${statuses[$pid]+set} ]] && kill -0 "$pid" 2>/dev/null; then alive=1; fi
        done
        ((alive)) || break
        sleep 0.02
    done
    for pid in "${pids[@]}"; do
        if [[ -z ${statuses[$pid]+set} ]]; then
            kill -KILL "$pid" 2>/dev/null || true
            statuses[$pid]=0
            wait "$pid" 2>/dev/null || statuses[$pid]=$?
        fi
    done
}

start_process()
{
    local log_name=$1
    shift
    (
        for raw_descriptor in "${raw_fds[@]}"; do exec {raw_descriptor}>&-; done
        exec "$@"
    ) > "$case_dir/$log_name.log" 2>&1 &
    last_pid=$!
    pids+=("$last_pid")
}

wait_process()
{
    local pid=$1 expected=$2 code=0
    if [[ -n ${statuses[$pid]+set} ]]; then
        code=${statuses[$pid]}
    else
        wait "$pid" 2> "$case_dir/wait_$pid.log" || code=$?
        statuses[$pid]=$code
    fi
    if [[ $expected == success ]]; then
        ((code == 0)) || fail "PID $pid: ожидался 0, получен $code"
    else
        ((code != 0 && code != 124)) || fail "PID $pid: ожидалась ошибка, получен $code"
    fi
}

wait_for_line()
{
    local pattern=$1 file=$2 attempt
    for ((attempt=0; attempt<200; ++attempt)); do
        if grep -Fq -- "$pattern" "$file"; then return; fi
        kill -0 "$server_pid" 2>/dev/null || fail "server завершился до события: $pattern"
        sleep 0.02
    done
    fail "timeout ожидания: $pattern"
}

start_server()
{
    local config=$1 attempt retry last_index
    shift
    for ((retry=0; retry<10; ++retry)); do
        port=$((20000 + RANDOM))
        start_process server "$build_dir/library_server" --host 127.0.0.1 --port "$port" -c "$config_dir/$config" "$@"
        server_pid=$last_pid
        for ((attempt=0; attempt<200; ++attempt)); do
            if grep -q '^READY ' "$case_dir/server.log"; then return; fi
            kill -0 "$server_pid" 2>/dev/null || break
            sleep 0.02
        done
        # повторяем выбор порта только при конфликте с другим локальным запуском
        if grep -q 'bind: Address already in use' "$case_dir/server.log"; then
            wait_process "$server_pid" failure
            last_index=$((${#pids[@]} - 1))
            unset "pids[$last_index]"
        else
            fail 'server не запустился'
        fi
    done
    fail 'не найден свободный TCP-порт'
}

start_client()
{
    local index=$1 log_name=${2:-}
    if [[ $index == librarian ]]; then
        start_process "${log_name:-librarian}" "$build_dir/library_librarian_client" --host 127.0.0.1 --port "$port"
    else
        start_process "${log_name:-reader_$index}" "$build_dir/library_reader_client" --host 127.0.0.1 --port "$port" --reader "$index"
    fi
}

wait_all_success()
{
    local pid
    for pid in "${pids[@]}"; do wait_process "$pid" success; done
    if grep -Rq --include='*.log' 'Condition ' "$case_dir"; then fail 'нарушен внутренний контракт'; fi
}

# преобразуем только предметные события из раздельных файлов для проверки инвариантов
extract_events()
{
    local file=$1 phase=$2 reader=$3
    awk -F '; ' -v phase="$phase" -v reader="$reader" '
        /^День [0-9]+; Reader / {
            day=$1; sub(/^День /,"",day)
            who=$2; sub(/^Reader /,"",who)
            book=$4; sub(/^книга /,"",book)
            value=$5; sub(/^[^0-9]*/,"",value)
            kind=""
            if (reader >= 0 && who != reader) exit 1
            if ($3=="приход читателя") kind="arrival"
            if ($3=="получение книги") kind="given"
            if ($3=="создана заявка") kind="created"
            if (phase==0 && $3=="возврат книги") kind="returned"
            if (phase==0 && $3=="уведомление о возврате") kind="notify"
            if (phase==0 && $3=="книга зарезервирована, заявка закрыта") kind="reserved"
            if (kind!="") print day,phase,NR,who,kind,book,value+0
        }
    ' "$file"
}

check_model()
{
    local count=$1 index line reader label
    extract_events "$case_dir/librarian.log" 0 -1 > "$case_dir/events.txt"
    for ((index=0; index<count; ++index)); do
        extract_events "$case_dir/reader_$index.log" 1 "$index" >> "$case_dir/events.txt"
    done
    sort -k1,1n -k2,2n -k3,3n "$case_dir/events.txt" > "$case_dir/ordered.txt"
    awk '
        function check(ok) { if (!ok) { print "Нарушен инвариант: " $0 > "/dev/stderr"; exit 1 } }
        {
            day=$1; reader=$4; kind=$5; book=$6; value=$7; pair=reader ":" book
            if (kind=="arrival") visits++
            if (kind=="given") {
                check(!(book in owner) && value>day)
                if (book in reserve) { check(reserve[book]==reader && reserved_day[book]<day); delete reserve[book] }
                owner[book]=reader; given++
            }
            if (kind=="returned") { check(book in owner && owner[book]==reader); delete owner[book]; returned++ }
            if (kind=="created") {
                check(!(value in active) && !(value in closed) && !(pair in pending))
                active[value]=pair; pending[pair]=value; created++
            }
            if (kind=="notify") { check(pair in pending); notified[day ":" pair]=1; notifications++ }
            if (kind=="reserved") {
                check(!(book in owner) && !(book in reserve) && !(value in closed))
                check(value in active && active[value]==pair && ((day ":" pair) in notified))
                delete active[value]; delete pending[pair]; closed[value]=1
                reserve[book]=reader; reserved_day[book]=day; completed++
            }
        }
        END {
            print "Приходов читателей: " visits+0
            print "Выдано книг: " given+0
            print "Возвращено книг: " returned+0
            print "Создано заявок: " created+0
            print "Выполнено заявок: " completed+0
            print "Отправлено уведомлений: " notifications+0
        }
    ' "$case_dir/ordered.txt" > "$case_dir/counts.txt"
    while IFS= read -r label; do grep -Fxq "$label" "$case_dir/server.log" || fail "$label"; done < "$case_dir/counts.txt"
    while IFS= read -r line; do
        reader=${line#*Reader }; reader=${reader%%;*}
        grep -Fxq "$line" "$case_dir/reader_$reader.log" || fail 'уведомление не доставлено Reader'
    done < <(grep '; уведомление о возврате;' "$case_dir/librarian.log" || true)
    if grep -Eq '; (приход читателя|запрос книги);' "$case_dir/server.log" "$case_dir/librarian.log"; then
        fail 'нарушен раздельный вывод'
    fi
}

full_run()
{
    local config=$1 index count
    shift
    start_server "$config" "$@"
    start_client librarian
    count=$(awk '/^reader=/{n++} END {print n+0}' "$config_dir/$config")
    for ((index=0; index<count; ++index)); do start_client "$index"; done
    wait_all_success
    check_model "$count"
    for ((index=0; index<count; ++index)); do
        grep -q 'Моделирование завершено:' "$case_dir/reader_$index.log" || fail 'Reader не завершился нормально'
    done
}

start_long_run()
{
    start_server signals.txt
    start_client librarian
    start_client 0
    reader_pid=$last_pid
    start_client 1
    wait_for_line 'сервер принимает посещения' "$case_dir/server.log"
}

# отдельный запуск case тоже ограничен временем, runner задаёт тот же предел снаружи
run_case()
{
    if [[ ${TCP_TEST_TIMED:-0} != 1 ]]; then
        TCP_TEST_TIMED=1 timeout --kill-after=5s 25s bash "${BASH_SOURCE[1]}"
        return
    fi
    local name=$1 pid index first number
    case_dir=$(mktemp -d "$build_dir/tcp-${name}.XXXXXX")
    printf 'Логи %s: %s\n' "$name" "$case_dir"
    local -a pids=()
    local -a raw_fds=()
    local -A statuses=()
    trap cleanup_case EXIT
    trap 'exit 130' INT
    trap 'exit 143' TERM
    cd "$case_dir"
    test_case
    if grep -Rq --include='*.log' 'Condition ' "$case_dir"; then fail 'нарушен внутренний контракт'; fi
    cleanup_case
    trap - EXIT INT TERM
}

start_observer()
{
    local log_name=${1:-observer}
    start_process "$log_name" "$build_dir/library_observer_client" --host 127.0.0.1 --port "$port"
    observer_pid=$last_pid
    wait_for_line 'Observer подключён' "$case_dir/$log_name.log"
}

start_workers()
{
    local config=$1 index
    worker_count=$(awk '/^reader=/{n++} END {print n+0}' "$config_dir/$config")
    start_client librarian
    librarian_pid=$last_pid
    for ((index=0; index<worker_count; ++index)); do start_client "$index"; done
}

finish_workers()
{
    local pid
    for pid in "${pids[@]}"; do
        [[ -n ${statuses[$pid]+set} ]] || wait_process "$pid" success
    done
    check_model "$worker_count"
}

start_paused_model()
{
    start_client librarian
    librarian_pid=$last_pid
    start_client 0
    start_client 1
    worker_count=3
    wait_for_line 'Зарегистрирован Librarian' "$case_dir/server.log"
    kill -STOP "$librarian_pid"
    start_client 2
    wait_for_line 'День 1: начало' "$case_dir/server.log"
}

check_observer_events()
{
    local event
    for event in 'моделирование начато' 'день начат' 'приход читателя' 'запрос книги' 'получение книги' \
        'книга недоступна' 'создана заявка' 'возврат книги' 'уведомление о возврате' \
        'книга зарезервирована' 'получение по собственному резерву' 'Librarian начал фазу' \
        'Librarian завершил фазу' 'Reader начал фазу' 'Reader завершил фазу' 'день завершён' 'моделирование завершено'; do
        grep -q "$event" "$case_dir/observer.log" || fail "Observer не получил: $event"
    done
}

stable_result()
{
    grep -Ec '^День [0-9]+: серверная обработка завершена$' "$case_dir/server.log"
    grep -E '^(Приходов читателей|Выдано книг|Возвращено книг|Создано заявок|Выполнено заявок|Отправлено уведомлений):' "$case_dir/server.log"
}

# подслучаи изолируют процессы и логи, сохраняя единый timeout основного case
run_subcase()
(
    local name=$1
    shift
    local -a pids=()
    local -a raw_fds=()
    local -A statuses=()
    case_dir=$(mktemp -d "$case_dir/${name}.XXXXXX")
    trap cleanup_case EXIT
    trap 'exit 130' INT
    trap 'exit 143' TERM
    cd "$case_dir"
    "$@"
    if grep -Rq --include='*.log' 'Condition ' "$case_dir"; then fail 'нарушен внутренний контракт'; fi
    cleanup_case
    trap - EXIT INT TERM
)

start_three_observers()
{
    local label
    observer_pids=()
    for label in observer_a observer_b observer_c; do
        start_observer "$label"
        observer_pids+=("$observer_pid")
    done
}

# сверяем весь поток предметных событий с результатами рабочих ролей
check_complete_observer()
{
    local log=$1 days=$2 index
    [[ $(grep -c '^День 0: моделирование начато$' "$log") == 1 ]]
    [[ $(grep -c ': день начат$' "$log") == "$days" ]]
    [[ $(grep -c ': день завершён$' "$log") == "$days" ]]
    [[ $(grep -c ': моделирование завершено$' "$log") == 1 ]]
    [[ $(grep -c '^Observer: нормальное завершение$' "$log") == 1 ]]
    {
        grep -E '^День [0-9]+; Reader [0-9]+; (возврат книги|уведомление о возврате|книга зарезервирована, заявка закрыта);' librarian.log || true
        for ((index=0; index<worker_count; ++index)); do
            grep -E '^День [0-9]+; Reader [0-9]+; (приход читателя|запрос книги|получение книги|книга недоступна|выдача отложена по always_wait|создана заявка);' "reader_$index.log" || true
        done
    } | sort > expected_events.txt
    grep -E '^День [0-9]+; Reader [0-9]+;' "$log" | sort > observer_events.txt
    diff expected_events.txt observer_events.txt
}

# управляем границей operation/PHASE_DONE без гонок и изменений рабочего C-кода
raw_send()
{
    local value shift octet
    for value in "$@"; do
        for shift in 24 16 8 0; do
            printf -v octet '%03o' "$(((value >> shift) & 255))"
            printf '%b' "\\$octet" >&"$raw_fd"
        done
    done
}

raw_read()
{
    local bytes_text value offset
    local -a bytes=()
    bytes_text=$(timeout 4s dd bs=24 count=1 iflag=fullblock status=none <&"$raw_fd" | od -An -tu1 -v) || return 1
    bytes_text=${bytes_text//$'\n'/ }
    read -ra bytes <<< "$bytes_text" || return 1
    ((${#bytes[@]} == 24)) || return 1
    raw_frame=()
    for ((offset=0; offset<24; offset+=4)); do
        value=$(( (bytes[offset] << 24) | (bytes[offset+1] << 16) | (bytes[offset+2] << 8) | bytes[offset+3] ))
        ((value < 2147483648)) || value=$((value - 4294967296))
        raw_frame+=("$value")
    done
    printf '%s\n' "${raw_frame[*]}" >> "$case_dir/raw_frames.log"
}

raw_expect()
{
    local expected=$1
    while raw_read; do
        if [[ ${raw_frame[0]} == "$expected" ]]; then return; fi
        [[ ${raw_frame[0]} == 10 || ${raw_frame[0]} == 15 ]] || fail "ожидался type=$expected: ${raw_frame[*]}"
    done
    fail "не получен raw type=$expected"
}

raw_worker()
{
    local index=$1 count idx
    exec {raw_fd}<>/dev/tcp/127.0.0.1/"$port"
    raw_fds+=("$raw_fd")
    if ((index == -1)); then raw_send 1 -1 0 0 0 0; else raw_send 2 "$index" 0 0 0 0; fi
    raw_expect 3
    count=${raw_frame[4]}
    for ((idx=0; idx<count; ++idx)); do raw_expect 5; done
    raw_expect 16
}

raw_close()
{
    local closed_fd=$raw_fd descriptor
    exec {raw_fd}>&-
    local -a remaining=()
    for descriptor in "${raw_fds[@]}"; do
        [[ $descriptor == "$closed_fd" ]] || remaining+=("$descriptor")
    done
    raw_fds=("${remaining[@]}")
}

capture_reconnect_baseline()
{
    start_server fifo.txt
    start_observer
    start_workers fifo.txt
    finish_workers
    stable_result > baseline_stats.txt
    grep -E '^День [0-9]+; Reader [0-9]+;' observer.log | sort > baseline_events.txt
    baseline_dir=$case_dir
    pids=()
    statuses=()
    mkdir resumed
    case_dir="$case_dir/resumed"
    cd "$case_dir"
}

finish_reconnect_run()
{
    local pid
    for pid in "${pids[@]}"; do
        [[ -n ${statuses[$pid]+set} ]] || wait_process "$pid" success
    done
    stable_result > resumed_stats.txt
    grep -E '^День [0-9]+; Reader [0-9]+;' observer.log | sort > resumed_events.txt
    diff "$baseline_dir/baseline_stats.txt" resumed_stats.txt
    diff "$baseline_dir/baseline_events.txt" resumed_events.txt
    [[ $(grep -c '^День 0: моделирование начато$' observer.log) == 1 ]]
    grep -q '^Observer: нормальное завершение$' observer.log
    for pid in "${pids[@]}"; do ! kill -0 "$pid" 2>/dev/null || fail "остался PID $pid"; done
}

resume_reader_test()
{
    local applied=$1
    capture_reconnect_baseline
    start_server fifo.txt
    start_observer
    raw_worker 0
    start_client librarian
    start_client 1
    start_client 2
    raw_expect 8
    [[ ${raw_frame[2]} == 1 ]]
    start_client 0 duplicate_reader
    wait_process "$last_pid" failure
    grep -q 'Ошибка регистрации' duplicate_reader.log
    if ((applied)); then
        raw_send 9 0 1 0 0 0
        raw_expect 11
    fi
    raw_close
    wait_for_line 'Отключился рабочий клиент Reader 0' server.log
    kill -0 "$server_pid"
    start_client 0
    finish_reconnect_run
    [[ $(grep -c '^День 1: запрос Reader 0 обработан$' server.log) == 1 ]]
    if ((applied)); then grep -q 'операция уже применена сервером' reader_0.log; fi
}

resume_librarian_test()
{
    local applied=$1 day
    capture_reconnect_baseline
    start_server fifo.txt
    start_observer
    raw_worker -1
    for index in 0 1 2; do start_client "$index"; done
    for day in 1 2 3 4; do
        raw_expect 6
        [[ ${raw_frame[2]} == "$day" ]]
        if ((day < 4 || applied)); then
            raw_send 7 -1 "$day" 0 0 0
            raw_expect 11
        fi
        if ((day < 4)); then raw_send 12 -1 "$day" 0 0 0; fi
    done
    raw_close
    wait_for_line 'Отключился рабочий клиент Librarian' server.log
    kill -0 "$server_pid"
    start_client librarian
    finish_reconnect_run
    [[ $(grep -c '^День 4: запрос Librarian -1 обработан$' server.log) == 1 ]]
    grep -q 'Синхронизация: день 4;' librarian.log
    if ((applied)); then grep -q 'операция уже применена сервером' librarian.log; fi
}
