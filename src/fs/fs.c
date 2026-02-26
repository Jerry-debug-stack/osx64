#include "mm/mm.h"
#include "fs/fs.h"
#include "fs/block.h"
#include "fs/fsmod.h"
#include "lib/string.h"
#include "view/view.h"
#include "const.h"
#include "task.h"
#include "fs/fcntl.h"

#include "fs/ramfs.h"
#include "fs/devfs.h"
#include "fs/ext2.h"

void init_block(void);

extern int init_cwd_for_started_tasks(struct dentry *root);

static inline void init_vfs_mgr(void);
static inline void mount_root_ramfs(void);
static int __vfs_mount_locked(partition_t *part, const char *target_path, int fstype);

static inline void dentry_free(dentry_t *dentry);

static void dentry_cache_task(void);
static uint8_t mount_fs(super_block_t *sb);

struct file *vfs_open(const char *path, int flags, int mode);
ssize_t vfs_read(struct file *file, char *buf, size_t count);
ssize_t vfs_write(struct file *file, const char *buf, size_t count);
int vfs_close(struct file *file);

vfs_manager_t vfs_mgr;

void test_filesystem(void){
    //sys_mount("/dev/sdb0","/mnt");
    //sys_umount("/dev/sdb0");
    int ret;
    int fd[5];
    char buf[64];
    const char *names[] = {"/mnt/file0.txt", "/mnt/file1.txt", "/mnt/file2.txt", "/mnt/file3.txt", "/mnt/file4.txt"};
    const char *data[] = {"data for file0", "data for file1", "data for file2", "data for file3", "data for file4"};

    wb_printf("=== EXT2 Multiple Files Create/Delete Test ===\n");

    // 1. 挂载
    ret = sys_mount("/dev/sdb0", "/mnt");
    if (ret < 0) {
        wb_printf("mount failed: %d\n", ret);
        return;
    }
    wb_printf("Mounted /dev/sdb0 on /mnt\n");

    // 2. 创建5个文件并写入数据
    for (int i = 0; i < 5; i++) {
        fd[i] = sys_open(names[i], O_CREAT | O_RDWR, 0644);
        if (fd[i] < 0) {
            wb_printf("open %s failed: %d\n", names[i], fd[i]);
            goto cleanup;
        }
        ret = sys_write(fd[i], data[i], strlen(data[i]));
        if (ret != (int)strlen(data[i])) {
            wb_printf("write to %s failed\n", names[i]);
            sys_close(fd[i]);
            goto cleanup;
        }
        wb_printf("Created and wrote to %s\n", names[i]);
    }

    // 关闭所有文件
    for (int i = 0; i < 5; i++) {
        sys_close(fd[i]);
    }

    // 3. 删除 file1.txt 和 file3.txt
    ret = sys_unlink("/mnt/file1.txt");
    wb_printf("unlink file1.txt: %s\n", ret == 0 ? "OK" : "FAIL");
    ret = sys_unlink("/mnt/file3.txt");
    wb_printf("unlink file3.txt: %s\n", ret == 0 ? "OK" : "FAIL");

    // 4. 重新打开剩余文件，验证内容
    for (int i = 0; i < 5; i++) {
        if (i == 1 || i == 3) continue; // 已删除
        int f = sys_open(names[i], O_RDONLY, 0);
        if (f < 0) {
            wb_printf("open %s after delete failed (should not happen)\n", names[i]);
            continue;
        }
        memset(buf, 0, sizeof(buf));
        ret = sys_read(f, buf, sizeof(buf)-1);
        if (ret >= 0) {
            buf[ret] = '\0';
            if (strcmp(buf, data[i]) == 0)
                wb_printf("verify %s: OK\n", names[i]);
            else
                wb_printf("verify %s: content mismatch\n", names[i]);
        } else {
            wb_printf("read %s failed\n", names[i]);
        }
        sys_close(f);
    }

    // 尝试打开已删除的文件，预期失败
    int f = sys_open("/mnt/file1.txt", O_RDONLY, 0);
    if (f < 0)
        wb_printf("open deleted file1.txt: OK (expected fail)\n");
    else {
        wb_printf("open deleted file1.txt unexpectedly succeeded\n");
        sys_close(f);
    }

cleanup:
    // 5. 卸载
    ret = sys_umount("/mnt");
    wb_printf("umount /mnt: %s\n", ret == 0 ? "OK" : "FAIL");
    wb_printf("=== Test End ===\n");
}

void init_fs_mem(void){
    init_block();
    init_vfs_mgr();
    kernel_thread("fs_cache",dentry_cache_task,pcb_of_init,0);
}

static inline void init_vfs_mgr(void){
    spin_list_init(&vfs_mgr.inode_list);
    spin_list_init(&vfs_mgr.mount_list);
    spin_list_init(&vfs_mgr.dentry_cache_list);
    atomic_set(&vfs_mgr.dentry_cache_num,0);
    rwlock_init(&vfs_mgr.namespace_lock);
    rwlock_init(&vfs_mgr.mount_lock);
    
    mount_root_ramfs();
    sys_mkdir("/dev",0755);
    if (__vfs_mount_locked(NULL,"/dev",FS_TYPE_DEVFS)){
        wb_printf("[  KERNEL PANIC  ] devfs not mounted!\n");
        halt();
    }
    sys_mkdir("/mnt",0755);

    int total_ref = init_cwd_for_started_tasks(vfs_mgr.root);
    atomic_add(total_ref,&vfs_mgr.root->refcount);
    atomic_add(total_ref,&vfs_mgr.root->in_mnt->fs_ref);
}

static inline void mount_root_ramfs(void)
{
    super_block_t *sb;
    inode_t *root_inode;
    dentry_t *root_dentry;
    sb = super_block_create(NULL, FS_TYPE_RAMFS);
    if (!sb)
        goto failed;
    if (mount_fs(sb) != 0)
        goto failed;
    if (!sb->super_ops || !sb->super_ops->read_root_inode)
        goto failed;
    root_inode = sb->super_ops->read_root_inode(sb);
    if (!root_inode)
        goto failed;
    root_dentry = dentry_create("/", root_inode);
    if (!root_dentry)
        goto failed;
    root_dentry->in_mnt = sb;
    sb_get(sb);
    dentry_get(root_dentry);
    vfs_mgr.root = root_dentry;
    wb_printf("[  VFS  ] Ramfs mounted as root (root ino=%d)\n", root_inode->ino);
    return;
failed:
    wb_printf("[  KERNEL PANIC  ] mount_root_ramfs: failed \n");
    halt();
}

