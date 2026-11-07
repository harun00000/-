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
