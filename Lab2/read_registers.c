#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

int main(int argc, char const *argv[])
{
	if(argc != 1){
		printf(2, "Usage: read_registers\n");
		exit();
  	}
	
	read_registers();
	exit();
}