dentry_t *__vfs_lookup_locked(dentry_t *start, const char *target_path)
{
    if (!target_path || target_path[0] == '\0')
        return NULL;

    dentry_t *current;
    char *path_copy;
    char *saveptr;
    char *token;

    // 确定起始点
    if (target_path[0] == '/') {
        current = vfs_mgr.root;
    } else {
        current = start ? start : vfs_mgr.root;
    }
    dentry_get(current);   // 增加查找者引用

    path_copy = kstrdup(target_path);
    if (!path_copy) {
        dentry_put(current);
        return NULL;
    }

    token = strtok_r(path_copy, "/", &saveptr);
    while (token) {
        dentry_t *next = NULL;
        inode_t *current_inode = current->inode;

        // 处理 "." 和 ".."
        if (strcmp(token, ".") == 0) {
            next = current;
            dentry_get(next);
            goto handle_mount;
        }
        if (strcmp(token, "..") == 0) {
            // 判断当前是否在挂载点根
            if (current->in_mnt && current == current->in_mnt->root) {
                // 当前是挂载点根，应跳转到挂载点的父目录
                if (current->in_mnt->mountpoint) {
                    next = current->in_mnt->mountpoint->parent;
                    if (next)
                        dentry_get(next);
                    else
                        next = current; // 回退到自身（根目录的父目录是自身）
                } else {
                    next = current;
                    dentry_get(next);
                }
            } else {
                // 普通情况
                if (current->parent) {
                    next = current->parent;
                    dentry_get(next);
                } else {
                    next = current;
                    dentry_get(next);
                }
            }
            goto handle_mount;
        }

        // 读锁查找 child_list
        read_lock(&current_inode->i_meta_lock);
        struct list_head *pos;
        list_for_each(pos, &current->child_list) {
            dentry_t *child = container_of(pos, dentry_t, child_list_item);
            if (strcmp(child->name, token) == 0) {
                next = child;
                dentry_get(next);   // 查找者引用
                break;
            }
        }
        read_unlock(&current_inode->i_meta_lock);

        if (next)
            goto handle_mount;

        // 未找到，调用底层 lookup
        if (!current_inode->inode_ops || !current_inode->inode_ops->lookup) {
            dentry_put(current);
            kfree(path_copy);
            return NULL;
        }

        dentry_t *tmp = dentry_create(token, NULL);  // refcount = 1 (缓存持有)
        if (!tmp) {
            dentry_put(current);
            kfree(path_copy);
            return NULL;
        }

        int err = current_inode->inode_ops->lookup(current_inode, tmp);
        if (err != 0) {
            dentry_delete(tmp);   // 释放临时 dentry
            dentry_put(current);
            kfree(path_copy);
            return NULL;
        }

        // lookup 成功，tmp->inode 已设置
        write_lock(&current_inode->i_meta_lock);
        // 再次检查是否已被其他线程插入
        list_for_each(pos, &current->child_list) {
            dentry_t *child = container_of(pos, dentry_t, child_list_item);
            if (strcmp(child->name, token) == 0) {
                next = child;
                dentry_get(next);
                dentry_delete(tmp);    // 释放临时 dentry
                break;
            }
        }
        if (!next) {
            // 插入新 dentry
            dentry_get(current);   // 子 dentry 持有父目录引用
            tmp->parent = current;
            list_add_tail(&tmp->child_list_item, &current->child_list);
            next = tmp;            // next 获得临时 dentry 的引用（创建者引用）
            // 注意：tmp 的 refcount 现在为 2（创建者 + 父目录持有）
        }
        write_unlock(&current_inode->i_meta_lock);

        // 如果 next 是已有 child，tmp 已被释放；如果 next 是 tmp，则 next 持有创建者引用
        // 继续处理挂载点

handle_mount:
        // 处理挂载点跳转：如果当前 dentry 是挂载点，则跳转到被挂载文件系统的根
        if (next && (next->flags & DENTRY_FLAG_MOUNTPOINT) && next->mounted_here) {
            dentry_t *mnt_root = next->mounted_here->root;
            if (mnt_root) {
                dentry_get(mnt_root);
                dentry_put(next);   // 释放原 next 的查找者引用
                next = mnt_root;
            }
        }

        // 释放上一级 current 的查找者引用，进入下一层
        dentry_put(current);
        current = next;
        token = strtok_r(NULL, "/", &saveptr);
    }

    kfree(path_copy);
    return current;   // 返回的 current 包含查找者引用
}

dentry_t *vfs_lookup(dentry_t *start,const char *target_path){
    read_lock(&vfs_mgr.namespace_lock);
    dentry_t *ret = __vfs_lookup_locked(start,target_path);
    read_unlock(&vfs_mgr.namespace_lock);
    return ret;
}

super_block_t *super_block_create(partition_t *device,int fstype){
    super_block_t *sb = (super_block_t *)kmalloc(sizeof(super_block_t));
    if (!sb)
        return NULL;
    if (device){
        sb->fs_type = (int32_t)(uint32_t)device->part_type;
        device->mounted_sb = sb;
        sb->part = device;
    }else{
        sb->fs_type = fstype;
    }
    atomic_set(&sb->fs_ref, 0);
    rwlock_init(&sb->sb_lock);
    sb->super_ops = NULL;
    return sb;
}

static uint8_t mount_fs(super_block_t *sb){
    if (!sb->fs_type)
        return -1;
    switch (sb->fs_type)
    {
    case FS_TYPE_RAMFS:
        sb->super_ops = &ramfs_super_ops;
        return 0;
    case FS_TYPE_DEVFS:
        sb->super_ops = &devfs_super_ops;
        return 0;
    case PARTITION_LINUX:
        sb->super_ops = &ext2_super_ops;
        return 0;
    default:
        return -1;
    }
}

dentry_t *dentry_create(const char *name,inode_t *inode){
    if (!name)
        return NULL;

    dentry_t *d = kmalloc(sizeof(dentry_t));
    if (!d)
        return NULL;

    size_t len = strlen(name) + 1;

    d->name = kmalloc(len);
    if (!d->name) {
        kfree(d);
        return NULL;
    }

    memcpy(d->name, (char*)name, len);

    d->parent = NULL;
    d->flags = 0;
    d->in_mnt = NULL;
    d->mounted_here = NULL;
    d->deleted = false;

    INIT_LIST_HEAD(&d->child_list);
    INIT_LIST_HEAD(&d->child_list_item);
    INIT_LIST_HEAD(&d->cache_list_item);

    atomic_set(&d->refcount, 1);
    atomic_set(&d->in_cache_list,0);

    // 🔑 dentry 持有 inode
    if (inode){
        d->inode = inode;
        inode_get(inode);
    }
    return d;
}

void dentry_put(dentry_t *dentry)
{
    if (!dentry) return;
    if (atomic_dec_and_test(&dentry->refcount)) {
        if (dentry->deleted){
            if (dentry->parent){
                dentry_put(dentry->parent);
            }
            dentry_free(dentry);
        }else{
            if (!atomic_test_and_set_bit(0, &dentry->in_cache_list)) {
                spin_lock(&vfs_mgr.dentry_cache_list.lock);
                // 再次检查计数（可能被并发 get 增加）
                if (atomic_read(&dentry->refcount) == 0) {
                    list_add_tail(&dentry->cache_list_item, &vfs_mgr.dentry_cache_list.list);
                    atomic_inc(&vfs_mgr.dentry_cache_num);
                } else {
                    atomic_set(&dentry->in_cache_list,0);
                }
                spin_unlock(&vfs_mgr.dentry_cache_list.lock);
            }
        }
    }
}

void dentry_delete(dentry_t *dentry){
    dentry->deleted = true;

    spin_lock(&vfs_mgr.dentry_cache_list.lock);
    if (atomic_read(&dentry->in_cache_list)){
        atomic_dec(&vfs_mgr.dentry_cache_num);
        list_del(&dentry->cache_list_item);
    }
    spin_unlock(&vfs_mgr.dentry_cache_list.lock);
    dentry_put(dentry);
}

void inode_put(inode_t *inode){
    if (!inode)
        return;

    if (!atomic_dec_and_test(&inode->refcount))
        return;

    if (inode->inode_ops && inode->inode_ops->delete)
        inode->inode_ops->delete(inode);
    
    kfree(inode);
}

static dentry_t *lookup_child(dentry_t *parent, const char *name)
{
    struct list_head *pos;
    list_for_each(pos, &parent->child_list) {
        dentry_t *child = container_of(pos, dentry_t, child_list_item);
        if (strcmp(child->name, name) == 0) {
            dentry_get(child);   // 增加引用供调用者使用
            return child;
        }
    }
    return NULL;
}

