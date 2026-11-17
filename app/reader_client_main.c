#include "client.h"

int main(int argc, char *argv[])
{
    // независимый процесс одного читателя
    return client_main(argc, argv, NETWORK_READER);
}
