#include "fs/fs.h"
#include "fs/ext2.h"
#include "fs/fsmod.h"
#include "mm/mm.h"
#include "const.h"
#include "lib/string.h"

static int ext2_create(struct inode *dir, struct dentry *dentry, int mode);
static int ext2_delete(struct inode *dir);
static int ext2_lookup(struct inode *dir, struct dentry *dentry);
static int ext2_mkdir(struct inode *dir, struct dentry *dentry, int mode);

static int ext2_setattr(struct inode *inode, struct iattr *attr);
static int ext2_unlink(struct inode *dir, struct dentry *dentry);

static ssize_t ext2_read(struct file *file, char __user *buf, size_t len, loff_t *ppos);
static ssize_t ext2_write(struct file *file, const char __user *buf, size_t len, loff_t *ppos);
static int ext2_open(UNUSED struct inode *inode,UNUSED struct file *file);
static int ext2_readdir(struct file *file, struct dirent __user *dirp, unsigned int count);
static int ext2_release(UNUSED struct inode *inode, UNUSED struct file *file);
static int ext2_fsync(UNUSED struct file *file);

static inode_t *ext2_read_root_inode(struct super_block *sb);
static int ext2_write_super(struct super_block *sb);
static int ext2_sync_fs(UNUSED struct super_block *sb);
static void ext2_put_super(struct super_block *sb);

/* tool functions */
static inline void ext2_set_block_all_zero(super_block_t *sb,uint32_t new_block,uint32_t block_size);
static void ext2_free_ind_blocks(struct super_block *sb, uint32_t block, int level);
static int ext2_truncate_blocks(struct inode *inode, loff_t new_size);
static int ext2_bmap_alloc(struct inode *inode, uint32_t logical_block);
static int ext2_write_inode(struct super_block *sb, uint32_t ino, struct ext2_inode *ei);
static inline uint16_t EXT2_DIR_REC_LEN(uint16_t name_len);
static int ext2_dir_add_entry(struct inode *dir, const char *name, uint32_t ino, uint8_t file_type);
static inode_t *ext2_iget(struct super_block *sb, uint32_t ino);
static int ext2_bmap(struct inode *inode, uint32_t logical_block);
static inode_t *ext2_create_VFS_inode(struct super_block *sb, struct ext2_inode *ei,uint64_t ino);
static int ext2_read_inode(struct super_block *sb, uint32_t ino, struct ext2_inode *ei);
static int ext2_rw_block(struct super_block *sb, uint32_t block_no, void *buf,bool read);
static int ext2_read_super(struct super_block *sb, struct ext2_super_block *es);
static int ext2_load_group_descs(struct super_block *sb);
static int ext2_alloc_block(struct super_block *sb);
static void ext2_free_block(struct super_block *sb, uint32_t block);
static int ext2_alloc_inode(struct super_block *sb);
static void ext2_free_inode(struct super_block *sb, uint32_t ino);

static struct inode_operations ext2_inode_ops = {
    .create = ext2_create,
    .delete = ext2_delete,
    .lookup = ext2_lookup,
    .mkdir = ext2_mkdir,
    .rename = NULL,
    .rmdir = NULL,
    .setattr = ext2_setattr,
    .unlink = ext2_unlink
};

static struct file_operations ext2_file_ops = {
    .read = ext2_read,
    .write = ext2_write,
    .open = ext2_open,
    .readdir = ext2_readdir,
    .release = ext2_release,
    .fsync = ext2_fsync
};

super_operations_t ext2_super_ops = {
    .read_root_inode = ext2_read_root_inode,
    .write_super = ext2_write_super,
    .sync_fs = ext2_sync_fs,
    .put_super = ext2_put_super,
};


static inode_t *ext2_read_root_inode(struct super_block *sb){
    if (!sb || !sb->part)
        return NULL;
    struct ext2_inode ei;
    inode_t *inode;
    ext2_fs_info_t *info = kmalloc(sizeof(ext2_fs_info_t));
    ext2_super_block_t *ext2sb = &info->es;
    if (!ext2sb)
        return NULL;
    if (ext2_read_super(sb,ext2sb))
        goto out_super_block;
    sb->private_data = ext2sb;
    info->block_size = 1024 << ext2sb->s_log_block_size;
    info->inode_size = ext2sb->s_inode_size;
    if (ext2_load_group_descs(sb))
        goto out_super_block;
    if (ext2_read_inode(sb,EXT2_ROOT_INO,&ei) < 0)
        goto out_super_block;
    inode = ext2_create_VFS_inode(sb,&ei,EXT2_ROOT_INO);
    if (!inode)
        goto out_super_block;
    return inode;

out_super_block:
    kfree(info);
    return NULL;
}

static int ext2_write_super(struct super_block *sb) {
    partition_t *part = sb->part;
    if (!part) return -1;
    block_device_t *dev = &part->device;
    uint32_t ext2_block_size = 1024;  // 默认，读取超级块前使用
    uint32_t dev_block_size = dev->block_size;
    uint32_t sectors_per_block = ext2_block_size / dev_block_size;
    part->device.write(&part->device,sectors_per_block,sectors_per_block,sb->private_data);
    return 0;
}

static int ext2_sync_fs(UNUSED struct super_block *sb) {return 0;}

static void ext2_put_super(struct super_block *sb) {
    ext2_fs_info_t *fsi = sb->private_data;
    uint32_t block_size = fsi->block_size;

    uint32_t descs_per_block = block_size / sizeof(struct ext2_group_desc);
    uint32_t first_desc_block = (block_size == 1024) ? 2 : 1;
    uint8_t *desc_block_buf = kmalloc(block_size);
    if (desc_block_buf) {
        for (uint32_t g = 0; g < fsi->group_count; g++) {
            ext2_group_desc_cache_t *gd = &fsi->group_descs[g];
                uint32_t block_idx = first_desc_block + g / descs_per_block;
                uint32_t offset = (g % descs_per_block) * sizeof(struct ext2_group_desc);
                if (ext2_rw_block(sb, block_idx, desc_block_buf,true) == 0) {
                    struct ext2_group_desc *desc = (struct ext2_group_desc *)(desc_block_buf + offset);
                    // 更新字段
                    desc->bg_free_blocks_count = gd->bg_free_blocks_count;
                    desc->bg_free_inodes_count = gd->bg_free_inodes_count;
                    desc->bg_used_dirs_count = gd->bg_used_dirs_count;
                    // 写回
                    ext2_rw_block(sb, block_idx, desc_block_buf,false);
                }
        }
        kfree(desc_block_buf);
    }

    ext2_write_super(sb);

    // 释放所有资源
    for (uint32_t g = 0; g < fsi->group_count; g++) {
        if (fsi->group_descs[g].block_bitmap)
            kfree(fsi->group_descs[g].block_bitmap);
        if (fsi->group_descs[g].inode_bitmap)
            kfree(fsi->group_descs[g].inode_bitmap);
    }
    kfree(fsi->group_descs);
    kfree(fsi);
}