static int super_block_destroy(super_block_t *sb){
    if (!sb)
        return -1;
    if (atomic_read(&sb->fs_ref) != 0) {
        return -1;
    }
    if (sb->super_ops && sb->super_ops->put_super)
        sb->super_ops->put_super(sb);
    kfree(sb);
    return 0;
}

static int __vfs_mount_locked(partition_t *part, const char *target_path, int fstype)
{
    dentry_t *mountpoint = NULL;
    super_block_t *sb = NULL;
    inode_t *root_inode = NULL;
    dentry_t *root = NULL;
    int ret = -1;

    mountpoint = __vfs_lookup_locked(NULL, target_path);
    if (!mountpoint) {
        goto out_unlock;
    }

    if ((mountpoint->flags & DENTRY_FLAG_MOUNTPOINT)||(mountpoint->flags & DENTRY_BLOCK_DEV)||(mountpoint->flags & DENTRY_CHARACTER_DEV)) {
        goto out_put_mountpoint;
    }

    if (!S_ISDIR(mountpoint->inode->mode)) {
        goto out_put_mountpoint;
    }

    if (mountpoint->in_mnt && mountpoint == mountpoint->in_mnt->root) {
        ret = -1;
        goto out_put_mountpoint;
    }

    write_lock(&mountpoint->inode->i_meta_lock);
    bool empty = list_empty(&mountpoint->child_list);
    write_unlock(&mountpoint->inode->i_meta_lock);
    if (!empty) {
        // 目录非空
        goto out_put_mountpoint;
    }

    // 如果有设备，检查是否已挂载
    if (part && part->mounted_sb) {
        goto out_put_mountpoint;
    }

    // 创建超级块，传入设备指针和文件系统类型
    if (part)
        sb = super_block_create(part, 0);       // 设备非空，fstype 参数被忽略
    else
        sb = super_block_create(NULL, fstype);  // 无设备，使用传入的 fstype

    if (!sb) {
        goto out_put_mountpoint;
    }

    if (mount_fs(sb)) {
        goto out_destroy_sb;
    }

    if (!sb->super_ops || !sb->super_ops->read_root_inode) {
        goto out_destroy_sb;
    }

    root_inode = sb->super_ops->read_root_inode(sb);
    if (!root_inode) {
        goto out_destroy_sb;
    }

    root = dentry_create("/", root_inode);
    if (!root) {
        goto out_put_inode;
    }
    root->in_mnt = sb;

    sb->root = root;
    sb->mountpoint = mountpoint;
    sb->part = part;   // 可能为 NULL

    mountpoint->flags |= DENTRY_FLAG_MOUNTPOINT;
    mountpoint->mounted_here = sb;

    if (part)
        part->mounted_sb = sb;

    list_add_tail(&sb->mount_list, &vfs_mgr.mount_list.list);

    if (mountpoint->inode && mountpoint->inode->sb) {
        atomic_inc(&mountpoint->inode->sb->fs_ref);
    }

    // 这里由于inode在create时ref为1,而且dentry_create时会增加ref,有ref问题,需要额外inode_put一次root
    inode_put(sb->root->inode);

    return 0;

out_put_inode:
    inode_put(root_inode);
out_destroy_sb:
    super_block_destroy(sb);
out_put_mountpoint:
    dentry_put(mountpoint);
out_unlock:
    
    return ret;
}

int vfs_mount(partition_t *part, const char *target_path, int fstype){
    write_lock(&vfs_mgr.mount_lock);
    write_lock(&vfs_mgr.namespace_lock);
    int ret = __vfs_mount_locked(part,target_path,fstype);
    write_unlock(&vfs_mgr.namespace_lock);
    write_unlock(&vfs_mgr.mount_lock);
    return ret;
}

int sys_mount(const char *dev_path,const char *to_path){
    if (!dev_path || !to_path)
        return -1;
    int ret = -1;
    write_lock(&vfs_mgr.mount_lock);
    write_lock(&vfs_mgr.namespace_lock);
    dentry_t *cwd = get_current()->cwd;
    dentry_t *dev = __vfs_lookup_locked(cwd,dev_path);
    if (!dev)
        goto out;
    if (!(dev->flags & DENTRY_BLOCK_DEV))
        goto out_put_dev;
    partition_t *part = dev->inode->private_data;
    if (part->device.type != BLOCK_PARTITION)
        goto out_put_dev;
    if (part->mounted_sb != NULL)
        goto out_put_dev;
    /* 这里给其它进程的fs都是disk fs */
    ret = __vfs_mount_locked(part,to_path,0);
    if (!ret)
        dentry_get(dev);
out_put_dev:
    dentry_put(dev);
out:
    write_unlock(&vfs_mgr.namespace_lock);
    write_unlock(&vfs_mgr.mount_lock);
    return ret;
}

int sys_umount(const char *target_path)
{
    dentry_t *mountpoint = NULL;
    super_block_t *sb = NULL;
    super_block_t *parent_sb = NULL;
    partition_t *part = NULL;
    int ret = -1;

    // 全程持有 namespace 写锁，保证卸载过程原子性
    write_lock(&vfs_mgr.mount_lock);
    write_lock(&vfs_mgr.namespace_lock);

    // 查找挂载点 dentry（从根开始）
    mountpoint = __vfs_lookup_locked(vfs_mgr.root, target_path);
    if (!mountpoint) {
        goto out_unlock;
    }
    // 如果得到的 dentry 是被挂载文件系统的根，则通过它找到真正的挂载点
    if (mountpoint->in_mnt && mountpoint == mountpoint->in_mnt->root) {
        dentry_t *real_mountpoint = mountpoint->in_mnt->mountpoint;
        dentry_get(real_mountpoint);
        dentry_put(mountpoint);
        mountpoint = real_mountpoint;
    }

    // 检查是否为挂载点
    if (!(mountpoint->flags & DENTRY_FLAG_MOUNTPOINT)) {
        goto out_put_mountpoint;
    }    
    sb = mountpoint->mounted_here;
    if (!sb) {
        // 标志位异常
        mountpoint->flags &= ~DENTRY_FLAG_MOUNTPOINT;
        goto out_put_mountpoint;
    }

    // 检查文件系统是否可卸载（fs_ref == 0）
    if (atomic_read(&sb->fs_ref) != 0) {
        // 仍有活跃引用，返回 EBUSY
        goto out_put_mountpoint;
    }
    // 父超级块
    if (sb->mountpoint) {
        parent_sb = sb->mountpoint->inode->sb;
    }

    part = sb->part;

    // 从全局挂载链表中移除
    list_del(&sb->mount_list);

    // 清除挂载点 dentry 的标志和指针
    mountpoint->flags &= ~DENTRY_FLAG_MOUNTPOINT;
    mountpoint->mounted_here = NULL;

    // 释放 super_block 持有的挂载点 dentry 引用（来自挂载时的保存）
    if (sb->mountpoint) {
        dentry_put(sb->mountpoint);
        sb->mountpoint = NULL;
    }

    // 释放 super_block 的根 dentry 引用
    if (sb->root) {
        dentry_delete(sb->root);
        sb->root = NULL;
    }

    // 销毁 super_block（会调用 put_super 并释放内存）
    super_block_destroy(sb);

    // 释放查找时获得的挂载点 dentry 引用
    dentry_put(mountpoint);

    if (parent_sb)
        atomic_dec(&parent_sb->fs_ref);
    
    if (part){
        part->mounted_sb = NULL;
        dentry_put(part->device.fs_dentry);
    }

    write_unlock(&vfs_mgr.namespace_lock);
    write_unlock(&vfs_mgr.mount_lock);
    return 0;

out_put_mountpoint:
    dentry_put(mountpoint);
out_unlock:
    write_unlock(&vfs_mgr.namespace_lock);
    write_unlock(&vfs_mgr.mount_lock);
    return ret;
}

