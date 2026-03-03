#ifndef OS_USER_MEM_H
#define OS_USER_MEM_H

void* malloc(unsigned long size);
void free(void* ptr);
void* realloc(void* ptr, unsigned long size);
void* calloc(unsigned long num, unsigned long size);

#endif