static int ext2_create(struct inode *dir, struct dentry *dentry, int mode)
{
    struct super_block *sb = dir->sb;
    uint32_t ino;
    struct ext2_inode ei;
    inode_t *inode;

    // 分配inode号
    ino = ext2_alloc_inode(sb);
    if (ino == (uint32_t)-1) return -1;

    // 初始化磁盘inode
    memset(&ei, 0, sizeof(ei));
    ei.i_mode = mode | S_IFREG; // mode应已包含S_IFREG，但确保
    ei.i_uid = 0; // 简化，可设为当前用户
    ei.i_gid = 0;
    ei.i_size = 0;
    ei.i_atime = ei.i_ctime = ei.i_mtime = 0;
    /// @todo get_seconds(); // 需要时间函数
    ei.i_links_count = 1; // 新建文件链接数为1
    ei.i_blocks = 0; // 尚未分配块
    ei.i_flags = 0;
    // 清空块指针
    for (int i = 0; i < EXT2_N_BLOCKS; i++)
        ei.i_block[i] = 0;

    // 写回inode
    if (ext2_write_inode(sb, ino, &ei) < 0) {
        ext2_free_inode(sb, ino);
        return -1;
    }

    // 在父目录中添加目录项
    if (ext2_dir_add_entry(dir, dentry->name, ino, EXT2_FT_REG_FILE) < 0) {
        ext2_free_inode(sb, ino);
        return -1;
    }

    // 创建VFS inode
    inode = ext2_iget(sb, ino); // 假设有 ext2_iget 从磁盘读取并分配VFS inode
    if (!inode) {
        // 回滚目录项？需要删除刚添加的目录项，但比较复杂，暂时忽略
        ext2_free_inode(sb, ino);
        return -1;
    }

    // 关联到dentry
    dentry->inode = inode;

    // 更新父目录的修改时间（可选）
    //dir->i_mtime = get_seconds();
    // 父目录的inode也需要写回（修改时间变化）
    struct ext2_inode *dir_ei = (struct ext2_inode *)dir->private_data;
    // dir_ei->i_mtime = dir->i_mtime;
    ext2_write_inode(sb, dir->ino, dir_ei);

    return 0;
}

static int ext2_delete(struct inode *dir){
    struct super_block *sb = dir->sb;
    ext2_fs_info_t *fsi = sb->private_data;
    struct ext2_inode *ei = (struct ext2_inode *)dir->private_data;
    uint32_t block_size = fsi->block_size;
    uint32_t per_block = block_size / sizeof(uint32_t);
    int i;

    if (atomic_read(&dir->link_count) == 0){
            // 释放所有数据块（包括间接块）
        for (i = 0; i < EXT2_NDIR_BLOCKS; i++) {
            if (ei->i_block[i])
                ext2_free_block(sb, ei->i_block[i]);
        }
        // 释放一级间接块
        if (ei->i_block[EXT2_IND_BLOCK]) {
            // 释放一级间接块指向的所有数据块
            uint32_t *ind_buf = kmalloc(block_size);
            if (ind_buf) {
                if (ext2_rw_block(sb, ei->i_block[EXT2_IND_BLOCK], ind_buf,true) == 0) {
                    for (i = 0; i < (int)per_block; i++) {
                        if (ind_buf[i])
                            ext2_free_block(sb, ind_buf[i]);
                    }
                }
                kfree(ind_buf);
            }
            ext2_free_block(sb, ei->i_block[EXT2_IND_BLOCK]);
        }
        // 释放二级间接块
        if (ei->i_block[EXT2_DIND_BLOCK]) {
            uint32_t *dind_buf = kmalloc(block_size);
            if (dind_buf) {
                if (ext2_rw_block(sb, ei->i_block[EXT2_DIND_BLOCK], dind_buf,true) == 0) {
                    for (i = 0; i < (int)per_block; i++) {
                        if (dind_buf[i]) {
                            // 释放一级间接块
                            uint32_t *ind_buf2 = kmalloc(block_size);
                            if (ind_buf2) {
                                if (ext2_rw_block(sb, dind_buf[i], ind_buf2, true) == 0) {
                                    for (int j = 0; j < (int)per_block; j++) {
                                        if (ind_buf2[j])
                                            ext2_free_block(sb, ind_buf2[j]);
                                    }
                                }
                                kfree(ind_buf2);
                            }
                            ext2_free_block(sb, dind_buf[i]);
                        }
                    }
                }
                kfree(dind_buf);
            }
            ext2_free_block(sb, ei->i_block[EXT2_DIND_BLOCK]);
        }
        // 三级间接块类似，可省略

        // 释放 inode 号
        ext2_free_inode(sb, dir->ino);
    }

    // 释放内存中的 ext2_inode 副本（由 VFS 的 delete 负责，但我们已经将 private_data 交给 VFS，VFS 在 delete 后不会释放 private_data？实际上，VFS 在 delete 调用后，会继续释放 inode 结构本身，但 private_data 需要在这里释放，否则会泄漏。因为 VFS 不知道 private_data 是什么，所以 delete 方法需要负责释放它。所以我们在这里要 kfree(ei)。
    kfree(ei);
    dir->private_data = NULL;

    return 0;
}

static int ext2_lookup(struct inode *dir, struct dentry *dentry)
{
    ext2_fs_info_t *fsi = dir->sb->private_data;
    uint32_t block_size = fsi->block_size;
    uint32_t blocks = (dir->size + block_size - 1) / block_size; // 目录占用的总块数
    uint32_t block;
    uint8_t *buf = kmalloc(block_size);
    if (!buf) return -1;

    for (block = 0; block < blocks; block++) {
        uint32_t phys_block = ext2_bmap(dir, block);
        if (phys_block == 0) {
            // 空洞块（目录不应该有空洞），跳过或视为空
            continue;
        }
        if (ext2_rw_block(dir->sb, phys_block, buf,true) < 0) {
            kfree(buf);
            return -1;
        }

        struct ext2_dir_entry_2 *de = (struct ext2_dir_entry_2 *)buf;
        uint32_t pos = 0;
        while (pos < block_size) {
            if (de->inode == 0) {
                // 已删除项，跳过
                pos += de->rec_len;
                de = (struct ext2_dir_entry_2 *)((char *)de + de->rec_len);
                continue;
            }

            int name_len = de->name_len;
            // 检查是否匹配（注意：ext2文件名不以'\0'结尾，需比较长度和内容）
            if (name_len == (int)strlen(dentry->name) && !strncmp(de->name, dentry->name, name_len)) {
                // 找到
                inode_t *child_inode = ext2_iget(dir->sb, de->inode);
                if (!child_inode) {
                    kfree(buf);
                    return -1;
                }
                dentry->inode = child_inode;
                atomic_inc(&child_inode->refcount); // dentry引用inode
                kfree(buf);
                return 0;
            }
            pos += de->rec_len;
            de = (struct ext2_dir_entry_2 *)((char *)de + de->rec_len);
        }
    }
    kfree(buf);
    return -1;
}