// 分配一个空闲的文件描述符，返回 fd，若没有可用则返回 -1
int fd_alloc(pcb_t *proc)
{
    for (int i = 0; i < NR_OPEN_DEFAULT; i++) {
        if (proc->files[i] == NULL)
            return i;
    }
    return -1;  // 无可用
}

// 将 file 指针安装到指定的 fd
void fd_install(pcb_t *proc, int fd, struct file *file)
{
    if (fd < 0 || fd >= NR_OPEN_DEFAULT)
        return;
    proc->files[fd] = file;
}

// 根据 fd 获取 file 指针，若 fd 无效或未打开则返回 NULL
struct file *fd_get(pcb_t *proc, int fd)
{
    if (fd < 0 || fd >= NR_OPEN_DEFAULT)
        return NULL;
    return proc->files[fd];
}

// 关闭指定 fd（由系统调用 close 调用）
int fd_close(pcb_t *proc, int fd)
{
    struct file *file = fd_get(proc, fd);
    if (!file)
        return -1;  // EBADF
    proc->files[fd] = NULL;
    return vfs_close(file);  // 假设 vfs_close 释放 file 结构并返回 0 成功
}

void dentry_set_parent(dentry_t *parent,dentry_t *child){
    if (!parent||!child)
        return;
    dentry_get(parent);
    child->parent = parent;
    write_lock(&parent->inode->i_meta_lock);
    list_add_tail(&child->child_list_item,&parent->child_list);
    write_unlock(&parent->inode->i_meta_lock);
}

/// @brief 将路径分割为父目录路径和最后一个分量
/// @param path 路径字符串（可以绝对或相对）
/// @param dir_path 输出参数，指向父目录路径字符串（动态分配，调用者需 kfree）若父目录为空（即路径中只有一个分量），则返回空字符串
/// @name 输出参数，指向最后一个分量的起始位置（指向 path 内部，无需释放）
/// @return 成功，-1 失败（路径无效或最后一个分量为 "." 或 ".." 且用于创建时
/// @warning 本函数不会检查路径是否存在，只做字符串分割。
static int split_path(const char *path, char **dir_path, const char **name)
{
    const char *p, *last_slash;

    if (!path || path[0] == '\0')
        return -1;

    // 找到路径最后一个非斜杠字符的位置
    p = path + strlen(path) - 1;
    while (p >= path && *p == '/')
        p--;

    if (p < path) {
        // 路径只包含斜杠，如 "/" 或 "///"
        *dir_path = kstrdup("/");
        if (!*dir_path) return -1;
        *name = "";   // 空文件名，表示根目录自身
        return 0;
    }

    // 现在 p 指向最后一个非斜杠字符
    // 从 p 向前找到上一个 '/'（即路径分隔符）
    last_slash = p;
    while (last_slash >= path && *last_slash != '/')
        last_slash--;

    // 确定文件名部分
    *name = last_slash + 1;  // 即使 last_slash 指向 '/' 的前一个，+1 指向文件名首字符

    // 确定父目录部分
    if (last_slash < path) {
        // 没有斜杠，父目录为空
        *dir_path = kstrdup("");
        if (!*dir_path) return -1;
    } else {
        // 有斜杠，截取 [path, last_slash) 作为父目录，并去除末尾多余的斜杠
        int parent_len = last_slash - path;
        // 去除父目录末尾多余的斜杠（保留一个？）
        while (parent_len > 1 && path[parent_len-1] == '/')
            parent_len--;
        if (parent_len == 0) {
            // 父目录是根目录 "/"
            *dir_path = kstrdup("/");
        } else {
            char *parent = kmalloc(parent_len + 1);
            if (!parent) return -1;
            memcpy(parent, (void*)path, parent_len);
            parent[parent_len] = '\0';
            *dir_path = parent;
        }
    }

    // 如果用于创建文件，不允许最后一个分量为 "." 或 ".."
    // 调用者可以自行检查，这里不强制
    return 0;
}

struct file *vfs_open(const char *path, int flags, int mode)
{
    dentry_t *dentry = NULL;
    dentry_t *parent = NULL;
    dentry_t *start = NULL;
    char *parent_path = NULL;
    const char *filename = NULL;
    struct file *file;
    int ret;

    if (!path || path[0] == '\0')
        return NULL;

    // 确定起始 dentry
    pcb_t *current = get_current();   // 假设存在获取当前进程的函数
    if (path[0] == '/') {
        start = vfs_mgr.root;
        dentry_get(start);
    } else {
        start = current->cwd;
        dentry_get(start);
    }

    // 先尝试完整路径查找
    dentry = vfs_lookup(start, path);
    if (dentry) {
        // 文件存在
        if (flags & O_CREAT && flags & O_EXCL) {
            dentry_put(dentry);
            dentry_put(start);
            return NULL;
        }
        // 存在则直接打开，释放 start 引用
        dentry_put(start);
        goto found;
    }

    // 文件不存在，如果需要创建
    if (!(flags & O_CREAT)) {
        dentry_put(start);
        return NULL;
    }

    // 分割路径
    if (split_path(path, &parent_path, &filename) < 0) {
        dentry_put(start);
        return NULL;
    }

    // 解析父目录
    if (parent_path[0] == '\0') {
        // 父目录就是起始目录
        parent = start;   // 借用 start 的引用
        // start 引用将在此分支结束后释放（通过后续的 dentry_put(parent)）
    } else {
        // 解析父目录路径
        parent = vfs_lookup(start, parent_path);
        kfree(parent_path);
        dentry_put(start);   // 释放 start 引用
        if (!parent) {
            return NULL;
        }
    }

    // 检查父目录是否为目录
    if (!S_ISDIR(parent->inode->mode)) {
        dentry_put(parent);
        return NULL;
    }

    // 检查文件名是否有效（不能为 "." 或 ".."）
    if (strcmp(filename, ".") == 0 || strcmp(filename, "..") == 0) {
        dentry_put(parent);
        return NULL;
    }

    // 创建 dentry 对象（暂时没有 inode）
    dentry = dentry_create(filename, NULL);
    if (!dentry) {
        dentry_put(parent);
        return NULL;
    }

    // 调用父目录的 create 方法
    if (!parent->inode->inode_ops || !parent->inode->inode_ops->create) {
        dentry_delete(dentry);
        dentry_put(parent);
        return NULL;
    }
    dentry->in_mnt = parent->in_mnt;
    ret = parent->inode->inode_ops->create(parent->inode, dentry, mode);
    if (ret < 0) {
        dentry_delete(dentry);
        dentry_put(parent);
        return NULL;
    }
    // create 成功后，dentry->inode 已设置，且 dentry 已被加入父目录的 child_list
    // dentry 的 negative 标志已被清除
    // 释放临时 parent 引用（因为 dentry 已经持有 parent，并且我们不再需要单独的 parent）
    dentry_set_parent(parent, dentry);
    dentry_put(parent);

