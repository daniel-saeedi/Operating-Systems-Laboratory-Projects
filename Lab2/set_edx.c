#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

int main(int argc, char const *argv[])
{
	if(argc != 2){
		printf(2, "Usage: set_edx value\n");
		exit();
  	}
	
	set_edx(atoi(argv[1]));
	exit();
}