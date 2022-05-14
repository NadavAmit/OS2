#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "kernel/syscall.h"
#include "kernel/memlayout.h"
#include "kernel/riscv.h"


void procIdTest(int counter){
   for(int i =0; i<counter; i++){
       fork();
   }
}

int
main(int argc, char *argv[])
{
    procIdTest(1);
    printf("hello World;");
    exit(0);
}