    // 现在 dentry 有效，继续打开文件流程

found:
    // 分配 file 结构
    file = (struct file *)kmalloc(sizeof(struct file));
    if (!file) {
        dentry_put(dentry);
        return NULL;
    }
    memset(file, 0, sizeof(struct file));
    file->dentry = dentry;
    file->inode = dentry->inode;
    file->pos = (flags & O_APPEND) ? dentry->inode->size : 0;
    file->flags = flags;
    mutex_init(&file->lock);
    atomic_set(&file->refcount, 1);
    file->file_ops = dentry->inode->default_file_ops;
    if (!file->file_ops) {
        kfree(file);
        dentry_put(dentry);
        return NULL;
    }

    if (file->file_ops->open) {
        ret = file->file_ops->open(dentry->inode, file);
        if (ret < 0) {
            kfree(file);
            dentry_put(dentry);
            return NULL;
        }
    }

    inode_get(dentry->inode);
    sb_get(file->dentry->in_mnt);

    // 处理 O_TRUNC 等（暂略）
    return file;
}

ssize_t vfs_read(struct file *file, char *buf, size_t count)
{
    if (!file || !file->file_ops || !file->file_ops->read)
        return -1;
    mutex_lock(&file->lock);
    read_lock(&file->inode->i_meta_lock);
    ssize_t ret = file->file_ops->read(file, buf, count, (int64_t*)&file->pos);
    read_unlock(&file->inode->i_meta_lock);
    mutex_unlock(&file->lock);
    return ret;
}

ssize_t vfs_write(struct file *file, const char *buf, size_t count)
{
    if (!file || !file->file_ops || !file->file_ops->write)
        return -1;
    mutex_lock(&file->lock);
    mutex_lock(&file->inode->i_data_lock);
    read_lock(&file->inode->i_meta_lock);
    ssize_t ret = file->file_ops->write(file, buf, count, (int64_t*)&file->pos);
    read_unlock(&file->inode->i_meta_lock);
    mutex_unlock(&file->inode->i_data_lock);
    mutex_unlock(&file->lock);
    return ret;
}

int vfs_close(struct file *file)
{
    if (!file)
        return -1;
    if (!atomic_dec_and_test(&file->refcount))
        return 0;  // 还有其他引用

    super_block_t *sb = file->dentry->in_mnt;

    // 调用 release 方法
    if (file->file_ops && file->file_ops->release)
        file->file_ops->release(file->inode, file);

    // 释放 dentry,inode 引用
    if (file->inode)
        inode_put(file->inode);
    dentry_put(file->dentry);

    // 释放fs_ref
    sb_put(sb);

    // 释放 file 结构自身
    kfree(file);

    return 0;
}

static inline void dentry_free(dentry_t *dentry){
    if (dentry->inode) {
        inode_put(dentry->inode);
    }
    if (dentry->name)
        kfree(dentry->name);
    kfree(dentry);
}

static void dentry_cache_task(void)
{
    while (1) {
        dentry_t *dentry;
        // 从队列取出（已移出）
        spin_lock(&vfs_mgr.dentry_cache_list.lock);
        if (atomic_read(&vfs_mgr.dentry_cache_num) <= DENTRY_CACHE_SIZE) {
            spin_unlock(&vfs_mgr.dentry_cache_list.lock);
            yield();
            continue;
        }
        dentry = list_first_entry(&vfs_mgr.dentry_cache_list.list, dentry_t, cache_list_item);
        list_del_init(&dentry->cache_list_item);
        spin_unlock(&vfs_mgr.dentry_cache_list.lock);

        // 获取父目录锁（若存在）
        inode_t *parent_inode = dentry->parent ? dentry->parent->inode : NULL;
        if (parent_inode)
            write_lock(&parent_inode->i_meta_lock);

        // 循环处理，应对并发释放
        while (1) {
            int ref = atomic_read(&dentry->refcount);
            if (ref == 0) {
                // 安全释放
                if (parent_inode) {
                    list_del_init(&dentry->child_list_item);
                    dentry_put(dentry->parent);  // 释放父目录引用
                }
                atomic_dec(&vfs_mgr.dentry_cache_num);
                if (parent_inode)
                    write_unlock(&parent_inode->i_meta_lock);
                dentry_free(dentry);
                break;  // 结束
            } else {
                // 需要放回队列
                spin_lock(&vfs_mgr.dentry_cache_list.lock);
                // 再次检查计数（可能在拿锁期间变化）
                if (atomic_read(&dentry->refcount) == 0) {
                    spin_unlock(&vfs_mgr.dentry_cache_list.lock);
                    continue;  // 重新检查，走释放路径
                }
                // 仍非0，放回队列（标志已为true，无需改）
                list_add_tail(&dentry->cache_list_item, &vfs_mgr.dentry_cache_list.list);
                spin_unlock(&vfs_mgr.dentry_cache_list.lock);
                if (parent_inode)
                    write_unlock(&parent_inode->i_meta_lock);
                break;  // 结束，等待下次处理
            }
        }
    }
}

int devfs_block_register(const char *name,int mode, struct file_operations *fops,block_device_t *bdev,uint64_t flags,bool locked)
{
    int ret = -1;
    if (!devfs_sb) {
        return -1;
    }
    if (!locked){
        write_lock(&vfs_mgr.mount_lock);
        write_lock(&vfs_mgr.namespace_lock);
    }

    dentry_t *find = __vfs_lookup_locked(devfs_sb->root,name);
    if (find){
        dentry_put(find);
        goto out;
    }

    inode_t *new_node = kmalloc(sizeof(inode_t));
    if (!new_node){
        goto out;
    }

    new_node->default_file_ops = fops;
    new_node->deleting = false;
    mutex_init(&new_node->i_data_lock);
    rwlock_init(&new_node->i_meta_lock);
    new_node->ino = 0;
    new_node->inode_ops = &devfs_root_iops;
    INIT_LIST_HEAD(&new_node->lru_node);
    new_node->mode = mode;
    new_node->private_data = bdev;
    atomic_set(&new_node->refcount,1);
    atomic_set(&new_node->link_count,1);
    new_node->sb = devfs_sb;
    new_node->size = 0;

    dentry_t *new_dentry = dentry_create(name,new_node);
    if (!new_dentry){
        kfree(new_node);
        goto out;
    }
    new_dentry->in_mnt = devfs_sb;
    new_dentry->flags |= flags;
    dentry_set_parent(devfs_sb->root,new_dentry);

    bdev->fs_dentry = new_dentry;
    
    ret = 0;
out:
    if (!locked){
        write_unlock(&vfs_mgr.namespace_lock);
        write_unlock(&vfs_mgr.mount_lock);
    }
    return ret;
}

int __devfs_unregister_locked(const char *name)
{
    bool found = false;
    dentry_t *target = NULL;
    
    if (!name)
        return -1;
    dentry_t *parent = devfs_sb->root;
    list_head_t *pos;
    list_for_each(pos,&parent->child_list){
        dentry_t *child = container_of(pos,dentry_t,child_list_item);
        if (!strcmp(name,child->name)){
            found = true;
            target = child;
            break;
        }
    }
    if (!found)
        return -1;
    list_del(&target->child_list_item);
    dentry_delete(target);
    return 0;
}

int sys_open(const char *path, int flags, int mode)
{
    read_lock(&vfs_mgr.mount_lock);
    struct file *file = vfs_open(path, flags, mode);
    read_unlock(&vfs_mgr.mount_lock);
    if (!file)
        return -1;

    pcb_t *current = get_current();
    int fd = fd_alloc(current);
    if (fd < 0) {
        vfs_close(file);
        return -1;
    }
    fd_install(current, fd, file);
    return fd;
}

