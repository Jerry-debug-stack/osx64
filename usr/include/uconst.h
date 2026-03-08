#ifndef OS_USER_CONST_H
#define OS_USER_CONST_H

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2

#define O_CREAT 00000100 /* 创建文件 */
#define O_EXCL 00000200 /* 独占创建 */
#define O_NOCTTY 00000400 /* 不分配控制终端 */
#define O_TRUNC 00001000 /* 截断文件 */
#define O_APPEND 00002000 /* 追加模式 */
#define O_NONBLOCK 00004000 /* 非阻塞模式 */
#define O_SYNC 00010000 /* 同步写入 */
#define O_ASYNC 00020000 /* 异步I/O */
#define O_LARGEFILE 00100000 /* 大文件支持 */
#define O_DIRECTORY 00200000 /* 必须是目录 */
#define O_NOFOLLOW 00400000 /* 不跟随符号链接 */
#define O_CLOEXEC 02000000 /* 执行时关闭 */

#define DT_UNKNOWN 0 /* Unknown file type */
#define DT_FIFO 1 /* Named pipe */
#define DT_CHR 3 /* Character device */
#define DT_DIR 2 /* Directory */
#define DT_BLK 4 /* Block device */
#define DT_REG 8 /* Regular file */
#define DT_LNK 10 /* Symbolic link */
#define DT_SOCK 12 /* Unix domain socket */
#define DT_WHT 14 /* Whiteout */

/* 文件类型（已在之前定义） */
#define S_IFMT   0170000
#define S_IFDIR  0040000
#define S_IFREG  0100000
#define S_IFLNK  0120000

/* 权限位 */
#define S_IRWXU  00700   /* 所有者读、写、执行 */
#define S_IRUSR  00400   /* 所有者读 */
#define S_IWUSR  00200   /* 所有者写 */
#define S_IXUSR  00100   /* 所有者执行 */

#define S_IRWXG  00070   /* 组读、写、执行 */
#define S_IRGRP  00040   /* 组读 */
#define S_IWGRP  00020   /* 组写 */
#define S_IXGRP  00010   /* 组执行 */

#define S_IRWXO  00007   /* 其他人读、写、执行 */
#define S_IROTH  00004   /* 其他人读 */
#define S_IWOTH  00002   /* 其他人写 */
#define S_IXOTH  00001   /* 其他人执行 */

#define S_ISDIR(mode)  (((mode) & S_IFMT) == S_IFDIR)
#define S_ISREG(mode)  (((mode) & S_IFMT) == S_IFREG)

#define REBOOT_CMD_POWER_OFF 0
#define REBOOT_CMD_RESTART 1

#ifdef __GNUC__
#define UNUSED __attribute__((unused))
#else
#define UNUSED
#endif

#endif