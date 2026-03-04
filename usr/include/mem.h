#ifndef OS_USER_MEM_H
#define OS_USER_MEM_H

#include <stdint.h>

void* malloc(uint64_t size);
void free(void* ptr);
void* realloc(void* ptr, uint64_t size);
void* calloc(uint64_t num, uint64_t size);

#endif