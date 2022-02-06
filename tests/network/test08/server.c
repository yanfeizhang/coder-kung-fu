#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CONNECTION_NUM 1100000

int main(int argc, char *argv[])
{
    char ip[16];
    int lisFd, conFd, port;
    struct sockaddr_in servAddr; 
    pid_t pid;


    if(argc != 3)
    {
        printf("\n Usage: %s <server ip> <server port>\n", argv[0]);
        return 1;
    }

    pid = getpid();

    //1. 从命令行获取并解析server ip以及端口
    strcpy(ip, argv[1]);
    port = atoi(argv[2]);

    //2. 启动服务
    //2.1 创建server socket
    if((lisFd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        printf("Server %d Error : Could not create socket!\n", pid);
        return 0;
    }

    //2.2 设置端口重用
    int val =1;
    if (setsockopt(lisFd, SOL_SOCKET, SO_REUSEPORT, &val, sizeof(val))<0) 
    {
        printf("\n Server %d Error : setsockopt failed!\n", pid);
        return 0;        
    } 

    //2.3 bind绑定
    servAddr.sin_family = AF_INET;
    servAddr.sin_addr.s_addr = inet_addr(ip);
    servAddr.sin_port = htons(port); 
    if(bind(lisFd, (struct sockaddr*)&servAddr, sizeof(servAddr)) < 0)
    {
        printf("\n Server %d Error : Bind Failed! \n", pid);
        return 0;
    }

    //2.4 启动监听
    if((listen(lisFd, 1024)) < 0)
    {
        printf("\n Server %d Error : Listen Failed \n", pid);
        return 0;
    }
    else
    {
        printf("\n Start server on %s:%d successed, pid is %d\n", ip, port, pid);
    }

    //3. 接收连接
    int i = 0;
    int *sockets = (int *)malloc(sizeof(int) * MAX_CONNECTION_NUM);
    while(1)
    {
        conFd = accept(lisFd, (struct sockaddr*)NULL, NULL); 
        if(conFd > 0)
        {
            sockets[i++] = conFd;
            printf("Server %s %d (%d) accept success:%d\n", ip, port, pid, i);
        }
    }
}