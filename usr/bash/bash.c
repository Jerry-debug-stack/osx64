#include "usr/sysapi.h"
#include "usr/usr_const.h"
#include "usr/usr_mem.h"
#include "usr/usr_printf.h"
#include "usr/usr_string.h"

#define MAX_ARGS 32
#define MAX_INPUT_LENGTH 1024
// 命令行解析结果结构体
typedef struct command_line {
    char* command; // 命令名
    char* args[MAX_ARGS]; // 参数数组
    int argc; // 参数个数
} command_line_t;

typedef struct shell_function {
    char* name;
    int (*func)(char* argv[], int length);
} shell_function_t;

static int bash_cd(char* argv[], int length);
static int bash_mount(char* argv[], int length);
static int bash_umount(char* argv[], int length);
static int bash_ls(UNUSED char* argv[], int length);
static int bash_setname(char* argv[], int length);
static int bash_clear(UNUSED char* argv[], int length);
static int bash_mkdir(char* argv[], int length);
static int bash_rmdir(char* argv[], int length);
static int bash_touch(char* argv[], int length);
static int bash_cat(char* argv[], int length);
static int bash_rm(char* argv[], int length);
static int bash_mv(char* argv[], int length);
static int bash_reboot(UNUSED char* argv[], int length);
static int bash_poweroff(UNUSED char* argv[], int length);
static int bash_whoami(UNUSED char* argv[], int length);
static int bash_exit(UNUSED char* argv[], int length);
static int bash_sync(UNUSED char* argv[], int length);

static shell_function_t functions[] = {
    { "cd", bash_cd },
    { "mount", bash_mount },
    { "umount", bash_umount },
    { "ls", bash_ls },
    { "setname", bash_setname },
    { "clear", bash_clear },
    { "mkdir", bash_mkdir },
    { "rmdir", bash_rmdir },
    { "touch", bash_touch },
    { "cat", bash_cat },
    { "rm", bash_rm },
    { "mv", bash_mv },
    { "reboot", bash_reboot },
    { "poweroff", bash_poweroff },
    { "whoami", bash_whoami },
    { "exit", bash_exit },
    { "sync", bash_sync },
    { (void*)0, (void*)0 }
};

static int do_bash(struct command_line* cmd);
static int parse_command(const char* input, struct command_line* cmd);
static void free_command(struct command_line* cmd);

char* non_argv[] = { 0 };
char name[64];

int bash_main(UNUSED char* argv[])
{
    name[0] = 0;
    char* buffer = malloc(1024);
    char* path = malloc(256);
    buffer[1023] = 0;
    struct command_line cmd;
    int ret;
    while (1) {
        getcwd(path, 256);
        printf("[root %s %s]$", &name[0], path);
        ret = read(0,buffer,1023);
        while (ret == 0)
        {
            yield();
            ret = read(0,buffer,1023);
        }
        ret = parse_command(buffer, &cmd);
        if (ret == -1)
            printf("syntax error!\n");
        else {
            ret = do_bash(&cmd);
            if (ret != 0)
                __asm__ __volatile__("nop" :::);
        }
    }
    exit(0);
}

static int do_bash(struct command_line* cmd)
{
    if (cmd->command == NULL)
        return -1;
    else {
        int i = 0;
        int builtin = 0;
        int ret = -1;
        while (functions[i].name) {
            if (strcmp(cmd->command, functions[i].name) == 0) {
                ret = functions[i].func(cmd->args, cmd->argc - 1);
                builtin = 1;
                goto end;
            }
            i++;
        }
        //int id = fork();
        //if (id == 0) {
        //    printf("forked\n");
        //    exit(execv(cmd->command, cmd->args));
        //} else {
        //    waitpid(id, &ret, 0);
        //}
    end:
        if (!builtin) {
            if (ret != 0) {
                printf("err happened\n");
            }
        }
        return ret;
    }
}


static int bash_cd(char* argv[], int length)
{
    if (length == 1) {
        int ret = chdir(argv[0]);
        if (ret)
            printf("target failed!\n");
        return ret;
    } else {
        printf("cd [dest]\nto go to where you want!\n");
        return -1;
    }
}

