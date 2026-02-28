#include "const.h"
#include "mm/mm.h"
#include "string.h"
#include "machine/keyboard.h"
#include "lib/io.h"
#include <stdint.h>

kbd_status_t *kbd_status;

void set_handler(uint64_t irq, uint64_t addr);
void disable_irq(uint16_t irq);
void enable_irq(uint16_t irq);
void set_EOI(void);

static void handle_scancode(uint8_t scancode);

static const char scancode_to_ascii[0x60] = {
    [0x00] = 0,
    [0x01] = 0, // Esc
    [0x02] = '1',
    [0x03] = '2',
    [0x04] = '3',
    [0x05] = '4',
    [0x06] = '5',
    [0x07] = '6',
    [0x08] = '7',
    [0x09] = '8',
    [0x0A] = '9',
    [0x0B] = '0',
    [0x0C] = '-',
    [0x0D] = '=',
    [0x0E] = '\b', // Backspace
    [0x0F] = '\t', // Tab
    [0x10] = 'q',
    [0x11] = 'w',
    [0x12] = 'e',
    [0x13] = 'r',
    [0x14] = 't',
    [0x15] = 'y',
    [0x16] = 'u',
    [0x17] = 'i',
    [0x18] = 'o',
    [0x19] = 'p',
    [0x1A] = '[',
    [0x1B] = ']',
    [0x1C] = '\n', // Enter
    [0x1D] = 0,    // Left Ctrl
    [0x1E] = 'a',
    [0x1F] = 's',
    [0x20] = 'd',
    [0x21] = 'f',
    [0x22] = 'g',
    [0x23] = 'h',
    [0x24] = 'j',
    [0x25] = 'k',
    [0x26] = 'l',
    [0x27] = ';',
    [0x28] = '\'',
    [0x29] = '`',
    [0x2A] = 0, // Left Shift
    [0x2B] = '\\',
    [0x2C] = 'z',
    [0x2D] = 'x',
    [0x2E] = 'c',
    [0x2F] = 'v',
    [0x30] = 'b',
    [0x31] = 'n',
    [0x32] = 'm',
    [0x33] = ',',
    [0x34] = '.',
    [0x35] = '/',
    [0x36] = 0, // Right Shift
    [0x37] = 0, // Print Screen
    [0x38] = 0, // Alt
    [0x39] = ' ',
    [0x3A] = 0,   // CapsLock
    [0x3B] = 0,   // F1
    [0x3C] = 0,   // F2
    [0x3D] = 0,   // F3
    [0x3E] = 0,   // F4
    [0x3F] = 0,   // F5
    [0x40] = 0,   // F6
    [0x41] = 0,   // F7
    [0x42] = 0,   // F8
    [0x43] = 0,   // F9
    [0x44] = 0,   // F10
    [0x45] = 0,   // NumLock
    [0x46] = 0,   // ScrollLock
    [0x47] = 0,   // Home
    [0x48] = 0,   // Up
    [0x49] = 0,   // PageUp
    [0x4A] = '-', // Keypad -
    [0x4B] = 0,   // Left
    [0x4C] = 0,   // Keypad 5 (Center)
    [0x4D] = 0,   // Right
    [0x4E] = '+', // Keypad +
    [0x4F] = 0,   // End
    [0x50] = 0,   // Down
    [0x51] = 0,   // PageDown
    [0x52] = 0,   // Insert
    [0x53] = 0,   // Delete
    [0x54] = 0,   // SysRq
    [0x55] = 0,
    [0x56] = 0, // (extra)
    [0x57] = 0, // F11
    [0x58] = 0, // F12
    // 0x59-0x5F 默认初始化为 0
};

static const char scancode_to_ascii_shift[0x60] = {
    [0x00] = 0,
    [0x01] = 0,
    [0x02] = '!',
    [0x03] = '@',
    [0x04] = '#',
    [0x05] = '$',
    [0x06] = '%',
    [0x07] = '^',
    [0x08] = '&',
    [0x09] = '*',
    [0x0A] = '(',
    [0x0B] = ')',
    [0x0C] = '_',
    [0x0D] = '+',
    [0x0E] = '\b',
    [0x0F] = '\t',
    [0x10] = 'Q',
    [0x11] = 'W',
    [0x12] = 'E',
    [0x13] = 'R',
    [0x14] = 'T',
    [0x15] = 'Y',
    [0x16] = 'U',
    [0x17] = 'I',
    [0x18] = 'O',
    [0x19] = 'P',
    [0x1A] = '{',
    [0x1B] = '}',
    [0x1C] = '\n',
    [0x1D] = 0,
    [0x1E] = 'A',
    [0x1F] = 'S',
    [0x20] = 'D',
    [0x21] = 'F',
    [0x22] = 'G',
    [0x23] = 'H',
    [0x24] = 'J',
    [0x25] = 'K',
    [0x26] = 'L',
    [0x27] = ':',
    [0x28] = '"',
    [0x29] = '~',
    [0x2A] = 0,
    [0x2B] = '|',
    [0x2C] = 'Z',
    [0x2D] = 'X',
    [0x2E] = 'C',
    [0x2F] = 'V',
    [0x30] = 'B',
    [0x31] = 'N',
    [0x32] = 'M',
    [0x33] = '<',
    [0x34] = '>',
    [0x35] = '?',
    [0x36] = 0,
    [0x37] = 0,
    [0x38] = 0,
    [0x39] = ' ',
    [0x3A] = 0,
    [0x3B] = 0,
    [0x3C] = 0,
    [0x3D] = 0,
    [0x3E] = 0,
    [0x3F] = 0,
    [0x40] = 0,
    [0x41] = 0,
    [0x42] = 0,
    [0x43] = 0,
    [0x44] = 0,
    [0x45] = 0,
    [0x46] = 0,
    [0x47] = 0,
    [0x48] = 0,
    [0x49] = 0,
    [0x4A] = '-', // Keypad -
    [0x4B] = 0,
    [0x4C] = 0,
    [0x4D] = 0,
    [0x4E] = '+', // Keypad +
    [0x4F] = 0,
    [0x50] = 0,
    [0x51] = 0,
    [0x52] = 0,
    [0x53] = 0,
    [0x54] = 0,
    [0x55] = 0,
    [0x56] = 0,
    [0x57] = 0,
    [0x58] = 0,
};

