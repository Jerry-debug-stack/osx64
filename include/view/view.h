#ifndef OS_VIEW_H
#define OS_VIEW_H

#include <stdint.h>
#include <stdarg.h>

#define VIEW_COLOR_WHITE 0xffffffff
#define VIEW_COLOR_RED 0x00ff0000
#define VIEW_COLOR_GREEN 0x0000ff00
#define VIEW_COLOR_BLUE 0x000000ff
#define VIEW_COLOR_BLACK 0x0

void color_print(char* str, uint32_t color_back, uint32_t color_fore);
uint32_t color_printf(const char* fmt, uint32_t color_back, uint32_t color_fore, ...);
uint32_t wb_printf(const char *fmt,...);
uint32_t sprintf(char* buf,const char* fmt,uint32_t size,...);

#endif
