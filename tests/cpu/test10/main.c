#include <stdio.h>
#include "libadd.h"

int main(void)
{
    int ret = add(2,3);
    printf("C调用Go函数2+3=%d", ret);
    return 0;
}