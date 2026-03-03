#include "fs/ramfs.h"
#include "fs/fcntl.h"
#include "mm/mm.h"
#include "lib/string.h"
#include "view/view.h"
#include "fs/fsmod.h"
#include "const.h"

extern struct file_operations ramfs_file_ops;

static ramfs_node_t *ramfs_alloc_node(struct super_block *sb, int mode)
{
    ramfs_node_t *node = kmalloc(sizeof(ramfs_node_t));
    if (!node) return NULL;
    memset(node, 0, sizeof(ramfs_node_t));
    node->mode = mode;
    atomic_set(&node->link_count, 1);
    spin_lock_init(&node->lock);
    INIT_LIST_HEAD(&node->dir_entries);

    ramfs_sb_info_t *sbi = (ramfs_sb_info_t *)sb->private_data;
    spin_lock(&sbi->lock);
    node->ino = ++sbi->next_ino;
    list_add_tail(&node->hash_list, &sbi->node_list);
    spin_unlock(&sbi->lock);
    return node;
}

static ramfs_node_t *ramfs_find_node(struct super_block *sb, uint64_t ino)
{
    ramfs_sb_info_t *sbi = (ramfs_sb_info_t *)sb->private_data;
    ramfs_node_t *node;
    spin_lock(&sbi->lock);
    list_for_each_entry(node, &sbi->node_list, hash_list) {
        if (node->ino == ino) {
            spin_unlock(&sbi->lock);
            return node;
        }
    }
    spin_unlock(&sbi->lock);
    return NULL;
}

static inode_t *ramfs_new_inode(super_block_t *sb, int mode)
{
    inode_t *inode = (inode_t *)kmalloc(sizeof(inode_t));
    if (!inode) return NULL;
    memset(inode, 0, sizeof(inode_t));
    inode->ino = (uint64_t)inode; // 简单用地址，或者使用 sb 分配的序号
    inode->mode = mode;
    inode->sb = sb;
    atomic_set(&inode->link_count,1);
    atomic_set(&inode->refcount, 1);
    inode->inode_ops = &ramfs_inode_ops;
    inode->default_file_ops = &ramfs_file_ops;
    rwlock_init(&inode->i_meta_lock);
    mutex_init(&inode->i_data_lock);
    INIT_LIST_HEAD(&inode->lru_node);
    // 如果需要，可以加入超级块的 inode 列表
    return inode;
}

/* -------------------- 超级块操作 -------------------- */
static inode_t *ramfs_read_root_inode(super_block_t *sb) {
    ramfs_sb_info_t *sbi = kmalloc(sizeof(ramfs_sb_info_t));
    if (!sbi) return NULL;
    INIT_LIST_HEAD(&sbi->node_list);
    spin_lock_init(&sbi->lock);
    sbi->next_ino = 0;
    sb->private_data = sbi;

    // 创建根节点
    ramfs_node_t *root_node = ramfs_alloc_node(sb, S_IFDIR | 0755);
    if (!root_node) {
        kfree(sbi);
        return NULL;
    }

    // 创建根 VFS inode
    inode_t *root_inode = ramfs_new_inode(sb, S_IFDIR | 0755);
    if (!root_inode) {
        // 释放 root_node
        return NULL;
    }
    root_inode->private_data = root_node;
    root_inode->size = 0;
    return root_inode;
}

static void ramfs_put_super(UNUSED super_block_t *sb) {
    ramfs_sb_info_t *sbi = (ramfs_sb_info_t *)sb->private_data;
    ramfs_node_t *node, *tmp;
    list_for_each_entry_safe(node, tmp, &sbi->node_list, hash_list) {
        if (node->data) kfree(node->data);
        // 目录项链表可能仍有残留，但卸载时应该已空
        kfree(node);
    }
    kfree(sbi);
}

struct super_operations ramfs_super_ops = {
    .read_root_inode = ramfs_read_root_inode,
    .put_super       = ramfs_put_super,
    // 其他操作暂不实现
};

/* -------------------- inode 操作 -------------------- */
static int ramfs_lookup(inode_t *dir, dentry_t *dentry) {
    ramfs_node_t *dir_node = (ramfs_node_t *)dir->private_data;
    ramfs_dirent_t *de;
    ramfs_node_t *child_node;

    spin_lock(&dir_node->lock);
    list_for_each_entry(de, &dir_node->dir_entries, list) {
        if (strcmp(de->name, dentry->name) == 0) {
            child_node = ramfs_find_node(dir->sb, de->ino);
            spin_unlock(&dir_node->lock);
            if (!child_node) return -1; // 异常

            // 创建新 VFS inode
            inode_t *inode = ramfs_new_inode(dir->sb, child_node->mode);
            if (!inode) return -1;
            inode->private_data = child_node;
            inode->size = child_node->file_size;
            dentry->inode = inode;
            dentry->in_mnt = dir->sb; 
            atomic_inc(&inode->refcount);
            return 0;
        }
    }
    spin_unlock(&dir_node->lock);
    return -1;
}