ssize_t sys_read(int fd, char *buf, size_t count)
{
    pcb_t *current = get_current();
    struct file *file = fd_get(current, fd);
    if (!file)
        return -1;
    return vfs_read(file, buf, count);
}

ssize_t sys_write(int fd, const char *buf, size_t count)
{
    pcb_t *current = get_current();
    struct file *file = fd_get(current, fd);
    if (!file)
        return -1;
    return vfs_write(file, buf, count);
}

int sys_close(int fd)
{
    pcb_t *current = get_current();
    return fd_close(current, fd);
}

off_t sys_lseek(int fd, off_t offset, int whence)
{
    pcb_t *current = get_current();
    struct file *file = fd_get(current, fd);
    off_t new_pos;
    inode_t *inode;

    if (!file)
        return -1;

    mutex_lock(&file->lock);
    inode = file->inode;

    // 根据 whence 计算新位置
    switch (whence) {
    case SEEK_SET:
        new_pos = offset;
        break;
    case SEEK_CUR:
        new_pos = file->pos + offset;
        break;
    case SEEK_END:
        new_pos = inode->size + offset;
        break;
    default:
        return -1;
    }

    // 检查新位置是否有效（不能为负）
    if (new_pos < 0)
        return -1;

    // 更新文件位置
    file->pos = new_pos;
    mutex_unlock(&file->lock);
    return new_pos;
}

int sys_mkdir(const char *path, int mode)
{
    dentry_t *start = NULL;
    dentry_t *parent = NULL;
    dentry_t *dentry = NULL;
    char *parent_path = NULL;
    const char *name;
    pcb_t *current = get_current();
    int ret = -1;

    if (!path || path[0] == '\0')
        return -1;

    write_lock(&vfs_mgr.namespace_lock);

    // 确定起始 dentry
    if (path[0] == '/') {
        start = vfs_mgr.root;
        dentry_get(start);
    } else {
        start = current->cwd;
        dentry_get(start);
    }

    // 先尝试查找，若存在则返回错误
    dentry = __vfs_lookup_locked(start, path);
    if (dentry) {
        dentry_put(dentry);
        ret = -1;   // EEXIST
        goto out_put_start;
    }

    // 分割路径，获取父目录路径和最后一层名称
    if (split_path(path, &parent_path, &name) < 0)
        goto out_put_start;

    // 不允许以 "." 或 ".." 作为新目录名
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        kfree(parent_path);
        goto out_put_start;
    }

    // 查找父目录
    if (parent_path[0] == '\0') {
        parent = start;
        dentry_get(parent);   // 增加临时引用，统一释放
    } else {
        parent = __vfs_lookup_locked(start, parent_path);
        if (!parent) {
            kfree(parent_path);
            goto out_put_start;
        }
    }
    kfree(parent_path);

    // 检查父目录是否为目录
    if (!S_ISDIR(parent->inode->mode)) {
        dentry_put(parent);
        goto out_put_start;
    }

    // 创建临时 dentry（尚未关联 inode）
    dentry = dentry_create(name, NULL);
    if (!dentry) {
        dentry_put(parent);
        goto out_put_start;
    }

    // 调用具体文件系统的 mkdir 方法
    if (!parent->inode->inode_ops || !parent->inode->inode_ops->mkdir) {
        dentry_delete(dentry);
        dentry_put(parent);
        goto out_put_start;
    }
    ret = parent->inode->inode_ops->mkdir(parent->inode, dentry, mode);
    if (ret < 0) {
        dentry_delete(dentry);
        dentry_put(parent);
        goto out_put_start;
    }
    dentry->in_mnt = parent->in_mnt;
    // mkdir 成功，将 dentry 加入父目录的 child_list，并建立引用关系
    dentry_set_parent(parent, dentry);

    // 释放临时创建的 dentry 引用（现在由父目录持有）
    dentry_put(dentry);
    // 释放父目录临时引用
    dentry_put(parent);
    // 释放起始点引用
    dentry_put(start);
    write_unlock(&vfs_mgr.namespace_lock);
    return 0;

out_put_start:
    if (start) dentry_put(start);
    write_unlock(&vfs_mgr.namespace_lock);
    return ret;
}

int sys_rmdir(const char *path)
{
    dentry_t *start = NULL;
    dentry_t *parent = NULL;
    dentry_t *dentry = NULL;
    pcb_t *current = get_current();
    int ret = -1;

    if (!path || path[0] == '\0')
        return -1;

    write_lock(&vfs_mgr.namespace_lock);

    // 确定起始 dentry
    if (path[0] == '/') {
        start = vfs_mgr.root;
        dentry_get(start);
    } else {
        start = current->cwd;
        dentry_get(start);
    }

    // 查找目标 dentry
    dentry = __vfs_lookup_locked(start, path);
    if (!dentry) {
        goto out_put_start;  // 不存在
    }

    // 检查是否为目录
    if (!S_ISDIR(dentry->inode->mode)) {
        goto out_put_dentry;  // ENOTDIR
    }

    // 获取父目录
    parent = dentry->parent;
    if (!parent) {
        // 根目录不能删除
        goto out_put_dentry;
    }
    dentry_get(parent);  // 临时引用

    // 检查目录是否为空（通过父目录的锁保护 child_list 的遍历）
    write_lock(&parent->inode->i_meta_lock);
    bool empty = list_empty(&dentry->child_list);
    write_unlock(&parent->inode->i_meta_lock);
    if (!empty) {
        ret = -1;  // ENOTEMPTY
        goto out_put_parent;
    }

    // 检查目录不能是挂载点
    if (dentry->flags & DENTRY_FLAG_MOUNTPOINT) {
        ret = -1;  // EBUSY
        goto out_put_parent;
    }

    if (atomic_read(&dentry->refcount) != 1){
        ret = -1;
        goto out_put_parent;
    }

    // 调用具体文件系统的 rmdir 方法
    if (!parent->inode->inode_ops || !parent->inode->inode_ops->rmdir) {
        goto out_put_parent;
    }
    ret = parent->inode->inode_ops->rmdir(parent->inode, dentry);
    if (ret < 0)
        goto out_put_parent;

    // 从父目录的 child_list 中移除该 dentry
    write_lock(&parent->inode->i_meta_lock);
    list_del_init(&dentry->child_list_item);
    write_unlock(&parent->inode->i_meta_lock);

    // 释放 dentry 持有的父目录引用（来自 dentry_set_parent）
    dentry_put(parent);
    // 释放起始点引用
    dentry_put(start);
    write_unlock(&vfs_mgr.namespace_lock);

    if (dentry->inode)
        atomic_dec(&dentry->inode->link_count);
    dentry_delete(dentry);
    return 0;

out_put_parent:
    dentry_put(parent);
out_put_dentry:
    dentry_put(dentry);
out_put_start:
    dentry_put(start);
    write_unlock(&vfs_mgr.namespace_lock);
    return ret;
}

