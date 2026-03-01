#ifndef OS_KEYBOARD_H
#define OS_KEYBOARD_H

#include "lib/safelist.h"

#define OUTPUT_BUFFER_IO 0x60
#define INPUT_BUFFER_IO 0x60
#define STATUS_REGISTER_IO 0x64
#define CONTROL_REGISTER_IO 0x64

#define KEYBOARD_BUFFER_SIZE 256

// 键码类型：低8位为键值，高8位为修饰标志
typedef uint16_t keycode_t;

// 修饰键标志
#define MOD_SHIFT   (1 << 8)
#define MOD_CTRL    (1 << 9)
#define MOD_ALT     (1 << 10)

// 特殊键码（0x80 - 0xFF）
#define KEY_UP      0x80
#define KEY_DOWN    0x81
#define KEY_LEFT    0x82
#define KEY_RIGHT   0x83
#define KEY_HOME    0x84
#define KEY_END     0x85
#define KEY_PGUP    0x86
#define KEY_PGDN    0x87
#define KEY_INSERT  0x88
#define KEY_DELETE  0x89
#define KEY_F1      0x8A
#define KEY_F2      0x8B
#define KEY_F3      0x8C
#define KEY_F4      0x8D
#define KEY_F5      0x8E
#define KEY_F6      0x8F
#define KEY_F7      0x90
#define KEY_F8      0x91
#define KEY_F9      0x92
#define KEY_F10     0x93
#define KEY_F11     0x94
#define KEY_F12     0x95

typedef struct {
    volatile uint32_t head;
    volatile uint32_t tail;
    keycode_t buffer[KEYBOARD_BUFFER_SIZE];
    uint8_t shift : 1;
    uint8_t ctrl : 1;
    uint8_t alt : 1;
    uint8_t caps_lock : 1;
    uint8_t e0_prefix : 1;
    spinlock_t lock;
} kbd_status_t;

extern kbd_status_t *kbd_status;

void init_keyboard(void);
void keyboard_input_put(keycode_t code);
size_t keyboard_read_events(keycode_t *buf, size_t max_events);

#endif