static int ext2_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
    struct super_block *sb = dir->sb;
    ext2_fs_info_t *fsi = sb->private_data;
    uint32_t block_size = fsi->block_size;
    uint32_t ino;
    struct ext2_inode ei;
    inode_t *new_inode;
    int ret;

    // 1. 分配 inode 号
    ino = ext2_alloc_inode(sb);
    if (ino == (uint32_t)-1)
        return -1;

    // 2. 初始化磁盘 inode
    memset(&ei, 0, sizeof(ei));
    ei.i_mode = mode | S_IFDIR;
    ei.i_uid = 0;
    ei.i_gid = 0;
    ei.i_size = 0;
    ei.i_atime = ei.i_ctime = ei.i_mtime = 0;//get_seconds();
    ei.i_links_count = 2;               // "." + 父目录项
    ei.i_blocks = 0;
    ei.i_flags = 0;
    for (int i = 0; i < EXT2_N_BLOCKS; i++)
        ei.i_block[i] = 0;

    // 3. 创建内存 VFS inode（附带私有数据）
    new_inode = ext2_create_VFS_inode(sb, &ei, ino);
    if (!new_inode)
        goto out_inode;

    // 4. 分配第一个数据块（逻辑块 0）
    uint32_t phys_block = ext2_bmap_alloc(new_inode, 0);
    if (phys_block == (uint32_t)-1)
        goto out_vfs_inode;

    // 5. 初始化目录块
    uint8_t *block_buf = kmalloc(block_size);
    if (!block_buf)
        goto out_block_inode;
    memset(block_buf, 0, block_size);

    // 计算目录项对齐长度（固定头部 8 字节）
    #define DIR_ENTRY_BASE 8
    uint16_t rec_len_self = (DIR_ENTRY_BASE + 1 + 3) & ~3;   // "."
    uint16_t rec_len_dotdot = block_size - rec_len_self;     // 剩余全部给 ".."

    // 写入 "."
    struct ext2_dir_entry_2 *de = (struct ext2_dir_entry_2 *)block_buf;
    de->inode = ino;
    de->rec_len = rec_len_self;
    de->name_len = 1;
    de->file_type = EXT2_FT_DIR;
    de->name[0] = '.';

    // 写入 ".."
    struct ext2_dir_entry_2 *de2 = (struct ext2_dir_entry_2 *)(block_buf + rec_len_self);
    de2->inode = dir->ino;
    de2->rec_len = rec_len_dotdot;
    de2->name_len = 2;
    de2->file_type = EXT2_FT_DIR;
    de2->name[0] = '.';
    de2->name[1] = '.';

    if (ext2_rw_block(sb, phys_block, block_buf,false) < 0) {
        kfree(block_buf);
        goto out_block_inode;
    }
    kfree(block_buf);

    // 6. 更新新目录的 inode 信息
    new_inode->size = block_size;
    struct ext2_inode *new_ei = (struct ext2_inode *)new_inode->private_data;
    new_ei->i_size = block_size;
    new_ei->i_blocks = block_size / 512;   // 一个块（512 字节扇区计数）
    if (ext2_write_inode(sb, ino, new_ei) < 0)
        goto out_block_inode;

    // 7. 在父目录中添加目录项
    ret = ext2_dir_add_entry(dir, dentry->name, ino, EXT2_FT_DIR);
    if (ret < 0)
        goto out_block_inode;

    // 8. 更新父目录的元数据
    //dir->i_mtime = get_seconds();
    struct ext2_inode *dir_ei = (struct ext2_inode *)dir->private_data;
    //dir_ei->i_mtime = dir->i_mtime;
    dir_ei->i_links_count++;        // 父目录增加一个子目录，链接计数加 1
    ext2_write_inode(sb, dir->ino, dir_ei);

    // 9. 关联 dentry(ref count 初始化为1了)
    dentry->inode = new_inode;

    return 0;
out_block_inode:
    ext2_free_block(sb, phys_block);
out_vfs_inode:
    new_inode->inode_ops = NULL;    // 防止重入del 导致奇怪问题
    inode_put(new_inode);
out_inode:
    ext2_free_inode(sb, ino);
    return -1;
}

static int ext2_unlink(struct inode *dir, struct dentry *dentry)
{
    struct super_block *sb = dir->sb;
    ext2_fs_info_t *fsi = sb->private_data;
    uint32_t block_size = fsi->block_size;
    struct inode *inode = dentry->inode;
    struct ext2_inode *ei = (struct ext2_inode *)inode->private_data;
    uint32_t target_ino = inode->ino;

    if (S_ISDIR(inode->mode))
        return -1;

    uint32_t blocks = (dir->size + block_size - 1) / block_size;
    uint8_t *block_buf = kmalloc(block_size);
    if (!block_buf) return -1;

    for (uint32_t block_idx = 0; block_idx < blocks; block_idx++) {
        uint32_t phys_block = ext2_bmap(dir, block_idx);
        if (phys_block == 0) continue;
        if (ext2_rw_block(sb, phys_block, block_buf, true) < 0) {
            kfree(block_buf);
            return -1;
        }

        uint32_t offset = 0;
        struct ext2_dir_entry_2 *prev_phys = NULL;
        while (offset < block_size) {
            struct ext2_dir_entry_2 *de = (struct ext2_dir_entry_2 *)(block_buf + offset);
            if (de->inode == target_ino) {
                // 标记删除
                de->inode = 0;
                de->name_len = 0;

                // 尝试与前一项合并
                if (prev_phys) {
                    prev_phys->rec_len += de->rec_len;
                    // de 被合并，不再单独存在
                    uint32_t next_off = offset + de->rec_len;
                    if (next_off < block_size) {
                        struct ext2_dir_entry_2 *next_de = (struct ext2_dir_entry_2 *)(block_buf + next_off);
                        if (next_de->inode == 0) {
                            prev_phys->rec_len += next_de->rec_len;
                        }
                    }
                } else {
                    // 尝试与后一项合并
                    uint32_t next_off = offset + de->rec_len;
                    if (next_off < block_size) {
                        struct ext2_dir_entry_2 *next_de = (struct ext2_dir_entry_2 *)(block_buf + next_off);
                        if (next_de->inode == 0) {
                            de->rec_len += next_de->rec_len;
                        }
                    }
                }

                // 写回修改后的块
                if (ext2_rw_block(sb, phys_block, block_buf,false) < 0) {
                    kfree(block_buf);
                    return -1;
                }
                goto found;
            }
            prev_phys = de;
            offset += de->rec_len;
        }
    }
    kfree(block_buf);
    return -1;

found:
    kfree(block_buf);

    // 更新目标 inode 的链接计数
    atomic_dec(&inode->link_count);
    ei->i_links_count--;
    if (ext2_write_inode(sb, target_ino, ei) < 0) {
        return -1;
    }

    // 更新父目录的修改时间
    // dir->i_mtime = get_seconds();
    struct ext2_inode *dir_ei = (struct ext2_inode *)dir->private_data;
    // dir_ei->i_mtime = dir->i_mtime;
    ext2_write_inode(sb, dir->ino, dir_ei);

    return 0;
}

static int ext2_setattr(struct inode *inode, struct iattr *attr)
{
    if (!(attr->ia_valid & ATTR_SIZE))
        return 0;

    loff_t new_size = attr->ia_size;
    loff_t old_size = inode->size;

    if (new_size == old_size)
        return 0;

    if (new_size < old_size) {
        if (ext2_truncate_blocks(inode, new_size) < 0) {
            return -1;
        }
    }

    inode->size = new_size;

    // 更新磁盘 inode
    struct ext2_inode *ei = (struct ext2_inode *)inode->private_data;
    ei->i_size = new_size;
    ext2_write_inode(inode->sb, inode->ino, ei);

    return 0;
}


static ssize_t ext2_read(struct file *file, char __user *buf, size_t len, loff_t *ppos)
{
    inode_t *inode = file->inode;
    ext2_fs_info_t *fsi = inode->sb->private_data;
    uint32_t block_size = fsi->block_size;
    loff_t pos = *ppos;
    size_t count = len;
    ssize_t ret = 0;
    uint8_t *block_buf;

    if (pos >= (int64_t)inode->size)
        return 0;
    if (pos + count > inode->size)
        count = inode->size - pos;

    while (count > 0) {
        uint32_t logical_block = pos / block_size;
        uint32_t offset = pos % block_size;
        uint32_t to_read = block_size - offset;
        if (to_read > count)
            to_read = count;

        uint32_t phys_block = ext2_bmap(inode, logical_block);
        if (phys_block == (uint32_t)-1) {
            ret = -1;
            break;
        }

        block_buf = kmalloc(block_size);
        if (!block_buf) {
            ret = -1;
            break;
        }

        if (phys_block == 0) {
            // 空洞块，用零填充
            memset(block_buf, 0, block_size);
        } else {
            if (ext2_rw_block(inode->sb, phys_block, block_buf,true) < 0) {
                kfree(block_buf);
                ret = -1;
                break;
            }
        }

        // 将数据复制到用户缓冲区（这里简化使用 memcpy，实际需用 copy_to_user）
        copy_to_user(buf, block_buf + offset, to_read);

        kfree(block_buf);

        buf += to_read;
        pos += to_read;
        count -= to_read;
        ret += to_read;
    }

    *ppos = pos;
    return ret;
}

