#ifndef OS_STRING_H
#define OS_STRING_H

#include <stdint.h>

void memcpy(void* dest, void* source, uint32_t length);
uint8_t memcmp(void* a, void* b, uint32_t length);
uint32_t strlen(const char* str);
uint32_t strcpy(char* dest, char* source);
uint8_t strcmp(const char *s1, const char *s2);
char *kstrdup(const char *s);
char *strrchr(const char *s, int c);
char *strtok_r(char *str, const char *delim, char **saveptr);
int strncmp(const char *s1, const char *s2, uint64_t n);
void memset(void* dest, uint8_t data, uint32_t size);

#endif
