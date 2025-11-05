#include "const.h"
#include "view/view.h"
#include "view/font.h"
#include "multiboot.h"
#include "mm/mm.h"
#include "lib/string.h"

#define SCREEN_WIDTH    1280
#define SCREEN_HEIGHT   720
#define CHAR_X          8
#define CHAR_Y          16
#define MAX_ROWS        (SCREEN_HEIGHT / CHAR_Y)
#define ROW_SIZE_INTS   (SCREEN_WIDTH * CHAR_Y)
#define ROW_SIZE_BYTES  (SCREEN_WIDTH * CHAR_Y * 4)

static struct
{
    uint32_t* vbuffer;
    uint32_t disp_position;
    uint16_t disp_c_x;
    uint16_t disp_c_y;
}vMm;

static void screen_clear(uint32_t color_back);

void init_view(MULTIBOOT_INFO* info){
    vMm.vbuffer = easy_phy2linear(info->framebuffer_addr);
    vMm.disp_position = 0;
    vMm.disp_c_x = 0;
    vMm.disp_c_y = 0;
}

static void copy_creen(uint32_t** buffer){
    for (uint32_t i = 0; i < MAX_ROWS; i++)
    {
        memcpy(vMm.vbuffer + i * ROW_SIZE_BYTES, buffer[i],ROW_SIZE_BYTES);
    }
}

static void print_rect(uint32_t position,uint16_t x_size,uint16_t y_size,uint32_t color){
    for (uint32_t i = 0; i < y_size; i++)
    {
        for (uint32_t j = 0; j < x_size; j++)
        {
            vMm.vbuffer[position + i * SCREEN_WIDTH + j] = color;
        }
    }
}

static void set_position(uint16_t x_char,uint16_t y_char){
    vMm.disp_c_x = x_char;
    vMm.disp_c_y = y_char;
    vMm.disp_position = y_char * ROW_SIZE_INTS + x_char;
}

static void screen_clear(uint32_t color_back){
    memset(vMm.vbuffer, color_back, SCREEN_WIDTH * SCREEN_HEIGHT * 4);
}

static void next_line(void){
    vMm.disp_c_x = 0;
    vMm.disp_c_y++;
    if(vMm.disp_c_y >= (SCREEN_HEIGHT / CHAR_Y)){
        vMm.disp_c_y = 0;
        vMm.disp_position = 0;
    }else{
        vMm.disp_position = vMm.disp_c_y * ROW_SIZE_INTS;
    }
}

static void move_to_next_pos(void){
    vMm.disp_c_x++;
    if(vMm.disp_c_x >= (SCREEN_WIDTH / CHAR_X)){
        next_line();
    }else{
        vMm.disp_position += CHAR_X;
    }
}

static void back_space(void){
    if(vMm.disp_c_x == 0 && vMm.disp_c_y == 0)
        return;
    if(vMm.disp_c_x == 0){
        vMm.disp_c_y--;
        vMm.disp_c_x = (SCREEN_WIDTH / CHAR_X) - 1;
    }else{
        vMm.disp_c_x--;
    }
    vMm.disp_position = vMm.disp_c_y * ROW_SIZE_INTS + vMm.disp_c_x * CHAR_X;
}

static void print_char(uint8_t ch,uint32_t color_back,uint32_t color_fore){
    if(ch == BACK_SPACE){
        back_space();
        print_rect(vMm.disp_position,CHAR_X,CHAR_Y,VIEW_COLOR_BLACK);
    }else if (ch == NEXT_LINE){
        next_line();
    }else if (ch == NEXT_PAGE){
        screen_clear(VIEW_COLOR_BLACK);
        set_position(0,0);
    }else{
        for (uint8_t i = 0; i < CHAR_Y; i++)
        {
            for (uint8_t j = 0; j < CHAR_X; j++)
            {
                if((font_ascii[ch][i] >> (7 - j)) & 0x1)
                    vMm.vbuffer[vMm.disp_position + i * SCREEN_WIDTH + j] = color_fore;
                else
                    vMm.vbuffer[vMm.disp_position + i * SCREEN_WIDTH + j] = color_back;                
            }
        }
        move_to_next_pos();
    }
}

void low_print(char* str,uint32_t color_back,uint32_t color_fore){
    uint32_t i = 0;
    while (str[i] != 0)
    {
        print_char((uint8_t)str[i],color_back,color_fore);
        i++;
    }
}
