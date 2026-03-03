// devfs.c
#include "fs/fs.h"
#include "fs/devfs.h"
#include "fs/fsmod.h"
#include "mm/mm.h"
#include "lib/string.h"
#include "lib/safelist.h"
#include "view/view.h"
#include "const.h"

// 全局变量，用于快速访问 devfs 的超级块（假设只有一个 devfs 实例）
struct super_block *devfs_sb = NULL;

// 静态函数声明
static int devfs_lookup(UNUSED struct inode *dir, UNUSED struct dentry *dentry);
static int devfs_create(UNUSED struct inode *dir, UNUSED struct dentry *dentry, UNUSED int mode);
static int devfs_unlink(UNUSED struct inode *dir, UNUSED struct dentry *dentry);
static int devfs_mkdir(UNUSED struct inode *dir, UNUSED struct dentry *dentry, UNUSED int mode);
static int devfs_rmdir(UNUSED struct inode *dir, UNUSED struct dentry *dentry);
static int devfs_rename(UNUSED struct inode *old_dir, UNUSED struct dentry *old_dentry,
                        UNUSED struct inode *new_dir, UNUSED const char *new_name);
static int devfs_setattr(UNUSED struct inode *inode, UNUSED struct iattr *attr);
static int devfs_delete(UNUSED struct inode *inode);

static int devfs_root_open(UNUSED inode_t *inode,UNUSED file_t *file);
static int devfs_root_readdir(struct file *file, struct dirent __user *dirp, unsigned int count);

// 根 inode 的 inode_operations
struct inode_operations devfs_root_iops = {
    .lookup = devfs_lookup,
    .create = devfs_create,
    .unlink = devfs_unlink,
    .mkdir  = devfs_mkdir,
    .rmdir  = devfs_rmdir,
    .rename = devfs_rename,
    .setattr = devfs_setattr,
    .delete = devfs_delete
};

struct file_operations devfs_root_file_ops = {
    .open = devfs_root_open,
    .readdir = devfs_root_readdir,
    .read = NULL,
    .write = NULL,
    .fsync = NULL,
    .release = NULL
};

// 超级块操作
static inode_t *devfs_read_root_inode(struct super_block *sb)
{
    sb->private_data = NULL;
    // 创建根目录 inode
    inode_t *inode = (inode_t *)kmalloc(sizeof(inode_t));
    if (!inode) return NULL;
    memset(inode, 0, sizeof(inode_t));
    inode->ino = 1;
    inode->mode = S_IFDIR | 0555;  // 只读目录
    inode->sb = sb;
    atomic_set(&inode->refcount, 1);
    atomic_set(&inode->link_count,1);
    inode->inode_ops = &devfs_root_iops;
    inode->default_file_ops = &devfs_root_file_ops;  // 目录没有文件操作
    rwlock_init(&inode->i_meta_lock);
    mutex_init(&inode->i_data_lock);
    INIT_LIST_HEAD(&inode->lru_node);
    devfs_sb = sb;  // 保存全局引用
    return inode;
}

static int devfs_write_super(UNUSED struct super_block *sb)
{
    return 0;  // 无写回
}

static int devfs_sync_fs(UNUSED struct super_block *sb)
{
    return 0;
}

static void devfs_put_super(struct super_block *sb)
{
    sb->private_data = NULL;
}

struct super_operations devfs_super_ops = {
    .read_root_inode = devfs_read_root_inode,
    .write_super = devfs_write_super,
    .sync_fs = devfs_sync_fs,
    .put_super = devfs_put_super,
};

static int devfs_lookup(UNUSED struct inode *dir, UNUSED struct dentry *dentry)
{
    return -1;
}

static int devfs_create(UNUSED struct inode *dir, UNUSED struct dentry *dentry, UNUSED int mode)
{
    return -1;  // 不允许创建文件
}

static int devfs_unlink(UNUSED struct inode *dir, UNUSED struct dentry *dentry)
{
    return -1;  // 不允许删除
}

static int devfs_mkdir(UNUSED struct inode *dir, UNUSED struct dentry *dentry, UNUSED int mode)
{
    return -1;  // 不允许创建子目录
}

static int devfs_rmdir(UNUSED struct inode *dir, UNUSED struct dentry *dentry)
{
    return -1;  // 不允许删除目录
}

static int devfs_rename(UNUSED struct inode *old_dir, UNUSED struct dentry *old_dentry,
                        UNUSED struct inode *new_dir, UNUSED const char *new_name)
{
    return -1;  // 不允许重命名
}

static int devfs_setattr(UNUSED struct inode *inode, UNUSED struct iattr *attr)
{
    return -1;  // 不允许修改属性
}

static int devfs_delete(UNUSED struct inode *inode)
{
    return 0;
}

static int devfs_root_open(UNUSED inode_t *inode,UNUSED file_t *file) {
    return 0;
}

static int devfs_root_readdir(struct file *file, struct dirent __user *dirp, unsigned int count){
    dentry_t *dentry = file->dentry;
    int pos = (int)file->pos;

    char buf[300];
    struct dirent *ud = (void *)buf;

    dentry_t *child;
    int i = 0;
    list_for_each_entry(child,&dentry->child_list,child_list_item){
        if (i == pos){
            ud->d_ino = child->inode->ino;
            ud->d_type = (child->flags & DENTRY_CHARACTER_DEV) ? DT_CHR : DT_BLK;
            uint32_t namelen = strlen((void *)child->name) + 1;
            ud->d_reclen = sizeof(struct dirent) + (uint16_t)namelen;
            if (ud->d_reclen > count || ud->d_reclen > 300)
                return -1;
            memcpy(ud->d_name,child->name,namelen);
            if (copy_to_user(dirp,ud,ud->d_reclen) != 0)
                return -1;
            file->pos++;
            return 0;
        }
        i++;
    }
    return -1;
}
