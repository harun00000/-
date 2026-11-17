#ifndef PROCESS_7_8_H
#define PROCESS_7_8_H

/** @brief Роль независимо запускаемой программы. */
typedef enum ProcessRole
{
    PROCESS_COORDINATOR,
    PROCESS_LIBRARIAN,
    PROCESS_READER
} ProcessRole;

/** @brief Разбирает параметры и выполняет программу выбранной роли. */
// роль фиксирует исполняемый файл, session задаётся пользователем
int process_7_8_main(int argc, char *argv[], ProcessRole role);

#endif
