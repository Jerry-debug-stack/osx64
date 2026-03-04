#include <stdint.h>
#include <stddef.h>

#include "uconst.h"
#include "mem.h"
#include "uprintf.h"
#include "ustring.h"
#include "sysapi.h"

int main(void){
    uint64_t seconds = time();
    utimespec_t u;
    utimespec_t u1;
    clock_gettime(&u);
    clock_gettime(&u1);
    printf("clock_gettime: second %lu,nano %lu\n",u.tv_sec,u.tv_nsec);
    printf("clock_gettime: second %lu,nano %lu\n",u1.tv_sec,u1.tv_nsec);
    int l = 20;
    while (l > 0)
    {
        uint64_t i = time();
        if (i != seconds){
            printf("%d\n",i);
            seconds = i;
            l--;
        }
        yield();
    }
    exit(0);
}