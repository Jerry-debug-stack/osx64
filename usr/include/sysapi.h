#ifndef OS_SYSAPI_H
#define OS_SYSAPI_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef int64_t ssize_t;
typedef int64_t loff_t; 
typedef loff_t off_t;

typedef struct dirent {
    uint64_t d_ino;      // inode 号
    uint16_t d_reclen;   // 记录长度
    uint8_t  d_type;     // 文件类型
    char     d_name[];   // 文件名
} dirent_t;

typedef struct utimespec {
    uint64_t tv_sec;  // 秒
    uint64_t   tv_nsec; // 纳秒
} utimespec_t;

uint32_t time(void);
int open(const char *path, int flags, int mode);
ssize_t read(int fd, char *buf, size_t count);
ssize_t write(int fd, const char *buf, size_t count);
int close(int fd);
off_t lseek(int fd, off_t offset, int whence);
int mkdir(const char *path, int mode);
int rmdir(const char *path);
int unlink(const char *path);
int chdir(const char *path);
int ftruncate(int fd, off_t length);
int truncate(const char *path, off_t length);
int rename(const char *oldpath, const char *newpath);
int dup(int oldfd);
int dup2(int oldfd, int newfd);
int getcwd(char *buf, size_t size);
int mount(const char *dev_path,const char *to_path);
int umount(const char *target_path);
int reload_partition(char *target);
int getdent(int fd, struct dirent *dirp, unsigned int count);
_Noreturn void exit(int exit_status);
void yield(void);
int waitpid(int pid, int *status);
int sync(void);
int fork(void);
int execv(const char* path, char* const argv[]);
int reboot(int cmd);
void clock_gettime(utimespec_t *u);

#endif
