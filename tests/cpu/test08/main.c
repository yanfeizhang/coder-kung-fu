#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/perf_event.h>
 
// 封装perf_event_open系统调用
int perf_event_open(struct perf_event_attr *attr,pid_t pid,int  ,int group_fd,unsigned long flags)
{
    return syscall(__NR_perf_event_open,attr,pid,cpu,group_fd,flags);
}
 
int main()
{
    // 第一步：创建perf文件描述符
    // 准备参数
    struct perf_event_attr attr;
    memset(&attr,0,sizeof(struct perf_event_attr));
    attr.size=sizeof(struct perf_event_attr);
    attr.type=PERF_TYPE_HARDWARE; // 监测硬件
    attr.config=PERF_COUNT_HW_INSTRUCTIONS; // 监测指令数
    
    // pid=0表示只检测当前进程
    // cpu=-1表示检测所有cpu核
    int fd=perf_event_open(&attr,0,-1,-1,0);
    if(fd<0)
    {
        perror("Cannot open perf fd!");
        return 1;
    }

    // 第二步：定时获取指标计数
    // 每隔1秒打印一次当前计数
    while(1)
    {
        uint64_t instructions;
        read(fd,&instructions,sizeof(instructions));
        printf("instructions=%ld\n",instructions);
        sleep(1);
    }
}