static int ramfs_create(inode_t *dir, dentry_t *dentry, int mode) {
    ramfs_node_t *dir_node = (ramfs_node_t *)dir->private_data;
    ramfs_node_t *child_node = ramfs_alloc_node(dir->sb, S_IFREG | mode);
    if (!child_node) return -1;

    // 分配 VFS inode
    inode_t *inode = ramfs_new_inode(dir->sb, child_node->mode);
    if (!inode) {
        // 回滚节点分配
        // 简化：直接返回，实际应释放 child_node
        return -1;
    }
    inode->private_data = child_node;
    inode->size = 0;
    child_node->file_size = 0;

    // 创建目录项
    ramfs_dirent_t *de = kmalloc(sizeof(ramfs_dirent_t));
    if (!de) {
        // 释放资源
        return -1;
    }
    de->name = kstrdup(dentry->name);
    if (!de->name) {
        kfree(de);
        return -1;
    }
    de->ino = child_node->ino;

    spin_lock(&dir_node->lock);
    list_add_tail(&de->list, &dir_node->dir_entries);
    spin_unlock(&dir_node->lock);

    dentry->inode = inode;
    return 0;
}

static int ramfs_mkdir(inode_t *dir, dentry_t *dentry, int mode)
{
    ramfs_node_t *dir_node = (ramfs_node_t *)dir->private_data;
    ramfs_node_t *child_node;
    inode_t *inode;
    ramfs_dirent_t *de;
    ramfs_sb_info_t *sbi = (ramfs_sb_info_t *)dir->sb->private_data;

    // 1. 分配内部节点
    child_node = ramfs_alloc_node(dir->sb, S_IFDIR | (mode & 0777));
    if (!child_node)
        return -1;

    // 2. 分配 VFS inode
    inode = ramfs_new_inode(dir->sb, S_IFDIR | (mode & 0777));
    if (!inode)
        goto fail_free_node;

    // 3. 关联
    inode->private_data = child_node;
    inode->size = 0;
    child_node->file_size = 0;

    // 4. 创建目录项（用于父目录）
    de = kmalloc(sizeof(ramfs_dirent_t));
    if (!de)
        goto fail_free_inode;
    de->name = kstrdup(dentry->name);
    if (!de->name) {
        kfree(de);
        goto fail_free_inode;
    }
    de->ino = child_node->ino;

    // 5. 将目录项加入父目录的内部链表
    spin_lock(&dir_node->lock);
    list_add_tail(&de->list, &dir_node->dir_entries);
    spin_unlock(&dir_node->lock);

    // 6. 关联 dentry
    dentry->inode = inode;

    return 0;

fail_free_inode:
    kfree(inode);
fail_free_node:
    // 从超级块列表中移除并释放内部节点
    spin_lock(&sbi->lock);
    list_del(&child_node->hash_list);
    spin_unlock(&sbi->lock);
    kfree(child_node);
    return -1;
}

static int ramfs_unlink(inode_t *dir, dentry_t *dentry) {
    ramfs_node_t *dir_node = (ramfs_node_t *)dir->private_data;
    ramfs_node_t *child_node = (ramfs_node_t *)dentry->inode->private_data;
    ramfs_dirent_t *de;

    spin_lock(&dir_node->lock);
    list_for_each_entry(de, &dir_node->dir_entries, list) {
        if (strcmp(de->name, dentry->name) == 0) {
            list_del(&de->list);
            kfree(de->name);
            kfree(de);
            break;
        }
    }
    spin_unlock(&dir_node->lock);

    atomic_dec(&child_node->link_count);
    // VFS inode 的 link_count 由 VFS 在 unlink 后减少
    return 0;
}

static int ramfs_rmdir(inode_t *dir, dentry_t *dentry)
{
    ramfs_node_t *dir_node = (ramfs_node_t *)dir->private_data;
    ramfs_node_t *child_node = (ramfs_node_t *)dentry->inode->private_data;
    ramfs_sb_info_t *sbi = (ramfs_sb_info_t *)dir->sb->private_data;
    ramfs_dirent_t *de;
    int found = 0;

    spin_lock(&child_node->lock);
    if (!list_empty(&child_node->dir_entries)) {
        spin_unlock(&child_node->lock);
        return -1;
    }
    spin_unlock(&child_node->lock);

    // 在父目录的目录项链表中查找并删除对应项
    spin_lock(&dir_node->lock);
    list_for_each_entry(de, &dir_node->dir_entries, list) {
        if (strcmp(de->name, dentry->name) == 0) {
            list_del(&de->list);
            kfree(de->name);
            kfree(de);
            found = 1;
            break;
        }
    }
    spin_unlock(&dir_node->lock);

    if (!found)
        return -1;  // 理论上不应发生

    if (atomic_dec_and_test(&child_node->link_count)) {
        spin_lock(&sbi->lock);
        list_del(&child_node->hash_list);
        spin_unlock(&sbi->lock);
        kfree(child_node);
    }

    return 0;
}

