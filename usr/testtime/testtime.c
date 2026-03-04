#include <stdint.h>
#include <stddef.h>

#include "uconst.h"
#include "mem.h"
#include "printf.h"
#include "ustring.h"
#include "sysapi.h"

int main(void){
    int seconds = get_ticks();
    printf("secounds %d\n",seconds);
    while (1)
    {
        int i = get_ticks();
        if ((i - seconds) > 5000){
            printf("timer 1 second\n");
            seconds += 5000;
        }
        yield();
    }
    
}