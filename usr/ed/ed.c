#include <stdint.h>
#include <stddef.h>

#include "usr/usr_const.h"
#include "usr/usr_mem.h"
#include "usr/usr_printf.h"
#include "usr/usr_string.h"
#include "usr/sysapi.h"

// 行结构
typedef struct line {
    uint32_t num;
    char *text;
    struct line *next;
} line_t;

line_t *first_line = NULL;
line_t *current_line = NULL;
uint32_t total_lines = 0;
char current_filename[256] = ""; // 当前关联的文件名

void update_line_numbers(void) {
    line_t *p = first_line;
    uint32_t num = 1;
    while (p) {
        p->num = num++;
        p = p->next;
    }
    total_lines = num - 1;
}

line_t *append_line(const char *text) {
    line_t *new_line = (line_t *)malloc(sizeof(line_t));
    if (!new_line) return NULL;

    new_line->text = (char *)malloc(strlen(text) + 1);
    if (!new_line->text) {
        free(new_line);
        return NULL;
    }
    strcpy(new_line->text, (void *)text);
    new_line->next = NULL;

    if (first_line == NULL) {
        first_line = new_line;
    } else {
        line_t *p = first_line;
        while (p->next) p = p->next;
        p->next = new_line;
    }

    update_line_numbers();
    return new_line;
}

line_t *insert_line_after(line_t *pos_line, const char *text) {
    line_t *new_line = (line_t *)malloc(sizeof(line_t));
    if (!new_line) return NULL;

    new_line->text = (char *)malloc(strlen(text) + 1);
    if (!new_line->text) {
        free(new_line);
        return NULL;
    }
    strcpy(new_line->text, (void *)text);

    if (pos_line == NULL) {
        new_line->next = first_line;
        first_line = new_line;
    } else {
        new_line->next = pos_line->next;
        pos_line->next = new_line;
    }

    update_line_numbers();
    return new_line;
}

line_t *delete_line(line_t *target) {
    if (!target) return NULL;

    line_t *prev = NULL;
    line_t *p = first_line;
    while (p && p != target) {
        prev = p;
        p = p->next;
    }
    if (!p) return NULL;

    if (prev == NULL) {
        first_line = target->next;
    } else {
        prev->next = target->next;
    }

    line_t *next = target->next;
    free(target->text);
    free(target);

    update_line_numbers();

    if (next) return next;
    if (prev) return prev;
    return NULL;
}

line_t *find_line_by_num(uint32_t num) {
    line_t *p = first_line;
    while (p) {
        if (p->num == num) return p;
        p = p->next;
    }
    return NULL;
}

char *read_line(void) {
    char buf[256];
    while (1) {
        int ret = read(0, buf, sizeof(buf) - 1);
        if (ret > 0) {
            buf[ret] = '\0';
            char *line = malloc(ret + 1);
            if (line) strcpy(line, buf);
            return line;
        } else if (ret == 0) {
            yield();
        } else {
            return NULL;
        }
    }
}

int load_file(const char *path) {
    int fd = open(path, O_RDONLY, 0);
    if (fd < 0) return -1;

    char buf[512];
    char line_buf[256];
    uint32_t line_pos = 0;
    ssize_t bytes_read;

    while (1) {
        bytes_read = read(fd, buf, sizeof(buf));
        if (bytes_read <= 0) break;

        for (ssize_t i = 0; i < bytes_read; i++) {
            char c = buf[i];
            if (c == '\n') {
                line_buf[line_pos] = '\0';
                append_line(line_buf);
                line_pos = 0;
            } else {
                if (line_pos < sizeof(line_buf) - 1) {
                    line_buf[line_pos++] = c;
                }
            }
        }
    }

    if (line_pos > 0) {
        line_buf[line_pos] = '\0';
        append_line(line_buf);
    }

    close(fd);
    return 0;
}

int save_file(const char *path) {
    // 以只写方式打开，若文件不存在则创建（不再使用 O_TRUNC）
    int fd = open(path, O_WRONLY | O_CREAT, 0644);
    if (fd < 0) return -1;

    // 使用 ftruncate 清空文件到长度 0
    if (ftruncate(fd, 0) < 0) {
        close(fd);
        return -1;
    }

    // 将文件偏移定位到开头（ftruncate 后偏移量未定义，保险起见 seek）
    lseek(fd, 0, SEEK_SET);

    // 逐行写入文件
    line_t *p = first_line;
    while (p) {
        write(fd, p->text, strlen(p->text));
        write(fd, "\n", 1);
        p = p->next;
    }

    close(fd);
    return 0;
}

