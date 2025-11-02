#ifndef OS_STRING_H
#define OS_STRING_H

#include <stdint.h>

void memcpy(void* dest,void* source,uint32_t length);
uint8_t memcmp(void* a,void* b,uint32_t length);
uint32_t strlen(char* str);
uint32_t strcpy(char* dest,char* source);
uint8_t strcmp(char* dest,char* source);
void memset(void* dest,uint8_t data,uint32_t size);

#endif
