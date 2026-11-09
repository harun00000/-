#include "process_10.h"

int main(int argc, char *argv[])
{
    // управляющая программа создаёт IPC и ожидает независимых участников
    return process_10_main(argc, argv, PROCESS_10_COORDINATOR);
}
