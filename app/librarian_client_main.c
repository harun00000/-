#include "client.h"

int main(int argc, char *argv[])
{
    // независимый процесс библиотекаря
    return client_main(argc, argv, NETWORK_LIBRARIAN);
}