static int bash_mount(char* argv[], int length)
{
    if (length == 2) {
        int ret = mount(argv[0], argv[1]);
        if (ret)
            printf("target failed!\n");
        return ret;
    } else {
        printf("mount [target] [mount_point]\nto mount the want things\n");
        return -1;
    }
}

static int bash_umount(char* argv[], int length)
{
    if (length == 1) {
        int ret = umount(argv[0]);
        if (ret)
            printf("target failed!\n");
        return ret;
    } else {
        printf("umount [target]\nto umount the things\n");
        return -1;
    }
}

static int bash_ls(UNUSED char* argv[], int length)
{
    if (length == 0) {
        char path[256];
        getcwd(&path[0], 256);
        int fd = open(&path[0], O_RDONLY | O_DIRECTORY,0);
        if (fd < 0) {
            printf("not able to open work folder\n");
            return -1;
        }
        dirent_t *dir = malloc(4096); 

        while (1) {
            int ret = getdent(fd,dir,4096);
            if (ret == 0){
                if (dir->d_type == DT_DIR)
                    printf("%s/ ", dir->d_name);
                else
                    printf("%s ", dir->d_name);
            }else{
                break;
            }
        }
        printf("\n");
        close(fd);
        return 0;
    } else {
        printf("ls\nto show what's in the dir\n");
        return -1;
    }
}

static int bash_setname(char* argv[], int length)
{
    if (length == 0) {
        name[0] = '\0';
        return 0;
    } else if (length == 1) {
        int len = strlen(argv[0]);
        if (len > 63) {
            printf("name too long\n");
            return -1;
        } else {
            strcpy(&name[0], argv[0]);
            return 0;
        }
    } else {
        printf("setname [name]\njust enter a name for this tty\n");
        return -1;
    }
}

static int bash_clear(UNUSED char* argv[], int length)
{
    if (length == 0) {
        printf("\f");
        return 0;
    } else {
        printf("clear\nclear the screen\n");
        return -1;
    }
}

static int bash_mkdir(char* argv[], int length)
{
    if (length == 1) {
        int ret = mkdir(argv[0],0755);
        if (ret)
            printf("mkdir failed\n");
        return ret;
    } else {
        printf("mkdir [target]\nmake a dir\n");
        return -1;
    }
}

static int bash_rmdir(char* argv[], int length)
{
    if (length == 1) {
        int ret = rmdir(argv[0]);
        if (ret)
            printf("rmdir failed\n");
        return ret;
    } else {
        printf("rmdir [target]\nremove a dir\n");
        return -1;
    }
}

static int bash_touch(char* argv[], int length)
{
    if (length == 1) {
        int fd = open(argv[0], O_CREAT,0755);
        if (fd < 0) {
            printf("Create failed and doesn't exist\n");
            return -1;
        } else {
            close(fd);
            return 0;
        }
    } else {
        printf("touch [name]\ncreate a file\n");
        return -1;
    }
}

static int bash_cat(char* argv[], int length)
{
    if (length == 1) {
        int fd = open(argv[0], O_RDONLY, 0);
        if (fd < 0) {
            printf("Open failed\n");
            return -1;
        } else {
            char buffer[128];
            int size = read(fd, &buffer[0], 128);
            if (size < 0) {
                printf("read failed\n");
            } else if (size > 0)
                printf("content:\n%s\n", &buffer[0]);
            else
                printf("\n");
            close(fd);
            return 0;
        }
    } else {
        printf("cat [name]\nshow file info\n");
        return -1;
    }
}

static int bash_rm(char* argv[], int length)
{
    if (length == 1) {
        int ret = unlink(argv[0]);
        if (ret)
            printf("Delete failed!\n");
        return ret;
    } else {
        printf("rm [name]\nremove file only!!\n");
        return -1;
    }
}

static int bash_mv(char* argv[], int length)
{
    if (length == 2) {
        int ret = rename(argv[0], argv[1]);
        if (ret)
            printf("mv failed!\n");
        return ret;
    } else {
        printf("mv [target] [destination]\nmove file\n");
        return -1;
    }
}

static int bash_reboot(UNUSED char* argv[], int length)
{
    if (length == 0){
    }
        //reboot(LINUX_REBOOT_CMD_RESTART);
    else
        printf("reboot\nto restart your computer\n");
    return -1;
}

