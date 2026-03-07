#ifndef OS_USER_PRINTF
#define OS_USER_PRINTF

#include <stdint.h>

uint32_t sprintf(char* buf,const char* fmt,uint32_t size,...);
uint32_t printf(const char* fmt,...);
int sscanf(const char *str, const char *fmt, ...);

#endif
