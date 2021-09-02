#include "types.h"
#include "user.h"

int main(int argc, char const *argv[])
{
    printf(1, "------------------------------------------------------\n");
    printf(1, "> sending request to get page 1:\n");
    printf(1, "    %x\n", (void*)shmget(1));
    printf(1, "------------------------------------------------------\n");

    printf(1, "> fork...\n");
    printf(1, "-------------------------------------------------------\n");
    int pid = fork();
    if (pid == 0)
    {
	    printf(1, "> sending request from child process to get page 1:\n");
	    printf(1, "    %x\n", (void*)shmget(1));
        printf(1, "-------------------------------------------------------\n");

        printf(1, "> sending request from child process to get page 3:\n");
	    printf(1, "    %x\n", (void*)shmget(3));
	    printf(1, "-------------------------------------------------------\n");

	    printf(1, "> sending request from child process to get page 0:\n");
	    printf(1, "    %x\n", (void*)shmget(0));
	    printf(1, "-------------------------------------------------------\n");

	    printf(1, "> sending request from child process to get page 2:\n");
	    printf(1, "    %x\n", (void*)shmget(2));
	    printf(1, "-------------------------------------------------------\n");

        exit();
    }

    if (pid < 0)
    {
        printf(1, "fork failed!\n");
        exit();
    }

    sleep(100);

	printf(1, "> sending request from parent process to get page 2:\n");
	printf(1, "    %x\n", (void*)shmget(2));
    printf(1, "-------------------------------------------------------\n");

	printf(1, "> sending request from parent process to get page 2:\n");
	printf(1, "    %x\n", (void*)shmget(2));
	printf(1, "-------------------------------------------------------\n");

	printf(1, "> sending request from parent process to get page 3:\n");
	printf(1, "    %x\n", (void*)shmget(3));
	printf(1, "-------------------------------------------------------\n");

    printf(1, "> sending request from parent process to get page 8:\n");
	printf(1, "    %x\n", (void*)shmget(8));
	printf(1, "-------------------------------------------------------\n");

	printf(1, "> sending request from parent process to get page 1:\n");
	printf(1, "    %x\n", (void*)shmget(1));
	printf(1, "-------------------------------------------------------\n");

    wait();
	exit();
}
