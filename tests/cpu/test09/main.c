#include <stdio.h>
#include <stdlib.h>
#include <math.h>

void caculate(){
    for (i = 0; i < 10000000; i++) {
    }
}

void funcE(){
    caculate();
}

void funcD(){
    funcE();
}

void funcA(){
    funcD();
}

void funcB(){
    caculate();
}

void funcC(){
    caculate();
}

int main() {
    int i;
    for (i = 0; i < 100; i++) {
        if (i < 10) {
            funcA();
        } else if (i < 16) {
            funcB();
        } else {
            funcC();
        }
    }

    return 0;
}