static int bash_poweroff(UNUSED char* argv[], int length)
{
    if (length == 0){
    }
        //reboot(LINUX_REBOOT_CMD_POWER_OFF);
    else
        printf("poweroff\nto power off your computer\n");
    return -1;
}

static int bash_whoami(UNUSED char* argv[], int length)
{
    if (length == 0) {
        printf("root\n");
        return 0;
    } else {
        printf("whoami\nreturn your username\n");
        return -1;
    }
}

static int bash_sync(UNUSED char* argv[], int length){
    if (length == 0){
        sync();
        return 0;
    }else{
        printf("sync\nwrite back all superblocks and group descriptors\n");
        return -1;
    }
}

static int bash_exit(UNUSED char* argv[], int length)
{
    if (length == 0) {
        exit(0);
    } else {
        printf("exit\nexit this bash\n");
    }
    return -1;
}

// 判断字符是否为空白字符
static int is_whitespace(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

// 跳过空白字符
static const char* skip_whitespace(const char* str)
{
    while (*str && is_whitespace(*str)) {
        str++;
    }
    return str;
}

// 解析引号内的字符串
static const char* parse_quoted_string(const char* str, char** result)
{
    char quote = *str;
    const char* start = str + 1;
    const char* end = start;

    // 找到匹配的引号
    while (*end && *end != quote) {
        if (*end == '\\' && *(end + 1) == quote) {
            // 处理转义字符
            end += 2;
        } else {
            end++;
        }
    }

    if (*end != quote) {
        // 引号不匹配
        return NULL;
    }

    // 分配内存并复制字符串
    size_t len = end - start;
    *result = malloc(len + 1);
    if (!*result) {
        return NULL;
    }

    // 复制并处理转义字符
    char* dest = *result;
    const char* src = start;
    while (src < end) {
        if (*src == '\\' && *(src + 1) == quote) {
            *dest++ = quote;
            src += 2;
        } else {
            *dest++ = *src++;
        }
    }
    *dest = '\0';

    return end + 1; // 返回引号后的位置
}

static int parse_command(const char* input, struct command_line* cmd)
{
    if (!input || !cmd) {
        return -1;
    }

    // 初始化结构体
    memset(cmd, 0, sizeof(struct command_line));
    cmd->argc = 0;

    const char* p = skip_whitespace(input);
    int arg_index = 0;

    while (*p && arg_index < MAX_ARGS - 1) {
        if (*p == '"' || *p == '\'') {
            // 处理引号字符串
            char* arg = NULL;
            p = parse_quoted_string(p, &arg);
            if (!p) {
                free_command(cmd);
                return -1;
            }

            if (arg_index == 0) {
                cmd->command = arg;
            } else {
                cmd->args[arg_index - 1] = arg;
            }
            cmd->argc++;
            arg_index++;

        } else {
            // 处理普通参数
            const char* start = p;
            while (*p && !is_whitespace(*p) && *p != '>' && *p != '<' && *p != '&') {
                p++;
            }

            size_t len = p - start;
            if (len > 0) {
                char* arg = malloc(len + 1);
                if (!arg) {
                    free_command(cmd);
                    return -1;
                }

                memcpy(arg, (void *)start, len);
                arg[len] = '\0';

                if (arg_index == 0) {
                    cmd->command = arg;
                } else {
                    cmd->args[arg_index - 1] = arg;
                }
                cmd->argc++;
                arg_index++;
            }
        }

        p = skip_whitespace(p);
    }

    // 设置参数列表的结束标记
    if (arg_index < MAX_ARGS) {
        cmd->args[arg_index - 1] = NULL;
    }

    return 0;
}

static void free_command(struct command_line* cmd)
{
    if (!cmd)
        return;

    if (cmd->command) {
        free(cmd->command);
        cmd->command = NULL;
    }

    for (int i = 0; i < cmd->argc - 1 && i < MAX_ARGS - 1; i++) {
        if (cmd->args[i]) {
            free(cmd->args[i]);
            cmd->args[i] = NULL;
        }
    }

    cmd->argc = 0;
}