static void keyboard_handler_soft(void)
{
    uint8_t scancode = io_inbyte(0x60);          // 读取键盘数据端口
    handle_scancode(scancode);
    set_EOI();
}

/// @brief 初始化键盘
void init_keyboard(void)
{
    __asm__ __volatile__("movw %0,%%dx;outb %%al,%%dx;" ::"i"(CONTROL_REGISTER_IO), "al"(0b01000101) :);
    set_handler(1, (unsigned long)keyboard_handler_soft);
    kbd_status = kmalloc(sizeof(kbd_status_t));
    memset((void *)kbd_status, 0, sizeof(kbd_status_t));
    enable_irq(1);
}

static void handle_scancode(uint8_t scancode)
{
    uint8_t intr = spin_lock_irq_save(&kbd_status->lock);

    if (scancode == 0xE0)
    {
        kbd_status->e0_prefix = 1;
        spin_unlock(&kbd_status->lock);
        io_set_intr(intr);
        return;
    }

    uint8_t make = !(scancode & 0x80); // 通码为1，断码为0
    uint8_t code = scancode & 0x7F;    // 去掉断码标志

    // 更新修饰键状态（简化版，仅处理常用键）
    if (!kbd_status->e0_prefix)
    {
        switch (code)
        {
        case 0x2A:
        case 0x36: // 左/右 Shift
            kbd_status->shift = make;
            break;
        case 0x1D: // 左 Ctrl
            kbd_status->ctrl = make;
            break;
        case 0x38: // 左 Alt
            kbd_status->alt = make;
            break;
        case 0x3A:    // CapsLock
            if (make) // 仅在按下时切换
                kbd_status->caps_lock = !kbd_status->caps_lock;
            break;
            // 可继续添加 NumLock、ScrollLock 等
        }
    }
    else
    {
        // 处理带 0xE0 前缀的键（如右 Ctrl、右 Alt、方向键）
        switch (code)
        {
        case 0x1D: // 右 Ctrl
            kbd_status->ctrl = make;
            break;
        case 0x38: // 右 Alt
            kbd_status->alt = make;
            break;
            // 方向键等可暂不处理，或映射为转义序列
        }
        kbd_status->e0_prefix = 0; // 清除前缀标志
    }

    // 如果是通码，尝试转换为 ASCII 并放入缓冲区
    if (make)
    {
        char ascii = 0;
        if (code < sizeof(scancode_to_ascii))
        {
            if (kbd_status->shift)
                ascii = scancode_to_ascii_shift[code];
            else
                ascii = scancode_to_ascii[code];

            // CapsLock 处理（仅对字母生效）
            if (kbd_status->caps_lock)
            {
                if (ascii >= 'a' && ascii <= 'z')
                    ascii -= 'a' - 'A';
                else if (ascii >= 'A' && ascii <= 'Z')
                    ascii += 'a' - 'A';
            }

            // Ctrl 组合：将字母转为控制字符（例如 Ctrl+A -> 1）
            if (kbd_status->ctrl)
            {
                if (ascii >= 'a' && ascii <= 'z')
                    ascii = ascii - 'a' + 1;
                else if (ascii >= 'A' && ascii <= 'Z')
                    ascii = ascii - 'A' + 1;
                // 其他键的 Ctrl 组合可后续扩展
            }
        }

        if (ascii)
        {
            uint32_t next_head = (kbd_status->head + 1) % KEYBOARD_BUFFER_SIZE;
            if (next_head != kbd_status->tail)
            { // 缓冲区未满
                kbd_status->buffer[kbd_status->head] = ascii;
                kbd_status->head = next_head;
            }
            // 若满则丢弃字符
        }
    }

    spin_unlock(&kbd_status->lock);
    io_set_intr(intr);
}

size_t keyboard_read(char *buf, size_t length) {
    size_t count = 0;

    uint8_t intr = io_cli();
    spin_lock(&kbd_status->lock);

    while (count < length && kbd_status->head != kbd_status->tail) {
        *buf++ = kbd_status->buffer[kbd_status->tail];
        kbd_status->tail = (kbd_status->tail + 1) % KEYBOARD_BUFFER_SIZE;
        count++;
    }
    spin_unlock(&kbd_status->lock);
    io_set_intr(intr);
    return count;
}

#include "view/view.h"
#include "task.h"

void tty_task(void *arg)
{
    (void)arg;          // 未使用参数
    char *buf = kmalloc(KEYBOARD_BUFFER_SIZE);

    while (1) {
        int length = keyboard_read(buf,KEYBOARD_BUFFER_SIZE - 1);
        if (length) {
            buf[length] = '\0';
            wb_printf(buf);
        } else {
            yield();
        }
    }
}
