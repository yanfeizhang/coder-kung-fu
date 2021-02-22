#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h> 


#define MAX_CONNECTION_NUM 50000

int buildConnect(const char *sIp, int sPort)
{
    int skFd; 
    if((skFd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        printf("\n Error : Could not create socket\n");
        return 0;
    }

    struct sockaddr_in srvAddr;
    srvAddr.sin_family = AF_INET;
    srvAddr.sin_addr.s_addr = inet_addr(sIp);
    srvAddr.sin_port = htons(sPort); 
    if(connect(skFd, (struct sockaddr *)&srvAddr, sizeof(srvAddr)) < 0)
    {
       printf("\n Error : Connect Failed \n");
       return 0;
    } 

    return skFd;
}

int main(int argc, char *argv[])
{
    int i = 0, sPort, fd;
    char sIp[16];

    if(argc != 3)
    {
        printf("\n Usage: %s <server ip> <server port>\n", argv[0]);
        return 1;
    }

    //1. 从命令行获取并解析server ip以及端口
    strcpy(sIp, argv[1]);
    sPort = atoi(argv[2]);
    
    //2. 开始建立连接
    int *sockets = (int *)malloc(sizeof(int) * MAX_CONNECTION_NUM);
    for(i = 1; i <= MAX_CONNECTION_NUM; i++)
    {
        if(0 == i % 1000)
        {//稍稍停顿一下，避免把服务端的握手队列打满
            printf("连接 %s:%d成功了 %d 条！\n", sIp, sPort, i);
            sleep(1);
        }
        
        fd = buildConnect(sIp, sPort);
        if(fd > 0)
        {
            sockets[i-1] = fd;
        }else{
            return 1;
        }
    }
    sleep(300);

    //3. 释放所有的连接
    printf("关闭所有的连接...\n");
    for(i = 0; i < MAX_CONNECTION_NUM; i++)
    {
        close(sockets[i]);
    }
 
    return 0;
}