int sys_unlink(const char *path)
{
    dentry_t *dentry = NULL;
    dentry_t *parent = NULL;
    dentry_t *start = NULL;
    pcb_t *current = get_current();
    int ret = -1;

    if (!path || path[0] == '\0')
        return -1;

    // 全程持有 namespace 写锁，保证路径解析和删除的原子性
    write_lock(&vfs_mgr.namespace_lock);

    // 确定起始 dentry（根或当前工作目录）
    if (path[0] == '/')
        start = vfs_mgr.root;
    else
        start = current->cwd;
    dentry_get(start);  // 临时引用，防止被释放

    // 使用 __vfs_lookup_locked 在锁保护下查找目标 dentry
    dentry = __vfs_lookup_locked(start, path);
    if (!dentry) {
        dentry_put(start);
        write_unlock(&vfs_mgr.namespace_lock);
        return -1;  // 文件不存在
    }

    // 检查是否为目录（目录应由 rmdir 处理）
    if (S_ISDIR(dentry->inode->mode)) {
        dentry_put(dentry);
        dentry_put(start);
        write_unlock(&vfs_mgr.namespace_lock);
        return -1;  // EISDIR
    }

    // 获取父目录（dentry->parent 在锁保护下稳定）
    parent = dentry->parent;
    if (!parent) {
        // 理论上不会发生（根目录不会被删除）
        dentry_put(dentry);
        dentry_put(start);
        write_unlock(&vfs_mgr.namespace_lock);
        return -1;
    }
    dentry_get(parent);  // 临时引用，后面要用

    // 调用具体文件系统的 unlink 方法
    if (!parent->inode->inode_ops || !parent->inode->inode_ops->unlink) {
        ret = -1;
        goto out;
    }
    ret = parent->inode->inode_ops->unlink(parent->inode, dentry);
    if (ret < 0)
        goto out;

    // 从父目录的 child_list 中移除该 dentry（需要父目录 inode 的锁）
    write_lock(&parent->inode->i_meta_lock);
    list_del_init(&dentry->child_list_item);  // 从链表移除
    write_unlock(&parent->inode->i_meta_lock);

    dentry_put(parent);  // 对应 dentry_set_parent 时增加的引用
    dentry_put(start);

    write_unlock(&vfs_mgr.namespace_lock);

    if (dentry->inode)
        atomic_dec(&dentry->inode->link_count);
    dentry_delete(dentry);
    return 0;

out:
    // 错误处理：释放临时引用
    if (parent) dentry_put(parent);
    dentry_put(dentry);
    dentry_put(start);
    write_unlock(&vfs_mgr.namespace_lock);
    return ret;
}

int sys_chdir(const char *path)
{
    dentry_t *dentry;
    pcb_t *current = get_current();

    read_lock(&vfs_mgr.mount_lock);
    dentry = vfs_lookup(current->cwd, path);
    if (!dentry) {
        read_unlock(&vfs_mgr.mount_lock);
        return -1;
    }

    if (!S_ISDIR(dentry->inode->mode)) {
        dentry_put(dentry);
        read_unlock(&vfs_mgr.mount_lock);
        return -1;
    }

    sb_get(dentry->in_mnt);
    sb_put(current->cwd->in_mnt);

    dentry_put(current->cwd);
    current->cwd = dentry;
    read_unlock(&vfs_mgr.mount_lock);
    return 0;
}

int sys_ftruncate(int fd, off_t length)
{
    pcb_t *current = get_current();
    struct file *file = fd_get(current, fd);
    if (!file)
        return -1;

    // 检查写入权限（根据打开模式）
    if ((file->flags & O_ACCMODE) == O_RDONLY)
        return -1;  // 只读文件不可截断

    inode_t *inode = file->inode;

    if (!S_ISREG(inode->mode)) {
        return -1;
    }

    if (!inode->inode_ops || !inode->inode_ops->setattr)
        return -1;

    // 准备属性
    struct iattr attr;
    attr.ia_valid = ATTR_SIZE;
    attr.ia_size = length;

    // 加 inode 元数据锁（写锁），保护 size 等元数据
    write_lock(&inode->i_meta_lock);

    // 调用具体文件系统的 setattr
    int ret = inode->inode_ops->setattr(inode, &attr);

    write_unlock(&inode->i_meta_lock);
    return ret;
}

int sys_truncate(const char *path, off_t length)
{
    dentry_t *start = NULL;
    dentry_t *dentry = NULL;
    inode_t *inode;
    pcb_t *current = get_current();
    int ret = -1;

    if (!path || path[0] == '\0')
        return -1;

    // 确定起始 dentry
    if (path[0] == '/') {
        start = vfs_mgr.root;
        dentry_get(start);
    } else {
        start = current->cwd;
        dentry_get(start);
    }

    dentry = vfs_lookup(start, path);
    if (!dentry) {
        dentry_put(start);
        return -1;  // 文件不存在
    }

    inode = dentry->inode;
    if (!inode) {
        goto out;
    }

    // 检查文件类型：普通文件才能截断
    if (!S_ISREG(inode->mode)) {
        goto out;
    }

    // 检查写权限（简化：只要文件可写即可，具体权限检查可后续完善）
    // 这里省略权限检查，由文件系统 setattr 内部处理

    if (!inode->inode_ops || !inode->inode_ops->setattr) {
        goto out;
    }

    // 准备属性
    struct iattr attr;
    attr.ia_valid = ATTR_SIZE;
    attr.ia_size = length;

    // 加 inode 元数据锁（写锁），保护 size 等元数据
    write_lock(&inode->i_meta_lock);
    ret = inode->inode_ops->setattr(inode, &attr);
    write_unlock(&inode->i_meta_lock);

out:
    dentry_put(dentry);
    dentry_put(start);
    return ret;
}

int sys_rename(const char *oldpath, const char *newpath)
{
    dentry_t *old_start = NULL, *new_start = NULL;
    dentry_t *old_dentry = NULL;
    dentry_t *old_parent = NULL, *new_parent = NULL;
    char *old_parent_path = NULL, *new_parent_path = NULL;
    const char *old_name, *new_name;
    pcb_t *current = get_current();
    int ret = -1;

    if (!oldpath || !newpath || oldpath[0] == '\0' || newpath[0] == '\0')
        return -1;

    write_lock(&vfs_mgr.namespace_lock);

    // 确定起始 dentry
    if (oldpath[0] == '/')
        old_start = vfs_mgr.root;
    else
        old_start = current->cwd;
    dentry_get(old_start);

    if (newpath[0] == '/')
        new_start = vfs_mgr.root;
    else
        new_start = current->cwd;
    dentry_get(new_start);

    // 查找源 dentry
    old_dentry = __vfs_lookup_locked(old_start, oldpath);
    if (!old_dentry)
        goto out;

    // 分割源路径
    if (split_path(oldpath, &old_parent_path, &old_name) < 0)
        goto out;
    if (old_parent_path[0] == '\0') {
        old_parent = old_start;
        dentry_get(old_parent);
    } else {
        old_parent = __vfs_lookup_locked(old_start, old_parent_path);
        if (!old_parent) {
            kfree(old_parent_path);
            goto out;
        }
    }
    kfree(old_parent_path);

    // 分割目标路径
    if (split_path(newpath, &new_parent_path, &new_name) < 0)
        goto out;
    if (new_parent_path[0] == '\0') {
        new_parent = new_start;
        dentry_get(new_parent);
    } else {
        new_parent = __vfs_lookup_locked(new_start, new_parent_path);
        if (!new_parent) {
            kfree(new_parent_path);
            goto out;
        }
    }
    kfree(new_parent_path);

    // 检查源和目标是否在同一文件系统
    if (old_parent->inode->sb != new_parent->inode->sb) {
        ret = -1;  // 跨设备非法
        goto out;
    }

    // 如果源是目录，检查目标是否在其子目录下（防止循环）
    if (S_ISDIR(old_dentry->inode->mode)) {
        dentry_t *tmp = new_parent;
        while (tmp) {
            if (tmp == old_dentry) {
                ret = -1;  // 非法
                goto out;
            }
            tmp = tmp->parent;
        }
    }

    // 检查目标是否已存在（不支持覆盖，除非是同一个文件）
    dentry_t *tmp = lookup_child(new_parent, new_name);
    if (tmp) {
        bool same_file = (tmp->inode == old_dentry->inode);
        dentry_put(tmp);
        if (!same_file) {
            ret = -1;  // 目标存在且不是同一个文件
            goto out;
        }
    }

    // 调用具体文件系统的 rename 方法（new_name 直接传递字符串）
    if (!old_parent->inode->inode_ops || !old_parent->inode->inode_ops->rename) {
        goto out;
    }
    ret = old_parent->inode->inode_ops->rename(old_parent->inode, old_dentry,
                                               new_parent->inode, new_name);
    if (ret < 0)
        goto out;

    // VFS 层更新：将 old_dentry 从旧父目录移到新父目录
    write_lock(&old_parent->inode->i_meta_lock);
    list_del_init(&old_dentry->child_list_item);
    write_unlock(&old_parent->inode->i_meta_lock);
    dentry_put(old_parent);   // 释放旧父目录引用

    write_lock(&new_parent->inode->i_meta_lock);
    list_add_tail(&old_dentry->child_list_item, &new_parent->child_list);
    write_unlock(&new_parent->inode->i_meta_lock);

    // 更新 old_dentry 的父指针和名称
    dentry_get(new_parent);   // 新父目录引用
    old_dentry->parent = new_parent;

    char *new_name_dup = kstrdup(new_name);
    if (new_name_dup) {
        kfree(old_dentry->name);
        old_dentry->name = new_name_dup;
    }
    ret = 0;

out:
    if (old_dentry) dentry_put(old_dentry);
    if (old_parent) dentry_put(old_parent);
    if (new_parent) dentry_put(new_parent);
    dentry_put(old_start);
    dentry_put(new_start);
    write_unlock(&vfs_mgr.namespace_lock);
    return ret;
}

