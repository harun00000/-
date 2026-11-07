#ifndef PROCESS_4_5_H
#define PROCESS_4_5_H

#include "simulation.h"

/**
 * @brief Запускает библиотекаря и читателей через fork().
 *
 * Данные и неименованные семафоры находятся в POSIX shared memory.
 * Родитель управляет днями, собирает детей и освобождает ресурсы.
 * Итоговое состояние сохраняется в simulation.
 * @return true при естественном завершении или остановке по сигналу.
 */
bool process_4_5_run(Simulation *simulation, int total_days);

#endif
