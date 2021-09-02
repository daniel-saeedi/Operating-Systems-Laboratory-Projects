#include "types.h"
#include "user.h"

#define ONE 1

int main(int argc, char const *argv[])
{
	int* pointer;
	int number = 8;

	pointer = (void*)0;

	number = (int)*pointer;


	printf(ONE, "NULL Pointer Content: %d\n", number);

	exit();
}