static ssize_t ext2_write(struct file *file, const char __user *buf, size_t len, loff_t *ppos)
{
    inode_t *inode = file->inode;
    ext2_fs_info_t *fsi = inode->sb->private_data;
    uint32_t block_size = fsi->block_size;
    loff_t pos = *ppos;
    size_t count = len;
    ssize_t ret = 0;
    uint8_t *block_buf;

    while (count > 0) {
        uint32_t logical_block = pos / block_size;
        uint32_t offset = pos % block_size;
        uint32_t to_write = block_size - offset;
        if (to_write > count)
            to_write = count;

        /* 获取或分配物理块 */
        uint32_t phys_block = ext2_bmap_alloc(inode, logical_block);
        if (phys_block == (uint32_t)-1) {
            ret = -1;
            break;
        }

        block_buf = kmalloc(block_size);
        if (!block_buf) {
            ret = -1;
            break;
        }

        /* 如果写入不覆盖整个块，需要先读取原块 */
        if (to_write != block_size) {
            if (ext2_rw_block(inode->sb, phys_block, block_buf,true) < 0) {
                kfree(block_buf);
                ret = -1;
                break;
            }
        }

        /* 复制用户数据到缓冲区（此处假设 buf 在内核空间，实际需用 copy_from_user） */
        memcpy(block_buf + offset, (void *)buf, to_write);

        /* 写回磁盘 */
        if (ext2_rw_block(inode->sb, phys_block, block_buf, false) < 0) {
            kfree(block_buf);
            ret = -1;
            break;
        }

        kfree(block_buf);

        buf += to_write;
        pos += to_write;
        count -= to_write;
        ret += to_write;
    }

    if (ret > 0) {
        /* 更新文件大小 */
        if (pos > (loff_t)inode->size) {
            inode->size = pos;
        }
        /* 更新 inode 的磁盘副本（包括大小、块指针等） */
        struct ext2_inode *ei = (struct ext2_inode *)inode->private_data;
        ei->i_size = inode->size;
        /* 更新修改时间等（此处省略时间戳） */
        if (ext2_write_inode(inode->sb, inode->ino, ei) < 0) {
            /* 写回失败，但数据可能已部分写入，返回已写入长度？这里简单返回错误 */
            ret = -1;
        }
    }

    if (ret > 0)
        *ppos = pos;
    return ret;
}

static int ext2_open(UNUSED struct inode *inode,UNUSED struct file *file){
    return 0;
}

static int ext2_readdir(struct file *file, struct dirent __user *dirp, unsigned int count)
{
    inode_t *inode = file->inode;
    ext2_fs_info_t *fsi = inode->sb->private_data;
    uint32_t block_size = fsi->block_size;
    uint32_t blocks = (inode->size + block_size - 1) / block_size;
    uint32_t target = (uint32_t)file->pos;  // 要读取的目录项序号
    uint32_t cur = 0;                        // 当前已扫描的有效目录项计数
    uint32_t block_idx;
    uint8_t *buf = kmalloc(block_size);
    if (!buf) return -1;

    for (block_idx = 0; block_idx < blocks; block_idx++) {
        uint32_t phys = ext2_bmap(inode, block_idx);
        if (phys == 0) {
            // 空洞块，跳过（目录不应有空洞）
            continue;
        }
        if (ext2_rw_block(inode->sb, phys, buf,true) < 0) {
            kfree(buf);
            return -1;
        }
        uint32_t offset = 0;
        while (offset < block_size) {
            struct ext2_dir_entry_2 *de = (struct ext2_dir_entry_2 *)(buf + offset);
            if (de->inode != 0) {
                if (cur == target) {
                    uint16_t name_len = de->name_len;
                    uint16_t reclen = sizeof(struct dirent) + name_len + 1;
                    if (reclen > count) {
                        kfree(buf);
                        return -1;
                    }
                
                    // 先复制固定头部（d_ino, d_reclen, d_type）
                    struct dirent hdr;
                    hdr.d_ino = de->inode;
                    hdr.d_reclen = reclen;
                    switch (de->file_type) {
                        case EXT2_FT_REG_FILE: hdr.d_type = DT_REG; break;
                        case EXT2_FT_DIR:      hdr.d_type = DT_DIR; break;
                        case EXT2_FT_CHRDEV:   hdr.d_type = DT_CHR; break;
                        case EXT2_FT_BLKDEV:   hdr.d_type = DT_BLK; break;
                        case EXT2_FT_FIFO:     hdr.d_type = DT_FIFO; break;
                        case EXT2_FT_SOCK:     hdr.d_type = DT_SOCK; break;
                        case EXT2_FT_SYMLINK:  hdr.d_type = DT_LNK; break;
                        default:                hdr.d_type = DT_UNKNOWN;
                    }
                    if (copy_to_user(dirp, &hdr, sizeof(struct dirent)))
                        goto copy_fail;
                
                    // 再复制文件名到用户空间（d_name 区域）
                    char __user *name_ptr = dirp->d_name;
                    if (copy_to_user(name_ptr, de->name, name_len))
                        goto copy_fail;
                    if (put_user(0, name_ptr + name_len))
                        goto copy_fail;
                
                    file->pos = target + 1;
                    kfree(buf);
                    return 0;
                
                copy_fail:
                    kfree(buf);
                    return -1;
                }
                cur++;
            }
            offset += de->rec_len;
        }
    }
    kfree(buf);
    return -1;  // 无更多目录项
}

static int ext2_release(UNUSED struct inode *inode, UNUSED struct file *file){
    return 0;
}

static int ext2_fsync(UNUSED struct file *file){
    return 0;
}


static inline void ext2_set_block_all_zero(super_block_t *sb,uint32_t new_block,uint32_t block_size){
    char *temp = kmalloc(block_size);
    memset(temp,0,block_size);
    ext2_rw_block(sb,new_block,temp,false);
    kfree(temp);
}

static void ext2_free_ind_blocks(struct super_block *sb, uint32_t block, int level)
{
    ext2_fs_info_t *fsi = sb->private_data;
    uint32_t block_size = fsi->block_size;
    uint32_t per_block = block_size / sizeof(uint32_t);
    uint32_t *buf;
    uint32_t i;

    if (block == 0) return;
    if (level == 0) {
        ext2_free_block(sb, block);
        return;
    }
    buf = kmalloc(block_size);
    if (!buf) return;
    if (ext2_rw_block(sb, block, buf,true) < 0) {
        kfree(buf);
        return;
    }
    for (i = 0; i < per_block; i++) {
        if (buf[i])
            ext2_free_ind_blocks(sb, buf[i], level - 1);
    }
    ext2_free_block(sb, block);
    kfree(buf);
}

