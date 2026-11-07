#ifndef PROCESS_6_H
#define PROCESS_6_H

#include "simulation.h"

/**
 * @brief Запускает библиотекаря и читателей через fork().
 *
 * Состояние находится в POSIX shared memory, синхронизация использует
 * именованные POSIX-семафоры. Ресурсы создаёт и удаляет родитель.
 * @return true при естественном завершении или остановке по сигналу.
 */
bool process_6_run(Simulation *simulation, int total_days);

#endif
