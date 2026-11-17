#ifndef CLIENT_H
#define CLIENT_H

#include "network.h"

/** @brief Выполняет независимый клиент Reader или Librarian и возвращает exit code. */
int client_main(int argc, char *argv[], NetworkRole role);

#endif
