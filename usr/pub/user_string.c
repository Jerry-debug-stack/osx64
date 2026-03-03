#include <stdint.h>

void memcpy(void* dest, void* source, uint32_t length)
{
    uint8_t* d = dest;
    uint8_t* s = source;
    for (uint32_t i = 0; i < length; i++)
        d[i] = s[i];
}

void *memmove(void *dest, void *src, uint32_t n) {
    unsigned char *d = dest;
    unsigned char *s = src;
    if (d < s) {
        for (uint32_t i = 0; i < n; i++)
            d[i] = s[i];
    } else if (d > s) {
        for (uint32_t i = n; i > 0; i--)
            d[i-1] = s[i-1];
    }
    return dest;
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

char *strchr(const char *s, int c) {
    while (*s != '\0') {
        if (*s == (char)c) {
            return (char *)s;
        }
        s++;
    }
    if ((char)c == '\0') {
        return (char *)s;
    }
    return (void *)0;
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

char *strtok_r(char *str, const char *delim, char **saveptr)
{
    char *token_start;
    const char *d;

    if (str == (void *)0)
        str = *saveptr;
    while (*str != '\0') {
        int is_delim = 0;
        for (d = delim; *d != '\0'; d++) {
            if (*str == *d) {
                is_delim = 1;
                break;
            }
        }
        if (!is_delim)
            break;
        str++;
    }
    if (*str == '\0') {
        *saveptr = str;
        return (void *)0;
    }
    token_start = str;
    while (*str != '\0') {
        int is_delim = 0;
        for (d = delim; *d != '\0'; d++) {
            if (*str == *d) {
                is_delim = 1;
                break;
            }
        }
        if (is_delim)
            break;
        str++;
    }
    if (*str != '\0') {
        *str = '\0';
        *saveptr = str + 1;
    } else {
        *saveptr = str;
    }

    return token_start;
}

int strncmp(const char *s1, const char *s2, uint64_t n)
{
    while (n-- > 0) {
        if (*s1 != *s2)
            return (*(unsigned char *)s1 - *(unsigned char *)s2);
        if (*s1 == '\0')
            return 0;
        ++s1;
        ++s2;
    }
    return 0;
}
