#ifndef OS_RAMFS_H
#define OS_RAMFS_H

#include "fs/fs.h"

// ramfs 内部节点
typedef struct ramfs_node {
    uint64_t         ino;            // 节点号
    int              mode;           // 文件类型和权限
    atomic_t         link_count;     // 硬链接计数（与 VFS 同步）
    void            *data;           // 文件内容缓冲区（普通文件）
    size_t           data_size;      // 缓冲区大小
    size_t           file_size;      // 实际文件大小
    struct list_head dir_entries;    // 目录项链表头（仅当目录时使用）
    spinlock_t       lock;           // 保护节点内部
    struct list_head hash_list;      // 全局哈希表链表
} ramfs_node_t;

// 目录项结构
typedef struct ramfs_dirent {
    char            *name;
    uint64_t         ino;            // 子节点 ino
    struct list_head list;
} ramfs_dirent_t;

// ramfs 超级块私有数据
typedef struct ramfs_sb_info {
    struct list_head node_list;      // 所有内部节点链表
    spinlock_t       lock;
    uint64_t         next_ino;       // 简单的 ino 分配器
} ramfs_sb_info_t;

// 声明全局操作表
extern struct super_operations ramfs_super_ops;
extern struct inode_operations ramfs_inode_ops;
extern struct file_operations ramfs_file_ops;

#endif