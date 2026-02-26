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
static int devfs_lookup(struct inode *dir, struct dentry *dentry);
static int devfs_create(struct inode *dir, struct dentry *dentry, int mode);
static int devfs_unlink(struct inode *dir, struct dentry *dentry);
static int devfs_mkdir(struct inode *dir, struct dentry *dentry, int mode);
static int devfs_rmdir(struct inode *dir, struct dentry *dentry);
static int devfs_rename(struct inode *old_dir, struct dentry *old_dentry,
                        struct inode *new_dir, const char *new_name);
static int devfs_setattr(struct inode *inode, struct iattr *attr);
static int devfs_delete(UNUSED struct inode *inode);

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
    inode->default_file_ops = NULL;  // 目录没有文件操作
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
    return -1;
}