static int ext2_truncate_blocks(struct inode *inode, loff_t new_size)
{
    ext2_fs_info_t *fsi = inode->sb->private_data;
    struct ext2_inode *ei = (struct ext2_inode *)inode->private_data;
    uint32_t block_size = fsi->block_size;
    uint32_t per_block = block_size / sizeof(uint32_t);
    uint32_t new_blocks = (new_size + block_size - 1) / block_size;
    uint32_t old_blocks = (inode->size + block_size - 1) / block_size;
    int i;

    // 释放直接块
    for (i = new_blocks; i < EXT2_NDIR_BLOCKS && (uint32_t)i < old_blocks; i++) {
        if (ei->i_block[i]) {
            ext2_free_block(inode->sb, ei->i_block[i]);
            ei->i_block[i] = 0;
        }
    }

    // 处理一级间接块
    if (new_blocks <= EXT2_NDIR_BLOCKS) {
        if (ei->i_block[EXT2_IND_BLOCK]) {
            ext2_free_ind_blocks(inode->sb, ei->i_block[EXT2_IND_BLOCK], 1);
            ei->i_block[EXT2_IND_BLOCK] = 0;
        }
    } else {
        uint32_t ind_start = new_blocks - EXT2_NDIR_BLOCKS;
        if (ei->i_block[EXT2_IND_BLOCK]) {
            uint32_t ind_block = ei->i_block[EXT2_IND_BLOCK];
            uint32_t *ind_buf = kmalloc(block_size);
            if (!ind_buf) return -1;
            if (ext2_rw_block(inode->sb, ind_block, ind_buf,true) < 0) {
                kfree(ind_buf);
                return -1;
            }
            for (i = ind_start; (uint32_t)i < per_block; i++) {
                if (ind_buf[i]) {
                    ext2_free_block(inode->sb, ind_buf[i]);
                    ind_buf[i] = 0;
                }
            }
            ext2_rw_block(inode->sb, ind_block, ind_buf,false);
            kfree(ind_buf);
        }
    }

    // 处理二级间接块
    if (new_blocks <= EXT2_NDIR_BLOCKS + per_block) {
        if (ei->i_block[EXT2_DIND_BLOCK]) {
            ext2_free_ind_blocks(inode->sb, ei->i_block[EXT2_DIND_BLOCK], 2);
            ei->i_block[EXT2_DIND_BLOCK] = 0;
        }
    } else {
        uint32_t dind_start = new_blocks - (EXT2_NDIR_BLOCKS + per_block);
        if (ei->i_block[EXT2_DIND_BLOCK]) {
            uint32_t dind_block = ei->i_block[EXT2_DIND_BLOCK];
            uint32_t *dind_buf = kmalloc(block_size);
            if (!dind_buf) return -1;
            if (ext2_rw_block(inode->sb, dind_block, dind_buf,true) < 0) {
                kfree(dind_buf);
                return -1;
            }
            for (i = dind_start / per_block; (uint32_t)i < per_block; i++) {
                if (dind_buf[i]) {
                    if ((uint32_t)i == dind_start / per_block) {
                        // 部分释放这个二级间接块指向的一级间接块
                        uint32_t sub_start = dind_start % per_block;
                        uint32_t sub_block = dind_buf[i];
                        uint32_t *sub_buf = kmalloc(block_size);
                        if (!sub_buf) {
                            kfree(dind_buf);
                            return -1;
                        }
                        if (ext2_rw_block(inode->sb, sub_block, sub_buf,true) < 0) {
                            kfree(sub_buf);
                            kfree(dind_buf);
                            return -1;
                        }
                        for (uint32_t j = sub_start; j < per_block; j++) {
                            if (sub_buf[j]) {
                                ext2_free_block(inode->sb, sub_buf[j]);
                                sub_buf[j] = 0;
                            }
                        }
                        ext2_rw_block(inode->sb, sub_block, sub_buf,false);
                        kfree(sub_buf);
                    } else {
                        // 释放整个一级间接块及其所有数据块
                        ext2_free_ind_blocks(inode->sb, dind_buf[i], 1);
                        dind_buf[i] = 0;
                    }
                }
            }
            ext2_rw_block(inode->sb, dind_block, dind_buf,false);
            kfree(dind_buf);
        }
    }

    // 处理三级间接块（类似，可省略）
    return 0;
}

static int ext2_bmap_alloc(struct inode *inode, uint32_t logical_block)
{
    ext2_fs_info_t *fsi = inode->sb->private_data;
    struct ext2_inode *ei = (struct ext2_inode *)inode->private_data;
    uint32_t block_size = fsi->block_size;
    uint32_t per_block = block_size / sizeof(uint32_t);
    uint32_t ind_blocks = per_block;
    uint32_t dind_blocks = per_block * per_block;
    uint32_t *ind_buf = NULL;
    uint32_t *dind_buf = NULL;
    uint32_t new_block;
    int ret;

    /* 直接块 */
    if (logical_block < EXT2_NDIR_BLOCKS) {
        if (ei->i_block[logical_block] == 0) {
            new_block = ext2_alloc_block(inode->sb);
            if (new_block == (uint32_t)-1) return -1;
            ei->i_block[logical_block] = new_block;
        }
        return ei->i_block[logical_block];
    }

    /* 一级间接块 */
    if (logical_block < EXT2_NDIR_BLOCKS + ind_blocks) {
        uint32_t index = logical_block - EXT2_NDIR_BLOCKS;
        if (ei->i_block[EXT2_IND_BLOCK] == 0) {
            new_block = ext2_alloc_block(inode->sb);
            if (new_block == (uint32_t)-1) return -1;
            ei->i_block[EXT2_IND_BLOCK] = new_block;
            ext2_set_block_all_zero(inode->sb,new_block,block_size);
        }
        ind_buf = kmalloc(block_size);
        if (!ind_buf) return -1;
        /* 读取间接块（可能为空，需要初始化） */
        if (ext2_rw_block(inode->sb, ei->i_block[EXT2_IND_BLOCK], ind_buf,true) < 0) {
            kfree(ind_buf);
            return -1;
        }
        if (ind_buf[index] == 0) {
            new_block = ext2_alloc_block(inode->sb);
            if (new_block == (uint32_t)-1) {
                kfree(ind_buf);
                return -1;
            }
            ind_buf[index] = new_block;
            ext2_rw_block(inode->sb, ei->i_block[EXT2_IND_BLOCK], ind_buf,false);
        }
        ret = ind_buf[index];
        kfree(ind_buf);
        return ret;
    }

    /* 二级间接块（三级类似，这里省略以保持简洁） */
    if (logical_block < EXT2_NDIR_BLOCKS + ind_blocks + dind_blocks) {
        uint32_t offset = logical_block - (EXT2_NDIR_BLOCKS + ind_blocks);
        uint32_t dind_idx = offset / per_block;
        uint32_t ind_idx = offset % per_block;
        if (ei->i_block[EXT2_DIND_BLOCK] == 0) {
            new_block = ext2_alloc_block(inode->sb);
            if (new_block == (uint32_t)-1) return -1;
            ei->i_block[EXT2_DIND_BLOCK] = new_block;
            ext2_set_block_all_zero(inode->sb,new_block,block_size);
        }
        /* 读二级间接块 */
        dind_buf = kmalloc(block_size);
        if (!dind_buf) return -1;
        if (ext2_rw_block(inode->sb, ei->i_block[EXT2_DIND_BLOCK], dind_buf,true) < 0) {
            kfree(dind_buf);
            return -1;
        }
        if (dind_buf[dind_idx] == 0) {
            new_block = ext2_alloc_block(inode->sb);
            if (new_block == (uint32_t)-1) {
                kfree(dind_buf);
                return -1;
            }
            ext2_set_block_all_zero(inode->sb,new_block,block_size);
            dind_buf[dind_idx] = new_block;
            ext2_rw_block(inode->sb, ei->i_block[EXT2_DIND_BLOCK], dind_buf,false);
        }
        uint32_t ind_block = dind_buf[dind_idx];
        kfree(dind_buf);

        ind_buf = kmalloc(block_size);
        if (!ind_buf) return -1;
        if (ext2_rw_block(inode->sb, ind_block, ind_buf,true) < 0) {
            kfree(ind_buf);
            return -1;
        }
        if (ind_buf[ind_idx] == 0) {
            new_block = ext2_alloc_block(inode->sb);
            if (new_block == (uint32_t)-1) {
                kfree(ind_buf);
                return -1;
            }
            ind_buf[ind_idx] = new_block;
            ext2_rw_block(inode->sb, ind_block, ind_buf,false);
        }
        ret = ind_buf[ind_idx];
        kfree(ind_buf);
        return ret;
    }

    /* 超出范围 */
    return -1;
}

