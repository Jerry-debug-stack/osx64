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

/// @brief 处理扫描码,只在键盘中断上下文中启用
/// @param scancode 
static void handle_scancode(uint8_t scancode)
{
    spin_lock(&kbd_status->lock);

    // 处理 0xE0 前缀
    if (scancode == 0xE0) {
        kbd_status->e0_prefix = 1;
        spin_unlock(&kbd_status->lock);
        return;
    }

    uint8_t make = !(scancode & 0x80);
    uint8_t code = scancode & 0x7F;

    // 更新修饰键状态（不分左右）
    if (!kbd_status->e0_prefix) {
        switch (code) {
        case 0x2A: case 0x36: // 左/右 Shift
            kbd_status->shift = make;
            break;
        case 0x1D: // 左 Ctrl
            kbd_status->ctrl = make;
            break;
        case 0x38: // 左 Alt
            kbd_status->alt = make;
            break;
        case 0x3A: // CapsLock
            if (make) kbd_status->caps_lock = !kbd_status->caps_lock;
            break;
        }
    } else {
        switch (code) {
        case 0x1D: // 右 Ctrl
            kbd_status->ctrl = make;
            break;
        case 0x38: // 右 Alt
            kbd_status->alt = make;
            break;
        }
    }

    // 如果是通码，生成键码
    if (make) {
        keycode_t key = 0;

        if (!kbd_status->e0_prefix) {
            // 无前缀的普通键
            if (code < sizeof(scancode_to_ascii)) {
                char base_no_shift = scancode_to_ascii[code];
                char base_shift = scancode_to_ascii_shift[code];
                char result_char = 0;

                // 检查是否有 Ctrl 或 Alt 按下
                int meta_pressed = (kbd_status->ctrl || kbd_status->alt);

                if (meta_pressed) {
                    // 当 Ctrl 或 Alt 按下时
                    if (base_no_shift >= 'a' && base_no_shift <= 'z') {
                        // 字母：强制大写
                        result_char = base_no_shift - 'a' + 'A';
                    } else if (base_no_shift >= 'A' && base_no_shift <= 'Z') {
                        result_char = base_no_shift; // 已经是大写
                    } else {
                        // 非字母：使用无 Shift 的基础字符
                        result_char = base_no_shift;
                    }
                } else {
                    // 无 Ctrl/Alt 时，原有逻辑
                    result_char = kbd_status->shift ? base_shift : base_no_shift;

                    // CapsLock 处理（仅对字母）
                    if (kbd_status->caps_lock) {
                        if (result_char >= 'a' && result_char <= 'z')
                            result_char -= 'a' - 'A';
                        else if (result_char >= 'A' && result_char <= 'Z')
                            result_char += 'a' - 'A';
                    }
                }

                
                if (result_char) {
                    key = (keycode_t)result_char;
                    // 修饰标志：Ctrl 和 Alt 总是添加（如果按下）
                    if (kbd_status->ctrl)  key |= MOD_CTRL;
                    if (kbd_status->alt)   key |= MOD_ALT;
                    // Shift 仅在 Ctrl 或 Alt 按下时才添加，否则不添加（因为字符已体现）
                    if ((kbd_status->ctrl || kbd_status->alt) && kbd_status->shift) {
                        key |= MOD_SHIFT;
                    }
                } else {
                    // 功能键（映射表中为0的键）
                    switch (code) {
                    case 0x3B: key = KEY_F1; break;
                    case 0x3C: key = KEY_F2; break;
                    case 0x3D: key = KEY_F3; break;
                    case 0x3E: key = KEY_F4; break;
                    case 0x3F: key = KEY_F5; break;
                    case 0x40: key = KEY_F6; break;
                    case 0x41: key = KEY_F7; break;
                    case 0x42: key = KEY_F8; break;
                    case 0x43: key = KEY_F9; break;
                    case 0x44: key = KEY_F10; break;
                    case 0x57: key = KEY_F11; break;
                    case 0x58: key = KEY_F12; break;
                    default: break;
                    }
                }
            }
        } else {
            // 带 0xE0 前缀的键（方向键、编辑键）
            switch (code) {
            case 0x48: key = KEY_UP; break;
            case 0x50: key = KEY_DOWN; break;
            case 0x4B: key = KEY_LEFT; break;
            case 0x4D: key = KEY_RIGHT; break;
            case 0x47: key = KEY_HOME; break;
            case 0x4F: key = KEY_END; break;
            case 0x49: key = KEY_PGUP; break;
            case 0x51: key = KEY_PGDN; break;
            case 0x52: key = KEY_INSERT; break;
            case 0x53: key = KEY_DELETE; break;
            default: break;
            }
        }

        // 如果生成了键码，则放入缓冲区
        if (key) {
            // 对于特殊键，我们需要添加修饰标志
            if ((key & 0xFF) >= 0x80) {
                if (kbd_status->shift) key |= MOD_SHIFT;
                if (kbd_status->ctrl)  key |= MOD_CTRL;
                if (kbd_status->alt)   key |= MOD_ALT;
            }

            uint32_t next = (kbd_status->head + 1) % KEYBOARD_BUFFER_SIZE;
            if (next != kbd_status->tail) {
                kbd_status->buffer[kbd_status->head] = key;
                kbd_status->head = next;
            }
        }
    }

    // 清除 0xE0 前缀标志
    if (scancode != 0xE0) {
        kbd_status->e0_prefix = 0;
    }

    spin_unlock(&kbd_status->lock);
}

size_t keyboard_read(keycode_t *buf, size_t length) {
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
