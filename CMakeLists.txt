cmake_minimum_required(VERSION 3.16)

set(CMAKE_C_COMPILER gcc)
project(library_sim LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_executable(library_sim
    app/main.c
    src/book.c
    src/reader.c
    src/request.c
    src/simulation.c
    src/main_utils.c
    src/config.c
    src/logger.c
    src/statistics.c
)
target_include_directories(library_sim PRIVATE include)
target_compile_options(library_sim PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(library_sim PRIVATE Threads::Threads)

add_executable(library_4_5
    app/main.c
    src/book.c
    src/reader.c
    src/request.c
    src/simulation.c
    src/main_utils.c
    src/config.c
    src/logger.c
    src/statistics.c
    src/process_4_5.c
)
target_include_directories(library_4_5 PRIVATE include)
target_compile_definitions(library_4_5 PRIVATE LIBRARY_PROCESS_4_5)
target_compile_options(library_4_5 PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(library_4_5 PRIVATE rt)

add_executable(library_6
    app/main.c
    src/book.c
    src/reader.c
    src/request.c
    src/simulation.c
    src/main_utils.c
    src/config.c
    src/logger.c
    src/statistics.c
    src/process_6.c
)
target_include_directories(library_6 PRIVATE include)
target_compile_definitions(library_6 PRIVATE LIBRARY_PROCESS_6)
target_compile_options(library_6 PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(library_6 PRIVATE rt)

add_library(library_7_8_common STATIC
    src/book.c
    src/reader.c
    src/request.c
    src/simulation.c
    src/main_utils.c
    src/config.c
    src/logger.c
    src/statistics.c
    src/process_7_8.c
)
target_include_directories(library_7_8_common PUBLIC include)
target_compile_definitions(library_7_8_common PUBLIC LIBRARY_PROCESS_7_8)
target_compile_options(library_7_8_common PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(library_7_8_common PUBLIC rt)

foreach(role coordinator librarian reader)
    add_executable(library_7_8_${role} app/${role}_7_8.c)
    target_compile_options(library_7_8_${role} PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(library_7_8_${role} PRIVATE library_7_8_common)
endforeach()

add_library(library_10_common STATIC
    src/book.c
    src/reader.c
    src/request.c
    src/simulation.c
    src/main_utils.c
    src/config.c
    src/logger.c
    src/statistics.c
    src/ipc_10.c
    src/participant_10.c
    src/process_10.c
)
target_include_directories(library_10_common PUBLIC include)
target_compile_definitions(library_10_common PUBLIC LIBRARY_PROCESS_10)
target_compile_options(library_10_common PRIVATE -Wall -Wextra -Wpedantic)
include(CheckSymbolExists)
check_symbol_exists(mq_open "mqueue.h" LIBRARY_10_MQ_IN_LIBC)
if(NOT LIBRARY_10_MQ_IN_LIBC)
    target_link_libraries(library_10_common PUBLIC rt)
endif()

foreach(role coordinator librarian reader)
    add_executable(library_10_${role} app/${role}_10.c)
    target_compile_options(library_10_${role} PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(library_10_${role} PRIVATE library_10_common)
endforeach()
