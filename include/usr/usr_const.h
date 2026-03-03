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
#define DT_CHR 2 /* Character device */
#define DT_DIR 4 /* Directory */
#define DT_BLK 6 /* Block device */
#define DT_REG 8 /* Regular file */
#define DT_LNK 10 /* Symbolic link */
#define DT_SOCK 12 /* Unix domain socket */
#define DT_WHT 14 /* Whiteout */

#ifdef __GNUC__
#define UNUSED __attribute__((unused))
#else
#define UNUSED
#endif

#endif