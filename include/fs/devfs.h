#ifndef OS_DEVFS_H
#define OS_DEVFS_H

#include <stdint.h>
#include "fs/fs.h"

extern struct super_operations devfs_super_ops;
extern struct inode_operations devfs_root_iops;
int devfs_block_register(const char *name, int mode, struct file_operations *fops,struct block_device *private_data,uint64_t flags,bool locked);
int __devfs_unregister_locked(const char *name);
extern struct super_block *devfs_sb;

#endif
