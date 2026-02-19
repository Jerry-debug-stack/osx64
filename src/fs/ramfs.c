#include "fs/ramfs.h"
#include "mm/mm.h"
#include "lib/string.h"
#include "view/view.h"
#include "fs/fsmod.h"
#include "const.h"

static ramfs_inode_t *ramfs_i(inode_t *inode) {
    return (ramfs_inode_t *)inode->private_data;
}

static void ramfs_free_inode(inode_t *inode) {
    ramfs_inode_t *ri = ramfs_i(inode);
    if (!ri) return;
    if (S_ISDIR(inode->mode)) {
        // 释放目录下的所有 dirent 结构（但不释放子 inode，由 VFS 负责）
        ramfs_dirent_t *dirent, *tmp;
        list_for_each_entry_safe(dirent, tmp, (struct list_head *)ri->data, list) {
            kfree(dirent->name);
            kfree(dirent);
        }
    } else if (S_ISREG(inode->mode)) {
        // 释放文件缓冲区
        if (ri->data) kfree(ri->data);
    }
    kfree(ri);
    inode->private_data = NULL;
}

static inode_t *ramfs_new_inode(super_block_t *sb, int mode) {
    inode_t *inode = (inode_t *)kmalloc(sizeof(inode_t));
    if (!inode) return NULL;
    memset(inode, 0, sizeof(inode_t));
    inode->ino = (uint64_t)inode;  // 简单用地址作为 inode 号
    inode->mode = mode;
    inode->sb = sb;
    inode->default_file_ops = &ramfs_file_ops;
    atomic_set(&inode->refcount, 1);
    atomic_set(&inode->link_count, 0);  // 初始无硬链接
    inode->inode_ops = &ramfs_inode_ops;
    rwlock_init(&inode->i_meta_lock);
    mutex_init(&inode->i_data_lock);
    INIT_LIST_HEAD(&inode->lru_node);

    ramfs_inode_t *ri = (ramfs_inode_t *)kmalloc(sizeof(ramfs_inode_t));
    if (!ri) {
        kfree(inode);
        return NULL;
    }
    ri->mode = mode;
    ri->data = NULL;
    ri->data_size = 0;
    if (S_ISDIR(mode)) {
        // 初始化目录项链表
        struct list_head *head = (struct list_head *)kmalloc(sizeof(struct list_head));
        if (!head) {
            kfree(ri);
            kfree(inode);
            return NULL;
        }
        INIT_LIST_HEAD(head);
        ri->data = head;
    }
    inode->private_data = ri;

    // 添加到超级块的 inode 列表（可选）
    // spin_list_add(&inode->sb_list, &sb->inode_list);  // 需要 inode 中有 sb_list 字段
    return inode;
}

/* 在目录中查找子项 */
static ramfs_dirent_t *ramfs_find_dirent(inode_t *dir, const char *name) {
    if (!S_ISDIR(dir->mode)) return NULL;
    struct list_head *head = (struct list_head *)ramfs_i(dir)->data;
    ramfs_dirent_t *dirent;
    list_for_each_entry(dirent, head, list) {
        if (strcmp(dirent->name, name) == 0)
            return dirent;
    }
    return NULL;
}

/* 向目录添加子项 */
static int ramfs_add_dirent(inode_t *dir, const char *name, inode_t *inode) {
    if (!S_ISDIR(dir->mode)) return -1;
    struct list_head *head = (struct list_head *)ramfs_i(dir)->data;
    ramfs_dirent_t *dirent = (ramfs_dirent_t *)kmalloc(sizeof(ramfs_dirent_t));
    if (!dirent) return -1;
    dirent->name = kstrdup(name);
    if (!dirent->name) {
        kfree(dirent);
        return -1;
    }
    dirent->inode = inode;
    list_add_tail(&dirent->list, head);
    ramfs_i(dir)->data_size++;
    atomic_inc(&inode->link_count);  // 目录项增加硬链接计数
    return 0;
}

/* -------------------- 超级块操作 -------------------- */
static inode_t *ramfs_read_root_inode(super_block_t *sb) {
    return ramfs_new_inode(sb, S_IFDIR | 0755);
}

static void ramfs_put_super(UNUSED super_block_t *sb) {
    // 释放超级块下所有 inode（VFS 会通过 inode_put 逐步释放，这里可以空实现）
    // 如果需要主动清理，可以遍历 sb->inode_list 释放
}

struct super_operations ramfs_super_ops = {
    .read_root_inode = ramfs_read_root_inode,
    .put_super       = ramfs_put_super,
    // 其他操作暂不实现
};

/* -------------------- inode 操作 -------------------- */
static int ramfs_lookup(inode_t *dir, dentry_t *dentry) {
    if (!S_ISDIR(dir->mode)) return -1;

    ramfs_dirent_t *dirent = ramfs_find_dirent(dir, dentry->name);
    if (!dirent) {
        dentry->negative = true;
        return -1;  // 不存在
    }

    inode_t *inode = dirent->inode;
    dentry->inode = inode;
    atomic_inc(&inode->refcount);   // dentry 引用 inode
    dentry->negative = false;
    return 0;
}

static int ramfs_create(inode_t *dir, dentry_t *dentry, int mode) {
    if (!S_ISDIR(dir->mode)) return -1;

    // 检查是否已存在
    if (ramfs_find_dirent(dir, dentry->name))
        return -1;  // EEXIST

    // 创建新 inode（普通文件）
    inode_t *inode = ramfs_new_inode(dir->sb, S_IFREG | mode);
    if (!inode) return -1;

    // 添加到目录
    if (ramfs_add_dirent(dir, dentry->name, inode) < 0) {
        inode_put(inode);
        return -1;
    }

    dentry->inode = inode;
    atomic_inc(&inode->refcount);
    dentry->negative = false;
    return 0;
}

