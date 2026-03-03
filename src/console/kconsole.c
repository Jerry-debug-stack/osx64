#include "task.h"
#include "view/view.h"
#include "fs/fs.h"
#include "const.h"
#include "machine/keyboard.h"
#include "mm/mm.h"

extern bool tty_ready;
extern screen_t tty_screens[MAX_TTYS];
extern int current_tty;

static void sh_runner(void *arg);

void display_server(UNUSED void *arg)
{
    int master_fds[MAX_TTYS];
    char name[32];

    // 打开所有 pty master 设备
    for (int i = 0; i < MAX_TTYS; i++) {
        sprintf(name,  "/dev/pty_m%d", sizeof(name), i);
        master_fds[i] = sys_open(name, O_RDWR, 0);
        if (master_fds[i] < 0) {
            wb_printf("display_server: failed to open %s\n", name);
        }
    }

    keycode_t *kbd_buf = kmalloc(KEYBOARD_BUFFER_SIZE * sizeof(keycode_t));
    char *io_buf = kmalloc(KEYBOARD_BUFFER_SIZE);

    tty_ready = true;
    screen_switch(current_tty);

    for (int i = 0; i < MAX_TTYS; i++)
    {
        if (master_fds[i] >= 0) {
            if (i == 0)
            kernel_thread_link_init("sh_runner",sh_runner,(void*)(long)i);
        }
    }

    while (1) {
        int kbd_len = keyboard_read(kbd_buf, KEYBOARD_BUFFER_SIZE - 1);
        if (kbd_len > 0) {
            int j = 0;
            for (int i = 0; i < kbd_len; i++) {
                keycode_t code = kbd_buf[i];
                uint8_t key = code & 0xFF;
                uint16_t mod = code & 0xFF00;

                // 检测 Alt+Fn 切换终端
                if (mod & MOD_ALT) {
                    int new_tty = -1;
                    if (key >= KEY_F1 && key <= KEY_F8) {
                        new_tty = key - KEY_F1;
                    }
                    if (new_tty >= 0 && new_tty < MAX_TTYS && master_fds[new_tty] >= 0) {
                        if (new_tty != current_tty) {
                            current_tty = new_tty;
                            screen_switch(current_tty);  // 切换屏幕显示
                        }
                        break;
                    }
                    continue;  // 切换键不写入 pty
                }

                // 普通 ASCII 字符（无修饰标志）
                if (!(code >> 8)) {
                    io_buf[j++] = (char)key;
                }
                // 其他特殊键暂不处理
            }
            if (j > 0) {
                sys_write(master_fds[current_tty], io_buf, j);
            }
        }

        // 轮询所有终端，读取输出并写入屏幕缓冲区
        for (int i = 0; i < MAX_TTYS; i++) {
            if (master_fds[i] < 0) continue;

            int out_len = sys_read(master_fds[i], io_buf, KEYBOARD_BUFFER_SIZE - 1);

            if (out_len > 0){
                screen_t *scr = &tty_screens[i];
                io_buf[out_len] = '\0';
                char *last = io_buf;
                for (int j = 0; j < out_len + 1; j++)
                {
                    if (io_buf[j] == '\0'){
                        screen_print(scr,last,VIEW_COLOR_BLACK,VIEW_COLOR_WHITE);
                        if (i == current_tty){
                            color_print(last,VIEW_COLOR_BLACK,VIEW_COLOR_WHITE);
                        }
                        last = io_buf + j + 1;
                    }
                }
            }
        }
        sys_yield();
    }
}

static void sh_start(char *std_name);

static void sh_runner(void *arg)
{
    int idx = (int)(uintptr_t)arg;
    char slave_name[32];
    sprintf(slave_name, "/dev/pty_s%d", sizeof(slave_name), idx);

    while (1) {
        int child_pid = kernel_thread_default("sh",sh_start,slave_name);
        sys_waitpid(child_pid,NULL);
    }
}

static void sh_start(char *std_name){
    int fd = sys_open(std_name, O_RDWR, 0);
    if (fd < 0) {
        // 打开失败，退出子进程
        sys_exit(1);
    }
    sys_dup2(fd, 0);
    sys_dup2(fd, 1);
    sys_dup2(fd, 2);
    if (fd > 2){
        sys_close(fd);
    }
    char *argv[] = {NULL,NULL};
    sys_execv("/root/bash.elf",argv);
    wb_printf("open bash.elf failed!!\n");
    while (1){}
    sys_exit(-1);
}
