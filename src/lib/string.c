#include <stdint.h>

void memcpy(void* dest,void* source,uint32_t length){
    uint8_t* d = dest;
    uint8_t* s = source;
    for (uint32_t i = 0; i < length; i++)
        d[i] = s[i];
}

uint8_t memcmp(void* a,void* b,uint32_t length){
    uint8_t* p = a;
    uint8_t* q = b;
    for (uint32_t i = 0; i < length; i++)
        if(p[i] != q[i])
            return 0;
    return 1;
}

uint32_t strlen(char* str){
    uint32_t length = 0;
    while (str[length])
        length++;
    return length;
}

uint32_t strcpy(char* dest,char* source){
    uint32_t i = 0;
    while (source[i]){
        dest[i] = source[i];
        i++;
    }
    dest[i] = 0;
    return i;
}

uint8_t strcmp(char* dest,char* source){
    uint32_t i = 0;
    while (source[i]){
        if(dest[i] != source[i])
            return 0;
        i++;
    }
    if(dest[i] == '\0')
        return 1;
    else return 0;
}

void memset(void* dest,uint8_t data,uint32_t size){
    uint8_t* d = dest;
    for (uint32_t i = 0; i < size; i++)
        d[i] = data;
}

