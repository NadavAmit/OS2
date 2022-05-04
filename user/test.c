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
    int pid=0;
    int proccessArr[100];
    int i;
    int processCounter = 1;
    for(i=0;i<counter;i++){
        processCounter++;
        pid = fork();
        proccessArr[pid]=1;
    }
    printf("Total processes that has existed: %d\n",processCounter);
    for(i=0;i<100;i++){
        if(proccessArr[i]==1){
            printf("Procces with pid: %d is created\n",i);
        }
    }
    return;
}

int
main(int argc, char *argv[])
{
    procIdTest(2);
    exit(0);
}