static int ext2_write_inode(struct super_block *sb, uint32_t ino, struct ext2_inode *ei)
{
    ext2_fs_info_t *fsi = sb->private_data;
    uint32_t block_size = fsi->block_size;
    uint32_t inode_size = fsi->inode_size;
    uint32_t group = (ino - 1) / fsi->es.s_inodes_per_group;
    uint32_t index = (ino - 1) % fsi->es.s_inodes_per_group;

    /* 获取块组描述符 */
    ext2_group_desc_cache_t *gd = &fsi->group_descs[group];
    uint32_t inode_table_start = gd->bg_inode_table;
    uint32_t inodes_per_block = block_size / inode_size;
    uint32_t inode_block = inode_table_start + index / inodes_per_block;
    uint32_t inode_offset = (index % inodes_per_block) * inode_size;

    uint8_t *block_buf = kmalloc(block_size);
    if (!block_buf) return -1;
    if (ext2_rw_block(sb, inode_block, block_buf, true) < 0) {
        kfree(block_buf);
        return -1;
    }
    memcpy(block_buf + inode_offset, ei, sizeof(struct ext2_inode));
    if (ext2_rw_block(sb, inode_block, block_buf, false) < 0) {
        kfree(block_buf);
        return -1;
    }
    kfree(block_buf);
    return 0;
}

static inline uint16_t EXT2_DIR_REC_LEN(uint16_t name_len)
{
    uint16_t len = offsetof(struct ext2_dir_entry_2, name) + name_len + 1; // +1 for null? ext2不存储null，但为了对齐计算
    // ext2 实际存储没有null，但目录项长度需要4字节对齐
    len = (len + 3) & ~3;
    return len;
}

static int ext2_dir_add_entry(struct inode *dir, const char *name, uint32_t ino, uint8_t file_type)
{
    ext2_fs_info_t *fsi = dir->sb->private_data;
    uint32_t block_size = fsi->block_size;
    uint32_t name_len = strlen(name);
    uint16_t rec_len = EXT2_DIR_REC_LEN(name_len);  // 计算对齐后的长度
    uint8_t *block_buf = NULL;
    uint32_t blocks = (dir->size + block_size - 1) / block_size;
    uint32_t block_idx;
    uint32_t phys_block;

    // 先遍历已有块，尝试在末尾添加
    for (block_idx = 0; block_idx < blocks; block_idx++) {
        phys_block = ext2_bmap(dir, block_idx);
        if (phys_block == 0) continue; // 空洞块，跳过
        block_buf = kmalloc(block_size);
        if (!block_buf) return -1;
        if (ext2_rw_block(dir->sb, phys_block, block_buf,true) < 0) {
            kfree(block_buf);
            return -1;
        }

        uint32_t offset = 0;
        while (offset < block_size) {
            struct ext2_dir_entry_2 *de = (struct ext2_dir_entry_2 *)(block_buf + offset);
            uint16_t min_len = EXT2_DIR_REC_LEN(de->name_len); // 该目录项实际需要的最小长度
            uint16_t free_space = de->rec_len - min_len;       // 内部空闲空间

            // 如果该目录项已被删除 (inode == 0)，整个项都是空闲的
            if (de->inode == 0) {
                if (de->rec_len >= rec_len) {
                    // 可以直接重用整个项
                    de->inode = ino;
                    de->name_len = name_len;
                    de->file_type = file_type;
                    memcpy(de->name, (void*)name, name_len);
                    if (ext2_rw_block(dir->sb, phys_block, block_buf,false) < 0) {
                        kfree(block_buf);
                        return -1;
                    }
                    kfree(block_buf);
                    return 0;
                }
            } else {
                if (free_space >= rec_len) {
                    // 在当前目录项内部插入新项
                    // 分裂：将当前项的 rec_len 减少，在尾部创建新项
                    uint16_t new_rec_len = min_len; // 当前项缩小到实际大小
                    // 新项放在当前项之后
                    struct ext2_dir_entry_2 *new_de = (struct ext2_dir_entry_2 *)((char *)de + new_rec_len);
                    new_de->inode = ino;
                    new_de->rec_len = free_space; // 新项占用剩余空间
                    new_de->name_len = name_len;
                    new_de->file_type = file_type;
                    memcpy(new_de->name, (void*)name, name_len);
                    // 修改当前项的 rec_len
                    de->rec_len = new_rec_len;
                    if (ext2_rw_block(dir->sb, phys_block, block_buf,false) < 0) {
                        kfree(block_buf);
                        return -1;
                    }
                    kfree(block_buf);
                    return 0;
                }
            }
            offset += de->rec_len;
        }
        kfree(block_buf);
    }
    // 所有现有块都不够空间，分配新块
    block_buf = kmalloc(block_size);
    if (!block_buf) {
        return -1;
    }
    uint32_t new_block_idx = blocks;  // 新的逻辑块号
    uint32_t phys_new_block = ext2_bmap_alloc(dir, new_block_idx);
    if (phys_new_block == (uint32_t)-1){
        kfree(block_buf);
        return -1;
    }
    memset(block_buf, 0, block_size);
    struct ext2_dir_entry_2 *new_de = (struct ext2_dir_entry_2 *)block_buf;
    new_de->inode = ino;
    new_de->rec_len = block_size;
    new_de->name_len = name_len;
    new_de->file_type = file_type;
    memcpy(new_de->name, (void*)name, name_len);

    if (ext2_rw_block(dir->sb, phys_new_block, block_buf,false) < 0) {
        kfree(block_buf);
        return -1;
    }
    kfree(block_buf);

    // 更新目录大小
    dir->size += block_size;
    struct ext2_inode *dir_ei = (struct ext2_inode *)dir->private_data;
    dir_ei->i_size = dir->size;
    ext2_write_inode(dir->sb, dir->ino, dir_ei);

    return 0;
}

static inode_t *ext2_iget(struct super_block *sb, uint32_t ino)
{
    struct ext2_inode ei;
    inode_t *inode;

    if (ext2_read_inode(sb, ino, &ei) < 0)
        return NULL;

    inode = ext2_create_VFS_inode(sb, &ei, ino);
    if (!inode) return NULL;

    // 根据文件类型设置操作表（后续可完善）
    inode->inode_ops = &ext2_inode_ops;
    inode->default_file_ops = &ext2_file_ops;
    return inode;
}

