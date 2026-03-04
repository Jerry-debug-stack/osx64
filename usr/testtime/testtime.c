#include <stdint.h>
#include <stddef.h>

#include "usr/usr_const.h"
#include "usr/usr_mem.h"
#include "usr/usr_printf.h"
#include "usr/usr_string.h"
#include "usr/sysapi.h"

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