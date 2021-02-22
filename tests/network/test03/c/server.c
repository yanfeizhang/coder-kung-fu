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

    if(argc != 3)
    {
        printf("\n Usage: %s <server ip> <server port>\n", argv[0]);
        return 1;
    }

    //1. 从命令行获取并解析server ip以及端口
    strcpy(ip, argv[1]);
    port = atoi(argv[2]);

    //2. 创建server
    if((lisFd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        printf("\n Error : Could not create socket\n");
        return 0;
    }

    servAddr.sin_family = AF_INET;
    servAddr.sin_addr.s_addr = inet_addr(ip);
    servAddr.sin_port = htons(port); 
    if(bind(lisFd, (struct sockaddr*)&servAddr, sizeof(servAddr)) < 0)
    {
        printf("\n Error : Bind Failed \n");
    }
    if((listen(lisFd, 1024)) < 0)
    {
        printf("\n Error : Listen Failed \n");
    }

    //3. 接收连接
    int i = 0;
    int *sockets = (int*)malloc(sizeof(int) * MAX_CONNECTION_NUM);
    while(1)
    {
        conFd = accept(lisFd, (struct sockaddr*)NULL, NULL); 
        if(conFd > 0)
        {
            sockets[i++] = conFd;
            printf("%s %d accept success:%d\n", ip, port, i);
        }
    }
}