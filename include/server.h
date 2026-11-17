#ifndef SERVER_H
#define SERVER_H

#include "network.h"
#include "simulation.h"

/** @brief Обслуживает TCP-клиентов через poll; изменяет только серверную Simulation. */
bool server_run(Simulation *simulation, const NetworkOptions *options);

#endif
