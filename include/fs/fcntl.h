#ifndef OS_FCNTL_H
#define OS_FCNTL_H

/* 访问模式掩码 */
#define O_ACCMODE       00000003
#define O_RDONLY        00000000
#define O_WRONLY        00000001
#define O_RDWR          00000002

/* 创建标志 */
#define O_CREAT         00000100   /* 创建文件（如果不存在） */
#define O_EXCL          00000200   /* 与 O_CREAT 一起使用，若文件存在则失败 */
#define O_NOCTTY        00000400   /* 不分配控制终端 */
#define O_TRUNC         00001000   /* 截断文件（如果存在且可写） */
#define O_APPEND        00002000   /* 追加模式 */
#define O_NONBLOCK      00004000   /* 非阻塞模式 */
#define O_SYNC          00010000   /* 同步写 */
#define O_DIRECTORY     00200000   /* 必须为目录 */
#define O_NOFOLLOW      00400000   /* 不跟随符号链接 */
#define O_CLOEXEC       02000000   /* 执行时关闭 */

#define DEFAULT_FILE_MODE   (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)  /* 0644 */
#define DEFAULT_DIR_MODE    (S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) /* 0755 */

#define SEEK_SET    0   /* 从文件开头 */
#define SEEK_CUR    1   /* 从当前位置 */
#define SEEK_END    2   /* 从文件末尾 */

// 文件类型常量（用于 d_type）
#define DT_UNKNOWN  0
#define DT_REG      1   // 普通文件
#define DT_DIR      2   // 目录
#define DT_CHR      3   // 字符设备
#define DT_BLK      4   // 块设备
#define DT_FIFO     5   // FIFO
#define DT_SOCK     6   // 套接字
#define DT_LNK      7   // 符号链接

typedef struct dirent {
    uint64_t d_ino;      // inode 号
    uint16_t d_reclen;   // 记录长度
    uint8_t  d_type;     // 文件类型
    char     d_name[];   // 文件名
} dirent_t;

#endif