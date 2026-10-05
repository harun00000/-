cmake_minimum_required(VERSION 3.16)

set(CMAKE_C_COMPILER gcc)
project(library_sim LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(library_sim
    app/main.c
    src/book.c
    src/reader.c
    src/request.c
    src/simulation.c
)
target_include_directories(library_sim PRIVATE include)
target_compile_options(library_sim PRIVATE -Wall -Wextra -Wpedantic)