static int ext2_bmap(struct inode *inode, uint32_t logical_block)
{
    ext2_fs_info_t *fsi = inode->sb->private_data;
    struct ext2_inode *ei = (struct ext2_inode *)inode->private_data;
    uint32_t block_size = fsi->block_size;
    uint32_t per_block = block_size / sizeof(uint32_t);  // 每块可存块指针数
    uint32_t ind_blocks = per_block;                      // 一级间接块数
    uint32_t dind_blocks = per_block * per_block;         // 二级间接块数
    UNUSED uint32_t tind_blocks = per_block * per_block * per_block; // 三级间接块数

    uint32_t *ind_buf = NULL;
    int ret = 0;

    // 直接块
    if (logical_block < EXT2_NDIR_BLOCKS) {
        return ei->i_block[logical_block];
    }

    // 一级间接块
    if (logical_block < EXT2_NDIR_BLOCKS + ind_blocks) {
        uint32_t index = logical_block - EXT2_NDIR_BLOCKS;
        if (ei->i_block[EXT2_IND_BLOCK] == 0)
            return 0;  // 空洞
        ind_buf = kmalloc(block_size);
        if (!ind_buf) return -1;
        if (ext2_rw_block(inode->sb, ei->i_block[EXT2_IND_BLOCK], ind_buf, true) < 0) {
            kfree(ind_buf);
            return -1;
        }
        ret = ind_buf[index];
        kfree(ind_buf);
        return ret;
    }

    // 二级间接块
    if (logical_block < EXT2_NDIR_BLOCKS + ind_blocks + dind_blocks) {
        uint32_t offset = logical_block - (EXT2_NDIR_BLOCKS + ind_blocks);
        uint32_t dind_idx = offset / per_block;       // 一级间接块索引
        uint32_t ind_idx = offset % per_block;        // 二级间接块内的索引
        if (ei->i_block[EXT2_DIND_BLOCK] == 0)
            return 0;
        // 读二级间接块
        ind_buf = kmalloc(block_size);
        if (!ind_buf) return -1;
        if (ext2_rw_block(inode->sb, ei->i_block[EXT2_DIND_BLOCK], ind_buf, true) < 0) {
            kfree(ind_buf);
            return -1;
        }
        uint32_t ind_block = ind_buf[dind_idx];
        kfree(ind_buf);
        if (ind_block == 0)
            return 0;
        // 读一级间接块
        ind_buf = kmalloc(block_size);
        if (!ind_buf) return -1;
        if (ext2_rw_block(inode->sb, ind_block, ind_buf, true) < 0) {
            kfree(ind_buf);
            return -1;
        }
        ret = ind_buf[ind_idx];
        kfree(ind_buf);
        return ret;
    }

    // 三级间接块（如果需要，类似处理）
    // 这里省略三级间接，因为小文件系统可能用不到

    return -1;  // 超出范围
}

static inode_t *ext2_create_VFS_inode(struct super_block *sb, struct ext2_inode *ei,uint64_t ino)
{
    inode_t *inode = kmalloc(sizeof(inode_t));
    if (!inode) return NULL;
    ext2_inode_t *ext2inode = kmalloc(sizeof(ext2_inode_t));
    if (!ext2inode) {
        kfree(inode);
        return NULL;
    }
    memset(inode, 0, sizeof(inode_t));
    inode->ino = ino;
    inode->mode = ei->i_mode;
    inode->size = ei->i_size;
    inode->sb = sb;
    atomic_set(&inode->refcount, 1);
    if (S_ISDIR(ei->i_mode)) {
        atomic_set(&inode->link_count, 1);   // 目录：VFS 中固定为 1
    } else {
        atomic_set(&inode->link_count, ei->i_links_count);
    }
    inode->inode_ops = &ext2_inode_ops;
    inode->default_file_ops = &ext2_file_ops;
    inode->private_data = ext2inode;
    rwlock_init(&inode->i_meta_lock);
    mutex_init(&inode->i_data_lock);
    INIT_LIST_HEAD(&inode->lru_node);
    memcpy(ext2inode,ei,sizeof(ext2_inode_t));
    return inode;
}

static int ext2_read_inode(struct super_block *sb, uint32_t ino, struct ext2_inode *ei)
{
    struct ext2_super_block *es = (struct ext2_super_block *)sb->private_data;
    if (!es) return -1;

    uint32_t block_size = 1024 << es->s_log_block_size;
    uint32_t inodes_per_group = es->s_inodes_per_group;
    uint32_t group = (ino - 1) / inodes_per_group;
    uint32_t index = (ino - 1) % inodes_per_group;

    // 块组描述符起始块
    uint32_t first_desc_block = (block_size == 1024) ? 2 : 1;
    uint32_t descs_per_block = block_size / sizeof(struct ext2_group_desc);
    uint32_t desc_block = first_desc_block + group / descs_per_block;
    uint32_t desc_offset = (group % descs_per_block) * sizeof(struct ext2_group_desc);

    // 读取块组描述符块
    uint8_t *block_buf = kmalloc(block_size);
    if (!block_buf) return -1;
    if (ext2_rw_block(sb, desc_block, block_buf,true) < 0) {
        kfree(block_buf);
        return -1;
    }
    struct ext2_group_desc *desc = (struct ext2_group_desc *)(block_buf + desc_offset);
    uint32_t inode_table_start = desc->bg_inode_table;

    // 计算inode所在块
    uint32_t inode_size = es->s_inode_size ? es->s_inode_size : 128;
    uint32_t inodes_per_block = block_size / inode_size;
    uint32_t inode_block = inode_table_start + index / inodes_per_block;
    uint32_t inode_offset = (index % inodes_per_block) * inode_size;

    // 读取inode块
    uint8_t *inode_buf = kmalloc(block_size);
    if (!inode_buf) {
        kfree(block_buf);
        return -1;
    }
    if (ext2_rw_block(sb, inode_block, inode_buf,true) < 0) {
        kfree(block_buf);
        kfree(inode_buf);
        return -1;
    }
    memcpy(ei, inode_buf + inode_offset, sizeof(struct ext2_inode));

    kfree(block_buf);
    kfree(inode_buf);
    return 0;
}

static int ext2_rw_block(struct super_block *sb, uint32_t block_no, void *buf,bool read)
{
    partition_t *part = sb->part;
    if (!part) return -1;
    block_device_t *dev = &part->device;
    uint32_t ext2_block_size = 1024;  // 默认，读取超级块前使用
    if (sb->private_data) {
        struct ext2_super_block *es = (struct ext2_super_block *)sb->private_data;
        ext2_block_size = 1024 << es->s_log_block_size;
    }
    uint32_t dev_block_size = dev->block_size;
    uint32_t sectors_per_block = ext2_block_size / dev_block_size;
    uint64_t lba = block_no * sectors_per_block;
    if (read)
        return dev->read(dev, lba, sectors_per_block, buf);
    else
        return dev->write(dev, lba, sectors_per_block, buf);
}

static int ext2_read_super(struct super_block *sb, struct ext2_super_block *es)
{
    uint8_t buffer[1024];
    int ret;

    // 尝试从块1读取（偏移1024字节）
    ret = ext2_rw_block(sb, 1, buffer,true);
    if (ret < 0) return -1;
    memcpy(es, buffer, sizeof(struct ext2_super_block));
    if (es->s_magic == EXT2_SUPER_MAGIC)
        return 0;

    return -1;
}

