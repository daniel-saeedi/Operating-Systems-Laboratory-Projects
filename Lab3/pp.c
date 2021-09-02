#include "user.h"

int
main()
{
    int status = print_processes();
    printf(1, "-----------------------------------------------------------\n\
        All processes printed with status %d\n-----------------------------------------------------------\n", status);
    exit();
}
