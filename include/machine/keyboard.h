#ifndef OS_KEYBOARD_H
#define OS_KEYBOARD_H

#include "lib/safelist.h"

#define OUTPUT_BUFFER_IO 0x60
#define INPUT_BUFFER_IO 0x60
#define STATUS_REGISTER_IO 0x64
#define CONTROL_REGISTER_IO 0x64

// keyboard.h
#define KEYBOARD_BUFFER_SIZE 256

typedef struct {
    volatile uint32_t head;          // 生产者索引（中断中更新）
    volatile uint32_t tail;          // 消费者索引（tty读取时更新）
    char buffer[KEYBOARD_BUFFER_SIZE];
    // 修饰键状态
    uint8_t shift : 1;
    uint8_t ctrl : 1;
    uint8_t alt : 1;
    uint8_t caps_lock : 1;
    uint8_t num_lock : 1;
    uint8_t scroll_lock : 1;
    uint8_t e0_prefix : 1;           // 处理扩展键前缀 0xE0
    spinlock_t lock;                  // 保护整个结构
} kbd_status_t;

extern kbd_status_t *kbd_status;

#endif