static int ext2_load_group_descs(struct super_block *sb)
{
    ext2_fs_info_t *fsi = sb->private_data;
    struct ext2_super_block *es = &fsi->es;
    uint32_t block_size = fsi->block_size;
    //uint32_t desc_count = EXT2_DESC_PER_BLOCK((ext2_super_block_t*)sb->private_data);  // 每块能放多少个描述符
    uint32_t total_groups = (es->s_blocks_count + es->s_blocks_per_group - 1) / es->s_blocks_per_group;
    fsi->group_count = total_groups;
    fsi->group_descs = kmalloc(total_groups * sizeof(ext2_group_desc_cache_t));
    if (!fsi->group_descs) return -1;

    // 块组描述符起始块号（块大小为1024时从块2开始，否则从块1开始）
    uint32_t first_desc_block = (block_size == 1024) ? 2 : 1;
    uint32_t descs_per_block = block_size / sizeof(struct ext2_group_desc);
    uint8_t *block_buf = kmalloc(block_size);
    if (!block_buf) {
        kfree(fsi->group_descs);
        return -1;
    }

    for (uint32_t i = 0; i < total_groups; i++) {
        uint32_t block_idx = first_desc_block + i / descs_per_block;
        uint32_t offset = (i % descs_per_block) * sizeof(struct ext2_group_desc);
        if (i % descs_per_block == 0) {
            // 读取包含描述符的块
            if (ext2_rw_block(sb, block_idx, block_buf,true) < 0) {
                kfree(block_buf);
                kfree(fsi->group_descs);
                return -1;
            }
        }
        struct ext2_group_desc *desc = (struct ext2_group_desc *)(block_buf + offset);
        fsi->group_descs[i].bg_block_bitmap = desc->bg_block_bitmap;
        fsi->group_descs[i].bg_inode_bitmap = desc->bg_inode_bitmap;
        fsi->group_descs[i].bg_inode_table = desc->bg_inode_table;
        fsi->group_descs[i].bg_free_blocks_count = desc->bg_free_blocks_count;
        fsi->group_descs[i].bg_free_inodes_count = desc->bg_free_inodes_count;
        fsi->group_descs[i].bg_used_dirs_count = desc->bg_used_dirs_count;
        mutex_init(&fsi->group_descs[i].block_lock);
        mutex_init(&fsi->group_descs[i].inode_lock);
        // 加载块位图
        uint8_t *block_bitmap = kmalloc(fsi->block_size);
        if (!block_bitmap) {
            // 清理已加载的位图
            for (uint32_t j = 0; j < i; j++) {
                if (fsi->group_descs[j].block_bitmap)
                    kfree(fsi->group_descs[j].block_bitmap);
            }
            kfree(block_buf);
            kfree(fsi->group_descs);
            return -1;
        }
        if (ext2_rw_block(sb, desc->bg_block_bitmap, block_bitmap,true) < 0) {
            kfree(block_bitmap);
            for (uint32_t j = 0; j < i; j++) {
                if (fsi->group_descs[j].block_bitmap)
                    kfree(fsi->group_descs[j].block_bitmap);
            }
            kfree(block_buf);
            kfree(fsi->group_descs);
            return -1;
        }
        fsi->group_descs[i].block_bitmap = block_bitmap;
        // 加载inode位图
        uint8_t *inode_bitmap = kmalloc(fsi->block_size);
        if (!inode_bitmap) {
            kfree(block_bitmap); // 释放刚分配的块位图
            for (uint32_t j = 0; j < i; j++) {
                if (fsi->group_descs[j].block_bitmap)
                    kfree(fsi->group_descs[j].block_bitmap);
                if (fsi->group_descs[j].inode_bitmap)
                    kfree(fsi->group_descs[j].inode_bitmap);
            }
            kfree(block_buf);
            kfree(fsi->group_descs);
            return -1;
        }
        if (ext2_rw_block(sb, desc->bg_inode_bitmap, inode_bitmap,true) < 0) {
            kfree(inode_bitmap);
            kfree(block_bitmap);
            for (uint32_t j = 0; j < i; j++) {
                if (fsi->group_descs[j].block_bitmap)
                    kfree(fsi->group_descs[j].block_bitmap);
                if (fsi->group_descs[j].inode_bitmap)
                    kfree(fsi->group_descs[j].inode_bitmap);
            }
            kfree(block_buf);
            kfree(fsi->group_descs);
            return -1;
        }
        fsi->group_descs[i].inode_bitmap = inode_bitmap;
    }
    kfree(block_buf);
    return 0;
}

static int ext2_alloc_block(struct super_block *sb)
{
    ext2_fs_info_t *fsi = sb->private_data;
    for (uint32_t g = 0; g < fsi->group_count; g++) {
        ext2_group_desc_cache_t *gd = &fsi->group_descs[g];
        if (gd->bg_free_blocks_count == 0)
            continue;

        mutex_lock(&gd->block_lock);
        if (gd->bg_free_blocks_count == 0) {  // 双重检查
            mutex_unlock(&gd->block_lock);
            continue;
        }
        uint8_t *bitmap = gd->block_bitmap;
        if (!bitmap) {
            mutex_unlock(&gd->block_lock);
            continue;
        }
        for (uint32_t i = 0; i < fsi->block_size * 8; i++) {
            if (!(bitmap[i/8] & (1 << (i%8)))) {
                bitmap[i/8] |= (1 << (i%8));
                gd->bg_free_blocks_count--;
                fsi->es.s_free_blocks_count--;   // 更新超级块
                uint32_t bitmap_block = gd->bg_block_bitmap;
                ext2_rw_block(sb, bitmap_block, bitmap,false);
                uint32_t block = g * fsi->es.s_blocks_per_group + i;
                mutex_unlock(&gd->block_lock);
                return block;
            }
        }
        mutex_unlock(&gd->block_lock);
    }
    return -1;
}

static void ext2_free_block(struct super_block *sb, uint32_t block)
{
    ext2_fs_info_t *fsi = sb->private_data;
    uint32_t group = block / fsi->es.s_blocks_per_group;
    uint32_t index = block % fsi->es.s_blocks_per_group;
    if (group >= fsi->group_count)
        return;
    ext2_group_desc_cache_t *gd = &fsi->group_descs[group];
    mutex_lock(&gd->block_lock);
    uint8_t *bitmap = gd->block_bitmap;
    if (bitmap && (bitmap[index/8] & (1 << (index%8)))) {
        bitmap[index/8] &= ~(1 << (index%8));
        gd->bg_free_blocks_count++;
        fsi->es.s_free_blocks_count++; 
        uint32_t bitmap_block = gd->bg_block_bitmap;
        ext2_rw_block(sb, bitmap_block, bitmap,false);
    }
    mutex_unlock(&gd->block_lock);
}

static int ext2_alloc_inode(struct super_block *sb)
{
    ext2_fs_info_t *fsi = sb->private_data;
    int ret = -1;

    for (uint32_t g = 0; g < fsi->group_count; g++) {
        ext2_group_desc_cache_t *gd = &fsi->group_descs[g];
        if (gd->bg_free_inodes_count == 0)
            continue;

        mutex_lock(&gd->inode_lock);
        if (gd->bg_free_inodes_count == 0) {
            mutex_unlock(&gd->inode_lock);
            continue;
        }

        uint8_t *bitmap = gd->inode_bitmap;
        if (!bitmap) {
            mutex_unlock(&gd->inode_lock);
            continue;
        }
        for (uint32_t i = 0; i < fsi->block_size * 8; i++) {
            if (!(bitmap[i/8] & (1 << (i%8)))) {
                bitmap[i/8] |= (1 << (i%8));
                gd->bg_free_inodes_count--;

                uint32_t bitmap_block = gd->bg_inode_bitmap;
                ext2_rw_block(sb, bitmap_block, bitmap,false);

                // inode 号从 1 开始
                uint32_t ino = g * fsi->es.s_inodes_per_group + i + 1;
                ret = ino;
                mutex_unlock(&gd->inode_lock);
                return ret;
            }
        }
        mutex_unlock(&gd->inode_lock);
    }
    return -1;
}

static void ext2_free_inode(struct super_block *sb, uint32_t ino)
{
    ext2_fs_info_t *fsi = sb->private_data;
    uint32_t group = (ino - 1) / fsi->es.s_inodes_per_group;
    uint32_t index = (ino - 1) % fsi->es.s_inodes_per_group;
    if (group >= fsi->group_count)
        return;

    ext2_group_desc_cache_t *gd = &fsi->group_descs[group];
    mutex_lock(&gd->inode_lock);
    uint8_t *bitmap = gd->inode_bitmap;
    if (!bitmap) {
        mutex_unlock(&gd->inode_lock);
        return;
    }
    if (bitmap[index/8] & (1 << (index%8))) {
        bitmap[index/8] &= ~(1 << (index%8));
        gd->bg_free_inodes_count++;
        fsi->es.s_free_inodes_count++;
        uint32_t bitmap_block = gd->bg_inode_bitmap;
        ext2_rw_block(sb, bitmap_block, bitmap,false);
    }
    mutex_unlock(&gd->inode_lock);
}