static int ramfs_mkdir(inode_t *dir, dentry_t *dentry, int mode) {
    if (!S_ISDIR(dir->mode)) return -1;

    if (ramfs_find_dirent(dir, dentry->name))
        return -1;  // EEXIST

    inode_t *inode = ramfs_new_inode(dir->sb, S_IFDIR | mode);
    if (!inode) return -1;

    if (ramfs_add_dirent(dir, dentry->name, inode) < 0) {
        inode_put(inode);
        return -1;
    }

    dentry->inode = inode;
    atomic_inc(&inode->refcount);
    dentry->negative = false;
    return 0;
}

static int ramfs_unlink(inode_t *dir, dentry_t *dentry) {
    if (!S_ISDIR(dir->mode) || !dentry->inode) return -1;

    ramfs_dirent_t *dirent = ramfs_find_dirent(dir, dentry->name);
    if (!dirent) return -1;

    list_del(&dirent->list);
    kfree(dirent->name);
    kfree(dirent);
    atomic_dec(&dentry->inode->link_count);
    // dentry 本身会被 VFS 清理
    return 0;
}

static int ramfs_rmdir(inode_t *dir, dentry_t *dentry)
{
    inode_t *inode = dentry->inode;
    ramfs_dirent_t *dirent;

    if (!S_ISDIR(inode->mode))
        return -1;  // 不是目录

    // 检查子目录是否为空（通过子目录的内部链表）
    ramfs_inode_t *ri = ramfs_i(inode);
    if (ri && ri->data && !list_empty((struct list_head *)ri->data))
        return -1;  // 子目录非空

    // 可选：同时检查 VFS 的 child_list
    if (!list_empty(&dentry->child_list))
        return -1;

    // 在父目录的内部链表中查找并删除对应的 dirent
    dirent = ramfs_find_dirent(dir, dentry->name);
    if (!dirent)
        return -1;  // 理论不应发生

    list_del(&dirent->list);
    kfree(dirent->name);
    kfree(dirent);

    // 减少子目录 inode 的 link_count
    atomic_dec(&inode->link_count);

    return 0;
}

static int ramfs_setattr(inode_t *inode, struct iattr *attr)
{
    if (attr->ia_valid & ATTR_SIZE) {
        loff_t new_size = attr->ia_size;
        ramfs_inode_t *ri = ramfs_i(inode);

        if (new_size > (loff_t)ri->data_size) {
            // 扩展：分配新内存，复制旧数据，新增部分清零
            void *new_data = kmalloc(new_size);
            if (!new_data) {
                return -1;
            }
            if (ri->data) {
                memcpy(new_data, ri->data, ri->data_size);
                kfree(ri->data);
            }
            memset((char *)new_data + ri->data_size, 0, new_size - ri->data_size);
            ri->data = new_data;
            ri->data_size = new_size;
        } else if (new_size < (loff_t)ri->data_size) {
            // 截断：重新分配更小的内存（或直接缩小，但为简单重新分配）
            void *new_data = NULL;
            if (new_size > 0) {
                new_data = kmalloc(new_size);
                if (!new_data) {
                    return -1;
                }
                memcpy(new_data, ri->data, new_size);
            }
            kfree(ri->data);
            ri->data = new_data;
            ri->data_size = new_size;
        }
        inode->size = new_size;
    }
    return 0;
}

static int ramfs_delete(inode_t *inode) {
    // 当 link_count 降到 0 时调用，释放资源
    ramfs_free_inode(inode);
    return 0;
}

struct inode_operations ramfs_inode_ops = {
    .lookup = ramfs_lookup,
    .create = ramfs_create,
    .mkdir  = ramfs_mkdir,
    .rmdir  = ramfs_rmdir,
    .unlink = ramfs_unlink,
    .delete = ramfs_delete,
    .setattr = ramfs_setattr
};

static int ramfs_open(UNUSED inode_t *inode,UNUSED file_t *file) {
    return 0;
}

static int ramfs_release(UNUSED inode_t *inode,UNUSED file_t *file) {
    return 0;
}

static ssize_t ramfs_read(file_t *file, char __user *buf, size_t len, loff_t *ppos) {
    inode_t *inode = file->inode;
    if (!S_ISREG(inode->mode)) return -1;

    ramfs_inode_t *ri = ramfs_i(inode);
    if (!ri->data) return 0;

    loff_t pos = *ppos;
    if (pos >= (loff_t)inode->size) return 0;
    if (pos + len > inode->size)
        len = inode->size - pos;

    memcpy(buf, (char *)ri->data + pos, len);
    *ppos = pos + len;
    return len;
}

static ssize_t ramfs_write(file_t *file, const char __user *buf, size_t len, loff_t *ppos) {
    inode_t *inode = file->inode;
    if (!S_ISREG(inode->mode)) return -1;

    ramfs_inode_t *ri = ramfs_i(inode);
    loff_t pos = *ppos;
    size_t new_size = pos + len;

    if (new_size > ri->data_size) {
        void *new_data = kmalloc(new_size);
        if (!new_data) return -1;
        if (ri->data) {
            memcpy(new_data, ri->data, ri->data_size);
            kfree(ri->data);
        }
        memset((char *)new_data + ri->data_size, 0, new_size - ri->data_size);
        ri->data = new_data;
        ri->data_size = new_size;
        inode->size = new_size;
    }

    memcpy((char *)ri->data + pos, (char *)buf, len);
    *ppos = pos + len;
    return len;
}

struct file_operations ramfs_file_ops = {
    .open   = ramfs_open,
    .release= ramfs_release,
    .read   = ramfs_read,
    .write  = ramfs_write,
    // .fsync 暂不实现
};