int sys_dup(int oldfd)
{
    pcb_t *current = get_current();
    struct file *file = fd_get(current, oldfd);
    if (!file)
        return -1;  // EBADF

    int newfd = fd_alloc(current);
    if (newfd < 0)
        return -1;  // EMFILE

    atomic_inc(&file->refcount);
    current->files[newfd] = file;
    return newfd;
}

int sys_dup2(int oldfd, int newfd)
{
    pcb_t *current = get_current();

    if (oldfd == newfd) {
        // 检查 oldfd 是否有效
        if (fd_get(current, oldfd))
            return newfd;
        else
            return -1;  // EBADF
    }

    if (newfd < 0 || newfd >= NR_OPEN_DEFAULT)
        return -1;  // EBADF

    struct file *oldfile = fd_get(current, oldfd);
    if (!oldfile)
        return -1;  // EBADF

    struct file *target = current->files[newfd];
    if (target == oldfile) {
        // 已经指向同一个文件，无需操作
        return newfd;
    }

    if (target) {
        fd_close(current, newfd);  // 关闭目标 fd，释放旧引用
    }

    atomic_inc(&oldfile->refcount);
    current->files[newfd] = oldfile;
    return newfd;
}

int sys_getcwd(char *buf, size_t size)
{
    pcb_t *current = get_current();
    dentry_t *cur = current->cwd;
    dentry_t *names[256];  // 存储路径上的 dentry（从当前到根，但不包括根）
    int depth = 0;
    int total_len = 0;
    int i;

    if (!cur)
        return -1;

    // 回溯收集路径上的 dentry（不包括根）
    while (cur != vfs_mgr.root) {
        if (cur->parent == NULL && cur->in_mnt != NULL) {
            // 当前是挂载根，需要切换到挂载点
            dentry_t *mntpt = cur->in_mnt->mountpoint;
            if (!mntpt) {
                // 异常，挂载点不存在
                return -1;
            }
            names[depth++] = mntpt;
            cur = mntpt->parent;
        } else {
            // 普通 dentry
            names[depth++] = cur;
            cur = cur->parent;
        }
        if (!cur && depth == 0) {
            // 已经到达根但 cur 为 NULL？不应发生
            break;
        }
    }

    // 计算总长度
    for (i = depth - 1; i >= 0; i--) {
        const char *name = names[i]->name;
        if (i == depth - 1 && name[0] == '/' && name[1] == '\0') {
            // 根目录的特殊情况，但这里不应该出现，因为根不会被收集
            total_len += 1;
        } else {
            if (i != depth - 1) total_len += 1; // 分隔符
            total_len += strlen(name);
        }
    }
    if (cur == vfs_mgr.root) {
        // 根目录需要加 "/"
        total_len += 1;
    }

    if ((size_t)total_len + 1 > size) {
        return -1; // 缓冲区太小
    }

    // 构建路径
    int pos = 0;
    // 先处理根
    if (cur == vfs_mgr.root) {
        buf[pos++] = '/';
    }
    // 从根向下遍历 names（逆序）
    for (i = depth - 1; i >= 0; i--) {
        const char *name = names[i]->name;
        // 如果当前不是第一个组件，且上一个字符不是 '/', 则添加 '/'
        if (pos > 0 && buf[pos-1] != '/') {
            buf[pos++] = '/';
        }
        strcpy(buf + pos, (char*)name);
        pos += strlen(name);
    }
    buf[pos] = '\0';

    return 0;
}

int sys_reload_partition(char *target){
    if (!target)    
        return -1;
    int ret = -1;
    pcb_t *current = get_current();
    write_lock(&vfs_mgr.mount_lock);
    write_lock(&vfs_mgr.namespace_lock);
    dentry_t *d = __vfs_lookup_locked(current->cwd,target);
    if (!d)
        goto out;
    if (!d->inode || !(d->flags & DENTRY_BLOCK_DEV))
        goto out_d_found;
    real_device_t *real = d->inode->private_data;
    if (real->device.type != BLOCK_DISK)
        goto out_d_found;
    if (atomic_read(&real->device.fs_dentry->refcount) != 2){
        goto out_d_found;
    }
    
    bool able = true;
    list_head_t *pos;
    list_for_each(pos,&real->childs.list){
        partition_t *par = container_of(pos,partition_t,childs_list_item);
        if (atomic_read(&par->device.fs_dentry->refcount) != 1){
            able = false;
            break;
        }
    }
    if (!able)
        goto out_d_found;
    
    list_head_t *next;
    list_for_each_safe(pos,next,&real->childs.list){
        partition_t *par = container_of(pos,partition_t,childs_list_item);
        __devfs_unregister_locked(par->device.name);
        block_unregister((block_device_t*)par);
    }
    mbr_scan((block_device_t*)real,true);
    ret = 0;

out_d_found:
    dentry_put(d);
out:
    write_unlock(&vfs_mgr.namespace_lock);
    write_unlock(&vfs_mgr.mount_lock);
    return ret;
}

int sys_getdent(int fd, struct dirent __user *dirp, unsigned int count)
{
    pcb_t *current = get_current();
    struct file *file = fd_get(current, fd);
    if (!file)
        return -1;

    if (!S_ISDIR(file->inode->mode))
        return -1;

    if (!file->file_ops || !file->file_ops->readdir)
        return -1;

    read_lock(&vfs_mgr.namespace_lock);
    mutex_lock(&file->lock);
    read_lock(&file->inode->i_meta_lock);
    int ret = file->file_ops->readdir(file, dirp, count);
    read_unlock(&file->inode->i_meta_lock);
    mutex_unlock(&file->lock);
    read_unlock(&vfs_mgr.namespace_lock);
    return ret;
}
