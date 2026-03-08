#include <stdint.h>
#include "mem.h"
#include "uprintf.h"
#include "ustring.h"
#include "sysapi.h"
#include "uconst.h"

// 缓冲区大小（窗口大小）
#define BUF_SIZE 256

// 全局状态
static char filename[256];          // 当前文件名
static int fd = -1;                  // 文件描述符
static unsigned char buffer[BUF_SIZE]; // 当前窗口数据
static uint32_t current_offset;       // 当前窗口起始偏移
static int dirty;                     // 是否已修改

// 读取一行用户输入（动态分配，调用者需free）
static char *read_line(void) {
    char *line = (char *)malloc(BUF_SIZE);
    if (!line) return NULL;

    int ret = read(0, line, BUF_SIZE - 1);
    if (ret <= 0) {
        free(line);
        return NULL;
    }
    line[ret] = '\0';
    return line;
}

// 将当前缓冲区内容写回文件
static int save_current(void) {
    if (!dirty) return 0;

    // 定位到窗口起始位置
    if (lseek(fd, current_offset, SEEK_SET) < 0) {
        printf("Seek failed\n");
        return -1;
    }

    ssize_t written = write(fd, (void *)buffer, BUF_SIZE);
    if (written != BUF_SIZE) {
        printf("Write error: only %d bytes written\n", written);
        return -1;
    }

    dirty = 0;
    printf("Saved at offset 0x%x\n", current_offset);
    return 0;
}

// 跳转到指定偏移，重新加载窗口
static void jump_to(uint32_t offset) {
    // 如果当前窗口已修改，先保存
    if (dirty) {
        save_current();
    }

    // 定位并读取新数据
    lseek(fd, offset, SEEK_SET);
    ssize_t n = read(fd, (void *)buffer, BUF_SIZE);
    if (n < 0) {
        n = 0;  // 读错误，当作空
    }

    // 剩余部分补零
    for (int i = n; i < BUF_SIZE; i++) {
        buffer[i] = 0;
    }

    current_offset = offset;
    dirty = 0;
    printf("Jumped to offset 0x%x\n", offset);
}

// 修改窗口内的一个字节
static void change_byte(uint32_t addr, uint8_t val) {
    if (addr < current_offset || addr >= current_offset + BUF_SIZE) {
        printf("Address 0x%x is outside current window [0x%x, 0x%x)\n",
               addr, current_offset, current_offset + BUF_SIZE);
        return;
    }

    int idx = addr - current_offset;
    buffer[idx] = val;
    dirty = 1;
    printf("Byte at 0x%x set to 0x%x\n", addr, val);
}

// 打印当前窗口（十六进制与ASCII）
static void print_window(void) {
    printf("\nOffset: 0x%x  (dirty: %s)\n", current_offset, dirty ? "yes" : "no");
    printf("        ");
    for (int col = 0; col < 16; col++) {
        printf(" %x", col);
    }
    printf("  |0123456789abcdef|\n");
    printf("--------");
    for (int col = 0; col < 16; col++) {
        printf("---");
    }
    printf("+------------------+\n");

    for (int row = 0; row < BUF_SIZE / 16; row++) {
        uint32_t line_addr = current_offset + row * 16;
        printf("%x  ", line_addr);

        // 十六进制部分
        for (int col = 0; col < 16; col++) {
            int idx = row * 16 + col;
            printf("%x ", buffer[idx]);
        }

        // 分隔符
        printf(" |");

        // ASCII部分
        for (int col = 0; col < 16; col++) {
            int idx = row * 16 + col;
            unsigned char c = buffer[idx];
            if (c >= 32 && c <= 126) {
                printf("%c", c);
            } else {
                printf(".");
            }
        }
        printf("|\n");
    }
    printf("\n");
}

// 处理命令
static void handle_command(const char *line) {
    if (!line) return;

    // 跳过前导空格
    while (*line == ' ') line++;
    if (*line == '\0') {
        return;
    }

    char cmd[16];
    unsigned int addr, val;

    // 提取第一个单词
    if (sscanf(line, "%s", cmd) != 1) {
        printf("Invalid command\n");
        return;
    }

    if (strcmp(cmd, "q") == 0) {
        if (dirty) {
            save_current();
        }
        close(fd);
        exit(0);
    }
    else if (strcmp(cmd, "w") == 0) {
        save_current();
    }
    else if (strcmp(cmd, "p") == 0) {
        print_window();
    }
    else if (strcmp(cmd, "j") == 0) {
        if (sscanf(line, "j %i", &addr) == 1) {
            jump_to(addr);
            print_window();
        } else {
            printf("Usage: j <addr>\n");
        }
    }
    else if (strcmp(cmd, "c") == 0) {
        if (sscanf(line, "c %i %i", &addr, &val) == 2) {
            change_byte(addr, (uint8_t)val);
        } else {
            printf("Usage: c <addr> <val>\n");
        }
    }
    else {
        printf("Unknown command: %s\n", cmd);
    }
}

int main(char *argv[]) {
    const char *fname = "test.bin";   // 默认文件名
    if (argv && argv[0]) {
        fname = (const char *)argv[0];
    }
    strcpy(filename, (void *)fname);

    // 以读写方式打开文件，不存在则创建
    fd = open(filename, O_RDWR | O_CREAT, 0644);
    if (fd < 0) {
        printf("Failed to open file %s\n", filename);
        return 1;
    }

    // 初始化：从文件开头读取256字节
    current_offset = 0;
    ssize_t n = read(fd, (void *)buffer, BUF_SIZE);
    if (n < 0) n = 0;
    for (int i = n; i < BUF_SIZE; i++) {
        buffer[i] = 0;
    }
    dirty = 0;

    printf("Hex Editor - File: %s\n", filename);
    printf("Commands:\n");
    printf("  j <addr>   : jump to absolute offset (hex/dec)\n");
    printf("  c <addr> <v>: change byte at <addr> to value <v> (window only)\n");
    printf("  w          : write current window back to file\n");
    printf("  p          : print current window\n");
    printf("  q          : quit (auto-save if dirty)\n");
    printf("  (empty line) : print window\n\n");

    print_window();

    // 主循环
    while (1) {
        printf(": ");
        char *line = read_line();
        if (line) {
            handle_command(line);
            free(line);
        } else {
            // 读输入失败，退出
            break;
        }
    }

    // 清理
    if (dirty) save_current();
    close(fd);
    return 0;
}
