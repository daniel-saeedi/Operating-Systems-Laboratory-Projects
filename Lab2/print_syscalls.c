#include "user.h"

int main(int argc, char const *argv[])
{
	if(argc != 1){
		printf(2, "Usage: print_syscalls\n");
		exit();
  	}
	
	print_syscalls();
	exit();
}