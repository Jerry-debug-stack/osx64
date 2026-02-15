#ifndef OS_FSMOD_H
#define OS_FSMOD_H

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

#endif