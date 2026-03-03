#include "view/view.h"
#include "const.h"
#include "lib/string.h"
#include "mm/mm.h"
#include "multiboot.h"
#include "view/font.h"

#define SCREEN_WIDTH 1280
#define SCREEN_HEIGHT 720
#define CHAR_X 8
#define CHAR_Y 16
#define MAX_ROWS (SCREEN_HEIGHT / CHAR_Y)
#define ROW_SIZE_INTS (SCREEN_WIDTH * CHAR_Y)
#define ROW_SIZE_BYTES (SCREEN_WIDTH * CHAR_Y * 4)

screen_t tty_screens[MAX_TTYS];

// 系统显存对应的屏幕（直接映射到显存）
screen_t system_screen;
int current_tty;

void init_view(MULTIBOOT_INFO* info)
{
    // 初始化系统显存屏幕
    system_screen.vbuffer = easy_phy2linear(info->framebuffer_addr);
    system_screen.disp_c_x = 0;
    system_screen.disp_c_y = 0;
    system_screen.disp_position = 0;
    spin_lock_init(&system_screen.lock);

    // 初始化每个终端屏幕，分配缓冲区
    for (int i = 0; i < MAX_TTYS; i++) {
        screen_t *scr = &tty_screens[i];
        scr->vbuffer = kmalloc(SCREEN_WIDTH * SCREEN_HEIGHT * 4);
        if (!scr->vbuffer) {
            // 处理错误，可打印错误信息
            continue;
        }
        memset(scr->vbuffer, 0, SCREEN_WIDTH * SCREEN_HEIGHT * 4); // 清黑屏
        scr->disp_c_x = 0;
        scr->disp_c_y = 0;
        scr->disp_position = 0;
        spin_lock_init(&scr->lock);
    }
    current_tty = 0;
}

static void print_rect(screen_t *scr, uint32_t position, uint16_t x_size, uint16_t y_size, uint32_t color)
{
    for (uint32_t i = 0; i < y_size; i++) {
        for (uint32_t j = 0; j < x_size; j++) {
            scr->vbuffer[position + i * SCREEN_WIDTH + j] = color;
        }
    }
}

static void set_position(screen_t *scr, uint16_t x_char, uint16_t y_char)
{
    scr->disp_c_x = x_char;
    scr->disp_c_y = y_char;
    scr->disp_position = y_char * ROW_SIZE_INTS + x_char * CHAR_X * 4;
}

static void screen_clear(screen_t *scr, uint32_t color_back)
{
    memset(scr->vbuffer, color_back, SCREEN_WIDTH * SCREEN_HEIGHT * 4);
}

static void screen_moveup(screen_t *scr)
{
    memmove(scr->vbuffer, scr->vbuffer + SCREEN_WIDTH * CHAR_Y,
            SCREEN_WIDTH * (SCREEN_HEIGHT - CHAR_Y) * 4);
    memset(scr->vbuffer + SCREEN_WIDTH * (SCREEN_HEIGHT - CHAR_Y), 0,
           SCREEN_WIDTH * CHAR_Y * 4);
}

static void next_line(screen_t *scr)
{
    scr->disp_c_x = 0;
    scr->disp_c_y++;
    if (scr->disp_c_y >= (SCREEN_HEIGHT / CHAR_Y)) {
        set_position(scr, 0, (SCREEN_HEIGHT / CHAR_Y) - 1);
        screen_moveup(scr);
    } else {
        scr->disp_position = scr->disp_c_y * ROW_SIZE_INTS;
    }
}

static void move_to_next_pos(screen_t *scr)
{
    scr->disp_c_x++;
    if (scr->disp_c_x >= (SCREEN_WIDTH / CHAR_X)) {
        next_line(scr);
    } else {
        scr->disp_position += CHAR_X;
    }
}

static void back_space(screen_t *scr)
{
    if (scr->disp_c_x == 0 && scr->disp_c_y == 0)
        return;
    if (scr->disp_c_x == 0) {
        scr->disp_c_y--;
        scr->disp_c_x = (SCREEN_WIDTH / CHAR_X) - 1;
    } else {
        scr->disp_c_x--;
    }
    scr->disp_position = scr->disp_c_y * ROW_SIZE_INTS + scr->disp_c_x * CHAR_X;
}

static void print_char(screen_t *scr, uint8_t ch, uint32_t color_back, uint32_t color_fore)
{
    if (ch == BACK_SPACE) {
        back_space(scr);
        print_rect(scr, scr->disp_position, CHAR_X, CHAR_Y, color_back);
    } else if (ch == NEXT_LINE) {
        next_line(scr);
    } else if (ch == NEXT_PAGE) {
        screen_clear(scr, color_back);
        set_position(scr, 0, 0);
    } else {
        for (uint8_t i = 0; i < CHAR_Y; i++) {
            for (uint8_t j = 0; j < CHAR_X; j++) {
                if ((font_ascii[ch][i] >> (7 - j)) & 0x1)
                    scr->vbuffer[scr->disp_position + i * SCREEN_WIDTH + j] = color_fore;
                else
                    scr->vbuffer[scr->disp_position + i * SCREEN_WIDTH + j] = color_back;
            }
        }
        move_to_next_pos(scr);
    }
}

void screen_print(screen_t *scr, const char *str, uint32_t color_back, uint32_t color_fore)
{
    spin_lock(&scr->lock);
    uint32_t i = 0;
    while (str[i] != 0) {
        print_char(scr, (uint8_t)str[i], color_back, color_fore);
        i++;
    }
    spin_unlock(&scr->lock);
}

void color_print(char* str, uint32_t color_back, uint32_t color_fore)
{
    screen_print(&system_screen, str, color_back, color_fore);
}

void screen_switch(int idx)
{
    if (idx < 0 || idx >= MAX_TTYS) return;
    if (!tty_screens[idx].vbuffer) return; // 未初始化

    screen_t *src = &tty_screens[idx];
    screen_t *dst = &system_screen;

    spin_lock(&src->lock);
    spin_lock(&dst->lock);

    memcpy(dst->vbuffer, src->vbuffer, SCREEN_WIDTH * SCREEN_HEIGHT * 4);
    dst->disp_c_x = src->disp_c_x;
    dst->disp_c_y = src->disp_c_y;
    dst->disp_position = src->disp_position;

    current_tty = idx;

    spin_unlock(&dst->lock);
    spin_unlock(&src->lock);
}
