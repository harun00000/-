#include "process_7_8.h"

int main(int argc, char *argv[])
{
    // управляющая программа создаёт IPC и ожидает независимых участников
    return process_7_8_main(argc, argv, PROCESS_COORDINATOR);
}
