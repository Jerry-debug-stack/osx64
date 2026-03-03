#include "const.h"
#include <stdint.h>
#include "fs/fs.h"

extern uint64_t ticks;
uint64_t sys_get_ticks(void){
    return ticks;
}
int sys_open(const char *path, int flags, int mode);
ssize_t sys_read(int fd, char *buf, size_t count);
ssize_t sys_write(int fd, const char *buf, size_t count);
int sys_close(int fd);
off_t sys_lseek(int fd, off_t offset, int whence);
int sys_mkdir(const char *path, int mode);
int sys_rmdir(const char *path);
int sys_unlink(const char *path);
int sys_chdir(const char *path);
int sys_ftruncate(int fd, off_t length);
int sys_truncate(const char *path, off_t length);
int sys_rename(const char *oldpath, const char *newpath);
int sys_dup(int oldfd);
int sys_dup2(int oldfd, int newfd);
int sys_getcwd(char *buf, size_t size);
int sys_mount(const char *dev_path,const char *to_path);
int sys_umount(const char *target_path);
int sys_reload_partition(char *target);
int sys_getdent(int fd, struct dirent __user *dirp, unsigned int count);
_Noreturn void sys_exit(int exit_status);
void sys_yield(void);
int sys_waitpid(int pid, int *status);
int sys_sync(void);
int sys_execv(const char* path, char* const argv[]);
int sys_reboot(int cmd);

void *syscall_table[MAX_SYSCALL_NUM] = {
    sys_get_ticks,
    sys_open,
    sys_read,
    sys_write,
    sys_close,
    sys_lseek,
    sys_mkdir,
    sys_rmdir,
    sys_unlink,
    sys_chdir,
    sys_ftruncate,
    sys_truncate,
    sys_rename,
    sys_dup,
    sys_dup2,
    sys_getcwd,
    sys_mount,
    sys_umount,
    sys_reload_partition,
    sys_getdent,
    sys_exit,
    sys_yield,
    sys_waitpid,
    sys_sync,
    NULL, /* fork */
    sys_execv,
    sys_reboot
};
