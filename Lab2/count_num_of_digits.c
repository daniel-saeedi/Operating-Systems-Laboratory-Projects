#include "types.h"
#include "stat.h"
#include "fcntl.h"
#include "user.h"

int main(int argc, char *argv[])
{
   if(argc != 2){
      printf(2, "Usage: count_num_of_digits number\n");
      exit();
   }
   int number = atoi(argv[1]);
   int reg_value;

   __asm__("movl %%edx, %0" : "=r" (reg_value));
   __asm__("movl %0, %%edx" :  : "r"(number) );

   count_num_of_digits();

   __asm__("movl %0, %%edx" :  : "r"(reg_value));
   exit();
}