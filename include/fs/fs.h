#ifndef OS_FS_H
#define OS_FS_H

#include <stdbool.h>
#include "lib/safelist.h"
#include "lib/atomic.h"
#include "fs/fcntl.h"

#define DENTRY_CACHE_SIZE   1024
#define MAX_NAME 128
#define __user
typedef int64_t ssize_t;
typedef int64_t loff_t; 
typedef loff_t off_t;

typedef struct inode {
    uint64_t            ino;
    uint32_t            mode;
    uint64_t            size;
    struct super_block *sb;
    atomic_t            refcount;      // file / dentry 等活跃引用
    struct inode_operations *inode_ops;
    bool                deleting;      // unlink后
    void               *private_data;
    /* ========== 两锁模型 ========== */
    /*
     * 1 结构锁（保护 size / truncate / setattr）
     * 读锁：
     *      read / write
     * 写锁：
     *      truncate
     *      setattr(size)
     */
    rwlock_t            i_meta_lock;
    /*
     *  2 数据写锁（保证单写）
     * 读操作不需要
     * write/truncate 需要
     */
    mutex_t             i_data_lock;
    list_head_t    lru_node;
    struct file_operations *default_file_ops;
} inode_t;

#define DENTRY_FLAG_MOUNTPOINT          (0x1<<0)
#define DENTRY_FLAG_MOUNT_IN_PROGRESS   (0x1<<1)
#define DENTRY_BLOCK_ROOT               (0x1<<2)
#define DENTRY_BLOCK_DEV                (0x1<<3)
#define DENTRY_CHARACTER_DEV            (0x1<<4)

typedef struct dentry {
    char               *name;
    struct inode       *inode;
    struct dentry      *parent;
    list_head_t         child_list;
    list_head_t         child_list_item;
    /* cache计数 */
    list_head_t         cache_list_item;
    atomic_t            in_cache_list;
    /* 标志位 */
    uint64_t            flags;
    /* 引用计数 */
    atomic_t            refcount;
    /* 状态 */
    bool                deleted;
    struct super_block *mounted_here;
    struct super_block *in_mnt;
} dentry_t;

typedef struct super_block {
    int             fs_type;
    atomic_t        fs_ref;
    rwlock_t        sb_lock;
    void            *private_data;
    struct super_operations *super_ops;
    struct dentry   *root;          // 挂载根 dentry
    struct dentry   *mountpoint;    // 挂载点 dentry（如果已挂载）
    struct partition *part;         // 关联的分区（如果有）
    list_head_t      mount_list;    // 全局挂载链表
} super_block_t;

typedef struct file {
    struct inode   *inode;
    struct dentry  *dentry;
    struct file_operations *file_ops;
    uint64_t        pos;
    atomic_t        refcount;
    int             flags;
    mutex_t         lock;
} file_t;

#define ATTR_SIZE      0x0001

typedef struct iattr {
    unsigned int ia_valid;   /* 哪些字段有效 */
    //umode_t      ia_mode;
    //kuid_t       ia_uid;
    //kgid_t       ia_gid;
    loff_t       ia_size;
    //struct timespec64 ia_atime;
    //struct timespec64 ia_mtime;
    //struct timespec64 ia_ctime;
} iattr_t;

typedef struct super_operations {
    inode_t *(*read_root_inode)(struct super_block *sb);
    /* 写回整个 superblock */
    int (*write_super)(struct super_block *sb);
    /* 同步所有数据 */
    int (*sync_fs)(struct super_block *sb);
    /* 释放 superblock */
    void (*put_super)(struct super_block *sb);
} super_operations_t;

typedef struct inode_operations {
    /* 目录操作 */
    int (*lookup)(struct inode *dir, struct dentry *dentry);
    int (*create)(struct inode *dir, struct dentry *dentry, int mode);
    int (*unlink)(struct inode *dir, struct dentry *dentry);
    int (*mkdir)(struct inode *dir, struct dentry *dentry, int mode);
    int (*rmdir)(struct inode *dir, struct dentry *dentry);
    int (*delete)(struct inode *dir);
    int (*rename)(struct inode *old_dir, struct dentry *old_dentry,struct inode *new_dir, const char *new_name);
    /* 属性修改 */
    int (*setattr)(struct inode *inode, struct iattr *attr);
} inode_operations_t;

typedef struct file_operations {
    int     (*open)(struct inode *inode, struct file *file);
    int     (*release)(struct inode *inode, struct file *file);
    ssize_t (*read)(struct file *file, char __user *buf,
                    size_t len, int64_t *ppos);
    ssize_t (*write)(struct file *file, const char __user *buf,
                     size_t len, int64_t *ppos);
    int     (*fsync)(struct file *file);
    int     (*readdir)(struct file *file, struct dirent __user *dirp, unsigned int count);
} file_operations_t;

typedef struct vfs_manager {
    spin_list_head_t mount_list;
    spin_list_head_t inode_list;
    spin_list_head_t dentry_cache_list;
    spin_list_head_t dentry_deleting_list;
    mutex_t mount_lock;
    atomic_t dentry_cache_num;
    rwlock_t namespace_lock;
    dentry_t *root;
} vfs_manager_t;

static inline void dentry_get(dentry_t *d)
{
    atomic_inc(&d->refcount);
}
void dentry_put(dentry_t *dentry);
void dentry_delete(dentry_t *dentry);
static inline void inode_get(inode_t *inode)
{
    atomic_inc(&inode->refcount);
}
void inode_put(inode_t *inode);
static inline void sb_get(super_block_t *sb)
{
    atomic_inc(&sb->fs_ref);
}
static inline void sb_put(super_block_t *sb)
{
    atomic_dec(&sb->fs_ref);
}
static inline void getin_cwd(dentry_t *dentry){
    sb_get(dentry->in_mnt);
    dentry_get(dentry);
}
static inline void exit_cwd(dentry_t *dentry){
    dentry_put(dentry);
    sb_put(dentry->in_mnt);
}
dentry_t *dentry_create(const char *name,inode_t *inode);

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

#include "fs/block.h"

int vfs_mount(partition_t *part, const char *target_path, int fstype);
int sys_umount(const char *target_path);
dentry_t *vfs_lookup(dentry_t *start,const char *target_path);
dentry_t *__vfs_lookup_locked(dentry_t *start,const char *target_path);
super_block_t *super_block_create(partition_t *device,int fstype);

#endif
