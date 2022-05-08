#include <stdio.h>  
#include <stdlib.h>  
#include <sys/time.h>  
#include <time.h>  
#include <sched.h>  
#include <sys/types.h>  
#include <unistd.h>      //pipe()  
  
int main()  
{  
    int x, i, fd[2], p[2];  
    char send    = 's';  
    char receive;  
    pipe(fd);  
    pipe(p);  
    struct timeval tv;  
    struct sched_param param;  
    param.sched_priority = 0;  
  
    while ((x = fork()) == -1); 
    if (x==0) {  
        sched_setscheduler(getpid(), SCHED_FIFO, &param);  
        gettimeofday(&tv, NULL);  
        printf("Before Context Switch Time%u s, %u us\n", tv.tv_sec, tv.tv_usec);  
        for (i = 0; i < 10000; i++) {  
            read(fd[0], &receive, 1);  
            write(p[1], &send, 1);  
        }  
        exit(0);  
    }  
    else {  
        sched_setscheduler(getpid(), SCHED_FIFO, &param);  
        for (i = 0; i < 10000; i++) {  
            write(fd[1], &send, 1);  
            read(p[0], &receive, 1);  
        }  
        gettimeofday(&tv, NULL);  
        printf("After Context SWitch Time%u s, %u us\n", tv.tv_sec, tv.tv_usec);  
    }  
    return 0;  
} 