#ifndef PROCESS_10_H
#define PROCESS_10_H

#include "ipc_10.h"

/** @brief Роли независимых программ с обменом через очереди сообщений. */
typedef enum Process10Role
{
    PROCESS_10_COORDINATOR,
    PROCESS_10_LIBRARIAN,
    PROCESS_10_READER
} Process10Role;

/** @brief Разбирает CLI и выполняет программу выбранной роли. */
// только Coordinator создаёт и удаляет IPC, участники подключаются к нему
int process_10_main(int argc, char *argv[], Process10Role role);

/**
 * @brief Подключает независимого участника, -1 обозначает Librarian.
 * @param ipc Инициализированный локальный набор дескрипторов без владения именами.
 * @param reader_index Пользовательский индекс; диапазон проверяет Coordinator при регистрации.
 */
bool process_10_participant(Ipc10Resources *ipc, int reader_index);

#endif
