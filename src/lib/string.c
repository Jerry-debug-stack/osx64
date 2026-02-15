#include <stdint.h>
#include "mm/mm.h"

void memcpy(void* dest, void* source, uint32_t length)
{
    uint8_t* d = dest;
    uint8_t* s = source;
    for (uint32_t i = 0; i < length; i++)
        d[i] = s[i];
}

uint8_t memcmp(void* a, void* b, uint32_t length)
{
    uint8_t* p = a;
    uint8_t* q = b;
    for (uint32_t i = 0; i < length; i++)
        if (p[i] != q[i])
            return 0;
    return 1;
}

uint32_t strlen(const char* str)
{
    uint32_t length = 0;
    while (str[length])
        length++;
    return length;
}

uint32_t strcpy(char* dest, char* source)
{
    uint32_t i = 0;
    while (source[i]) {
        dest[i] = source[i];
        i++;
    }
    dest[i] = 0;
    return i;
}

uint8_t strcmp(const char *s1, const char *s2)
{
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

void memset(void* dest, uint8_t data, uint32_t size)
{
    uint8_t* d = dest;
    for (uint32_t i = 0; i < size; i++)
        d[i] = data;
}

char *kstrdup(const char *s) {
    uint32_t len = strlen(s) + 1;
    char *p = kmalloc(len);
    if (p) memcpy(p, (void*)s, len);
    return p;
}

char *strrchr(const char *s, int c) {
    const char *last = (void*)0;
    char ch = (char)c;
    while (*s) {
        if (*s == ch) {
            last = s;
        }
        ++s;
    }
    if (ch == '\0') {
        return (char *)s;
    }
    return (char *)last;
}