uint32_t parse_line_number(const char *cmd) {
    uint32_t num = 0;
    while (*cmd >= '0' && *cmd <= '9') {
        num = num * 10 + (*cmd - '0');
        cmd++;
    }
    return num;
}

void print_current(void) {
    if (current_line) {
        printf("%d\t%s\n", current_line->num, current_line->text);
    } else {
        printf("No current line\n");
    }
}

void handle_command(const char *cmd) {
    if (!cmd || *cmd == '\0') return;

    switch (cmd[0]) {
        case 'p':
            print_current();
            break;

        case 'n':
            printf("%d\n", current_line ? current_line->num : 0);
            break;

        case 'd':
            if (current_line) {
                current_line = delete_line(current_line);
                printf("Line deleted.\n");
                if (current_line) print_current();
            } else {
                printf("No current line\n");
            }
            break;

        case 'a':
        case 'i': {
            int after = (cmd[0] == 'a');
            printf("Enter text ('.' alone to end):\n");
            while (1) {
                char *line = read_line();
                if (!line) break;
                if (strcmp(line, ".") == 0) {
                    free(line);
                    break;
                }
                if (after) {
                    current_line = insert_line_after(current_line, line);
                } else {
                    line_t *prev = NULL;
                    if (current_line) {
                        line_t *p = first_line;
                        while (p && p != current_line) {
                            prev = p;
                            p = p->next;
                        }
                    }
                    current_line = insert_line_after(prev, line);
                }
                free(line);
            }
            if (current_line) print_current();
            break;
        }

        case 'c': {
            if (!current_line) {
                printf("No current line\n");
                break;
            }
            printf("Current: %s\nNew: ", current_line->text);
            char *new_text = read_line();
            if (new_text) {
                free(current_line->text);
                current_line->text = new_text;
                printf("Line updated.\n");
                print_current();
            }
            break;
        }

        case 'w': {
            // 解析命令中的文件名部分
            char *space = strchr(cmd, ' ');
            char filename_buf[256];
            char *target_file = NULL;

            if (space) {
                // 跳过空格
                while (*space == ' ') space++;
                if (*space != '\0') {
                    strcpy(filename_buf, space);
                    target_file = filename_buf;
                }
            }

            if (target_file == NULL) {
                // 使用当前文件名
                if (current_filename[0] == '\0') {
                    printf("No filename specified and no current filename.\n");
                    break;
                }
                target_file = current_filename;
            }

            if (save_file(target_file) == 0) {
                printf("Saved as %s\n", target_file);
                // 如果使用了新文件名，更新当前文件名
                if (target_file == filename_buf) {
                    strcpy(current_filename, filename_buf);
                }
            } else {
                printf("Save failed.\n");
            }
            break;
        }

        case 'q':
            exit(0);
            break;

        default: {
            uint32_t num = parse_line_number(cmd);
            if (num >= 1 && num <= total_lines) {
                current_line = find_line_by_num(num);
                if (current_line) print_current();
            } else {
                printf("Unknown command or line number\n");
            }
            break;
        }
    }
}

int main(char *argv[]) {
    const char *filename;
    if (argv != NULL && argv[0] != NULL){
        filename = (void *)argv[0];
    }else{
        filename = "test.txt";
    }
    strcpy(current_filename, (void *)filename);  // 记录当前文件名

    if (load_file(filename) == 0) {
        printf("Loaded %s, %d lines\n", filename, total_lines);
        current_line = first_line;
        if (current_line) print_current();
    } else {
        printf("New file: %s\n", filename);
        first_line = NULL;
        current_line = NULL;
        total_lines = 0;
    }

    printf("Commands: p(print), n(line number), d(delete), a(append after), i(insert before), c(change), w [file] (save), q(quit), [line number]\n");
    while (1) {
        printf(": ");
        char *cmd = read_line();
        if (cmd) {
            handle_command(cmd);
            free(cmd);
        }
    }
    return 0;
}
