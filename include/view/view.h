#ifndef OS_VIEW_H
#define OS_VIEW_H

#include <stdint.h>
#include <stdarg.h>
#include "lib/safelist.h"

#define VIEW_COLOR_WHITE 0xffffffff
#define VIEW_COLOR_RED 0x00ff0000
#define VIEW_COLOR_GREEN 0x0000ff00
#define VIEW_COLOR_BLUE 0x000000ff
#define VIEW_COLOR_BLACK 0x0
#define MAX_TTYS 8
// 屏幕结构体，每个终端一个
typedef struct {
    uint32_t* vbuffer;      // 像素缓冲区（内存）
    uint16_t disp_c_x;       // 当前字符列
    uint16_t disp_c_y;       // 当前字符行
    uint32_t disp_position;  // 像素偏移（对应显存中的位置）
    spinlock_t lock;
} screen_t;

void color_print(char* str, uint32_t color_back, uint32_t color_fore);
uint32_t color_printf(const char* fmt, uint32_t color_back, uint32_t color_fore, ...);
uint32_t wb_printf(const char *fmt,...);
uint32_t sprintf(char* buf,const char* fmt,uint32_t size,...);
void screen_print(screen_t *scr, const char *str, uint32_t color_back, uint32_t color_fore);
void screen_switch(int idx);
void sync_screen(void);

#endif
