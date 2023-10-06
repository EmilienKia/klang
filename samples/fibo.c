
//
// This file is a C starter file of the fibo.k file.
//

#include <stdio.h>

// Fibonacci meyhod implemented in K
unsigned long fibo(unsigned short);

int main() {
    unsigned short param = 8;
    unsigned long res = fibo(param);
    printf("fibo(%hu) : %lu\n", param, res);
    return 0;
}

