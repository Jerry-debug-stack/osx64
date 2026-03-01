#ifndef PTY_H
#define PTY_H

#include <stdint.h>
#include "lib/safelist.h"
#include "lib/wait_queue.h"

#define PTY_COUNT       8
#define PTY_BUFSIZE     4096

#define LINE_MAX        256
#define LINE_QUEUE_SIZE 16

// 本地模式标志 (c_lflag)
#define ICANON  (1 << 0)   // 规范模式
#define ECHO    (1 << 1)   // 回显
#define ISIG    (1 << 2)   // 信号生成（如 Ctrl+C）

// 控制字符索引
#define VINTR   0          // 中断字符 (通常 Ctrl+C)
#define VERASE  1          // 退格字符 (通常 0x7F 或 Ctrl+H)

typedef struct termios {
    uint32_t c_lflag;              // 本地模式标志
    unsigned char c_cc[2];         // 控制字符数组，分别对应 VINTR 和 VERASE
} termios_t;

struct line_discipline {
    char line_buf[LINE_MAX];         // 当前编辑行
    int line_pos;                    // 当前行长度
    char *completed_lines[LINE_QUEUE_SIZE]; // 已完成行队列（指针）
    int completed_head;
    int completed_tail;
    spinlock_t lock;
    termios_t termios;
};

typedef struct pty_pair {
    bool allocated;                 // 是否已分配（主设备至少打开过一次）
    int master_ref;                  // 主设备打开计数
    int slave_ref;                   // 从设备打开计数
    // master -> slave 缓冲区
    char m2s_buf[PTY_BUFSIZE];
    uint32_t m2s_head;
    uint32_t m2s_tail;
    // slave -> master 缓冲区
    char s2m_buf[PTY_BUFSIZE];
    uint32_t s2m_head;
    uint32_t s2m_tail;
    spinlock_t lock;
    struct line_discipline ldisc;
} pty_pair_t;

extern pty_pair_t *pty_pairs;

void pty_init(void);

#endif