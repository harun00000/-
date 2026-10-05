#include "simulation.h"

enum{
    READING_DAYS = 3,
    TOTAL_DAYS = 13
};

int main(void){
    const Librarian librarian = {
        .id = 1,
        .name = "Никита Пичурин"
    };

    const Book books[] = {{1, "Курочка ряба"}, {2, "Убийство в Реутовском экспрессе. 2026г."}};
    const Reader readers[] = {
        {.id = 1, .name = "Даниил Подлягин", .wanted_books = {1, 2}, .wanted_count = 2, .visit_days = {1, 5}, .visit_count = 2}, 
        {.id = 2, .name = "Никита Жилин", .wanted_books = {1}, .wanted_count = 1, .visit_days = {2, 3, 6}, .visit_count = 3}, 
        {.id = 3, .name = "Сергей Крылосов", .wanted_books = {1}, .wanted_count = 1, .visit_days = {2, 4, 10}, .visit_count = 3}};
    
    Simulation simulation;
    simulation_initialization(&simulation, books, (int)(sizeof books / sizeof books[0]), 
        readers, (int)(sizeof readers / sizeof readers[0]), READING_DAYS, &librarian);
    simulation_run(&simulation, TOTAL_DAYS);
    return 0;
}
