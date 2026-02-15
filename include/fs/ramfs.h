#ifndef OS_RAMFS_H
#define OS_RAMFS_H

#include "fs/fs.h"

typedef struct ramfs_inode {
    int mode;
    void *data;
    size_t data_size;
} ramfs_inode_t;

// 目录项链表节点（用于目录）
typedef struct ramfs_dirent {
    char *name;
    struct inode *inode;
    struct list_head list;
} ramfs_dirent_t;

// 声明全局操作表
extern struct super_operations ramfs_super_ops;
extern struct inode_operations ramfs_inode_ops;
extern struct file_operations ramfs_file_ops;

#endif