static int ramfs_setattr(inode_t *inode, struct iattr *attr)
{
    ramfs_node_t *ri = inode->private_data;
    if (attr->ia_valid & ATTR_SIZE) {
        loff_t new_size = attr->ia_size;

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

static int ramfs_delete(UNUSED inode_t *inode) {
    if (atomic_read(&inode->refcount) == 0){
        // 内部节点可释放
        ramfs_sb_info_t *sbi = (ramfs_sb_info_t *)inode->sb->private_data;
        ramfs_node_t *child_node = (ramfs_node_t *)inode->private_data;
        spin_lock(&sbi->lock);
        list_del(&child_node->hash_list);
        spin_unlock(&sbi->lock);
        if (child_node->data) kfree(child_node->data);
        kfree(child_node);
    }
    return 0;
}

static int ramfs_rename(inode_t *old_dir, dentry_t *old_dentry,
                        inode_t *new_dir, const char *new_name)
{
    ramfs_node_t *old_dir_node = (ramfs_node_t *)old_dir->private_data;
    ramfs_node_t *new_dir_node = (ramfs_node_t *)new_dir->private_data;
    ramfs_dirent_t *de;
    uint64_t target_ino;
    int found = 0;

    if (old_dir == new_dir && strcmp(old_dentry->name, new_name) == 0)
        return 0;

    spin_lock(&old_dir_node->lock);
    list_for_each_entry(de, &old_dir_node->dir_entries, list) {
        if (strcmp(de->name, old_dentry->name) == 0) {
            target_ino = de->ino;
            list_del(&de->list);
            kfree(de->name);
            kfree(de);
            found = 1;
            break;
        }
    }
    spin_unlock(&old_dir_node->lock);

    if (!found)
        return -1;

    de = kmalloc(sizeof(ramfs_dirent_t));
    if (!de)
        return -1;
    de->name = kstrdup(new_name);
    if (!de->name) {
        kfree(de);
        return -1;
    }
    de->ino = target_ino;

    spin_lock(&new_dir_node->lock);
    list_add_tail(&de->list, &new_dir_node->dir_entries);
    spin_unlock(&new_dir_node->lock);

    return 0;
}

struct inode_operations ramfs_inode_ops = {
    .lookup = ramfs_lookup,
    .create = ramfs_create,
    .mkdir  = ramfs_mkdir,
    .rmdir  = ramfs_rmdir,
    .unlink = ramfs_unlink,
    .delete = ramfs_delete,
    .setattr = ramfs_setattr,
    .rename = ramfs_rename
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

    ramfs_node_t *ri = inode->private_data;
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

    ramfs_node_t *ri = inode->private_data;
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

static int ramfs_fsync(UNUSED struct file *file){
    return 0;
}

static int ramfs_readdir(struct file *file, struct dirent __user *dirp, unsigned int count)
{
    inode_t *inode = file->inode;
    int pos = (int)file->pos;
    unsigned int reclen;

    char buf[300];
    struct dirent *ud = (void *)buf;
    ramfs_node_t *dir_node = (ramfs_node_t *)inode->private_data;
    super_block_t *sb = inode->sb;

    int i = 0;
    ramfs_dirent_t *de = NULL;
    uint64_t child_ino;

    spin_lock(&dir_node->lock);
    list_for_each_entry(de, &dir_node->dir_entries, list) {
        if (i == pos) {
            child_ino = de->ino;
            strcpy(ud->d_name, de->name);
            break;
        }
        i++;
    }
    if (!de) { // 没有找到对应条目，表示目录结束
        spin_unlock(&dir_node->lock);
        return -1;
    }
    spin_unlock(&dir_node->lock);

    // 根据子节点 inode 号获取节点，以确定文件类型
    ramfs_node_t *child_node = ramfs_find_node(sb, child_ino);
    if (!child_node) {
        return -1;
    }

    ud->d_ino = child_ino;
    if (S_ISDIR(child_node->mode))
        ud->d_type = DT_DIR;
    else if (S_ISREG(child_node->mode))
        ud->d_type = DT_REG;
    else
        ud->d_type = DT_UNKNOWN;

    reclen = sizeof(struct dirent) + strlen(ud->d_name) + 1;
    if (reclen > count) return -1;
    ud->d_reclen = reclen;
    if (copy_to_user(dirp, ud, reclen) != 0)
        return -1;

    file->pos = pos + 1;
    return 0;
}

struct file_operations ramfs_file_ops = {
    .open   = ramfs_open,
    .release= ramfs_release,
    .read   = ramfs_read,
    .write  = ramfs_write,
    .fsync  = ramfs_fsync,
    .readdir = ramfs_readdir
};
