#include "user.h"


int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        printf(1, "command must have 1 argument, %d given.\n", argc - 1);
        exit();
    }
    int milli_seconds = atoi(argv[1]);
    int cpid = fork();
    if (cpid < 0)
    {
        printf(1, "fork problem!\n");
        exit();
    }
    if (cpid == 0)
    {
        set_alarm(milli_seconds);
        exit();
    }
    exit();
}
