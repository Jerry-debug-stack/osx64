#include "mm/mm.h"
#include "fs/fs.h"
#include "fs/block.h"
#include "fs/fsmod.h"
#include "lib/string.h"
#include "view/view.h"
#include "const.h"
#include "task.h"
#include "fcntl.h"

#include "fs/ramfs.h"

void init_block(void);

extern void init_cwd_for_started_tasks(struct dentry *root);

static inline void init_vfs_mgr(void);
static dentry_t *lookup_child(dentry_t *parent, const char *name);
static inline void mount_root_ramfs(void);

struct file *vfs_open(const char *path, int flags, int mode);
ssize_t vfs_read(struct file *file, char *buf, size_t count);
ssize_t vfs_write(struct file *file, const char *buf, size_t count);
int vfs_close(struct file *file);

int sys_open(const char *path, int flags, int mode);
ssize_t sys_read(int fd, char *buf, size_t count);
ssize_t sys_write(int fd, const char *buf, size_t count);
int sys_close(int fd);

vfs_manager_t vfs_mgr;

void test_filesystem(void)
{
    int fd = sys_open("/test.txt", O_CREAT | O_RDWR, 0644);
    if (fd < 0) {
        wb_printf("test: open failed\n");
        return;
    }
    const char *msg = "Hello, VFS!";
    sys_write(fd, msg, strlen(msg));
    sys_close(fd);
    fd = sys_open("/test.txt", O_RDWR, 0644);
    if (fd < 0) {
        wb_printf("test: open failed\n");
        return;
    }
    char buf[128];
    sys_read(fd, buf, sizeof(buf));
    wb_printf("test: read: %s\n", buf);
    sys_close(fd);
}

void init_fs_mem(void){
    init_block();
    init_vfs_mgr();
    test_filesystem();
}

static inline void init_vfs_mgr(void){
    spin_list_init(&vfs_mgr.inode_list);
    spin_list_init(&vfs_mgr.mount_list);
    spin_list_init(&vfs_mgr.dentry_lru_list);
    rwlock_init(&vfs_mgr.namespace_lock);
    mount_root_ramfs();
    init_cwd_for_started_tasks(vfs_mgr.root);
}

static inline void mount_root_ramfs(void)
{
    super_block_t *sb;
    inode_t *root_inode;
    dentry_t *root_dentry;
    sb = super_block_create(NULL, FS_TYPE_RAMFS);
    if (!sb) {
        wb_printf("mount_root_ramfs: failed to create superblock\n");
        halt();
    }
    if (mount_fs(sb) != 0) {
        wb_printf("mount_root_ramfs: mount_fs failed\n");
        halt();
    }
    if (!sb->super_ops || !sb->super_ops->read_root_inode) {
        wb_printf("mount_root_ramfs: no read_root_inode operation\n");
        halt();
    }
    root_inode = sb->super_ops->read_root_inode(sb);
    if (!root_inode) {
        wb_printf("mount_root_ramfs: read_root_inode failed\n");
        halt();
    }
    root_dentry = dentry_create("/", root_inode);
    if (!root_dentry) {
        wb_printf("mount_root_ramfs: failed to create root dentry\n");
        halt();
    }
    dentry_get(root_dentry);
    vfs_mgr.root = root_dentry;
    wb_printf("[  VFS  ] Ramfs mounted as root (root ino=%d)\n", root_inode->ino);
}

dentry_t *__vfs_lookup_locked(dentry_t *start,const char *target_path)
{
    if (!target_path || target_path[0] == '\0')
        return NULL;

    dentry_t *current;

    /* 绝对路径 */
    if (target_path[0] == '/' || start == NULL)
        current = vfs_mgr.root;
    else
        current = start;   /* 相对路径 */

    const char *p = target_path;

    while (*p) {

        while (*p == '/')
            p++;

        if (*p == '\0')
            break;

        char name[256];
        int len = 0;

        while (*p && *p != '/') {
            if (len < 255)
                name[len++] = *p;
            p++;
        }
        name[len] = '\0';

        /* "." */
        if (strcmp((const char*)name, ".") == 0)
            continue;

        /* ".." */
        if (strcmp((const char*)name, "..") == 0) {
            if (current->in_mnt && current == current->in_mnt->root) {
                if (current->in_mnt->mountpoint)
                    current = current->in_mnt->mountpoint;
            } else if (current->parent) {
                current = current->parent;
            }
            continue;
        }

        dentry_t *child = lookup_child(current, name);
        if (!child)
            return NULL;

        /* mount 跳转 */
        if (child->flags & DENTRY_FLAG_MOUNTPOINT)
            current = child->mounted_here->root;
        else
            current = child;
    }
    if (current)
        dentry_get(current);
    return current;
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
    }else{
        sb->fs_type = fstype;
    }
    atomic_set(&sb->active_ref, 1);
    sb->shutting_down = false;
    spin_list_init(&sb->inode_list);
    spin_list_init(&sb->dentry_lru);
    sb->root_mount = NULL;
    rwlock_init(&sb->sb_lock);
    sb->super_ops = NULL;
    return sb;
}

uint8_t mount_fs(super_block_t *sb){
    if (!sb->fs_type)
        return -1;
    switch (sb->fs_type)
    {
    case FS_TYPE_RAMFS:
        sb->super_ops = &ramfs_super_ops;
        return 0;
    case PARTITION_LINUX:
        return -1;
    
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
    spin_lock_init(&d->d_lock);
    d->flags = 0;
    d->dentry_ops = NULL;
    d->in_mnt = NULL;
    d->mounted_here = NULL;
    d->negative = false;

    INIT_LIST_HEAD(&d->child_list);
    INIT_LIST_HEAD(&d->child_list_item);

    atomic_set(&d->refcount, 1);

    // 🔑 dentry 持有 inode
    if (inode){
        d->inode = inode;
        atomic_inc(&inode->refcount);
    }
    return d;
}

/// @warning with namespace lock
void dentry_put(dentry_t *dentry){
    if (!dentry)
        return;
    if (!atomic_dec_and_test(&dentry->refcount))
        return;
    if (dentry->parent) {
        inode_t *parent_inode = dentry->parent->inode;

        write_lock(&parent_inode->i_meta_lock);
        list_del(&dentry->child_list_item);
        write_unlock(&parent_inode->i_meta_lock);
        dentry_put(dentry->parent);
    }
    if (dentry->inode) {
        inode_put(dentry->inode);
    }
    if (dentry->name)
        kfree(dentry->name);
    kfree(dentry);
}

void inode_put(inode_t *inode){
    if (!inode)
        return;

    if (!atomic_dec_and_test(&inode->refcount))
        return;

    if (atomic_read(&inode->link_count) == 0) {
        if (inode->inode_ops && inode->inode_ops->delete)
            inode->inode_ops->delete(inode);
    }

    if (inode->sb)
        sb_put(inode->sb);
    
    kfree(inode);
}

static int super_block_destroy(super_block_t *sb){
    if (!sb)
        return -1;
    sb->shutting_down = true;
    if (atomic_read(&sb->active_ref) != 0) {
        return -1;
    }
    if (sb->super_ops && sb->super_ops->put_super)
        sb->super_ops->put_super(sb);
    kfree(sb);
    return 0;
}

int vfs_mount(partition_t *part, const char *target_path, int fstype)
{
    dentry_t *mountpoint = NULL;
    super_block_t *sb = NULL;
    inode_t *root_inode = NULL;
    dentry_t *root = NULL;
    mount_t *mnt = NULL;
    int ret = -1;

    write_lock(&vfs_mgr.namespace_lock);

    mountpoint = __vfs_lookup_locked(NULL, target_path);
    if (!mountpoint) {
        wb_printf("vfs_mount: mountpoint not found\n");
        goto out_unlock;
    }

    if (mountpoint->flags & DENTRY_FLAG_MOUNTPOINT) {
        wb_printf("vfs_mount: already a mountpoint\n");
        goto out_put_mountpoint;
    }

    // 如果有设备，检查是否已挂载
    if (part && part->mounted_sb) {
        wb_printf("vfs_mount: partition already mounted\n");
        goto out_put_mountpoint;
    }

    // 创建超级块，传入设备指针和文件系统类型
    if (part)
        sb = super_block_create(part, 0);       // 设备非空，fstype 参数被忽略
    else
        sb = super_block_create(NULL, fstype);  // 无设备，使用传入的 fstype

    if (!sb) {
        wb_printf("vfs_mount: failed to create superblock\n");
        goto out_put_mountpoint;
    }

    if (mount_fs(sb)) {
        wb_printf("vfs_mount: mount_fs failed\n");
        goto out_destroy_sb;
    }

    if (!sb->super_ops || !sb->super_ops->read_root_inode) {
        wb_printf("vfs_mount: no read_root_inode op\n");
        goto out_destroy_sb;
    }

    root_inode = sb->super_ops->read_root_inode(sb);
    if (!root_inode) {
        wb_printf("vfs_mount: read_root_inode failed\n");
        goto out_destroy_sb;
    }

    root = dentry_create("/", root_inode);
    if (!root) {
        wb_printf("vfs_mount: failed to create root dentry\n");
        goto out_put_inode;
    }

    mnt = kmalloc(sizeof(mount_t));
    if (!mnt) {
        wb_printf("vfs_mount: no memory for mount\n");
        goto out_put_root;
    }
    memset(mnt, 0, sizeof(mount_t));
    mnt->sb = sb;
    mnt->root = root;
    mnt->mountpoint = mountpoint;
    atomic_set(&mnt->refcount, 1);
    mnt->detached = false;
    mnt->part = part;   // 可能为 NULL

    spin_lock(&mountpoint->d_lock);
    mountpoint->flags |= DENTRY_FLAG_MOUNTPOINT;
    mountpoint->mounted_here = mnt;
    spin_unlock(&mountpoint->d_lock);

    if (part)
        part->mounted_sb = sb;

    list_add_tail(&mnt->mount_list, &vfs_mgr.mount_list.list);

    ret = 0;
    write_unlock(&vfs_mgr.namespace_lock);
    return 0;

out_put_root:
    dentry_put(root);
out_put_inode:
    inode_put(root_inode);
out_destroy_sb:
    super_block_destroy(sb);
out_put_mountpoint:
    dentry_put(mountpoint);
out_unlock:
    write_unlock(&vfs_mgr.namespace_lock);
    return ret;
}

static dentry_t *lookup_child(dentry_t *parent, const char *name)
{
    struct list_head *pos;
    list_for_each(pos, &parent->child_list) {
        dentry_t *child = container_of(pos, dentry_t, child_list_item);
        if (strcmp((const char*)child->name, name) == 0)
            return child;
    }
    return NULL;
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
    return -1;
    return vfs_close(file);  // 假设 vfs_close 释放 file 结构并返回 0 成功
}

void dentry_set_parent(dentry_t *parent,dentry_t *child){
    if (!parent||!child)
        return;
    dentry_get(parent);
    write_lock(&parent->inode->i_meta_lock);
    list_add(&child->child_list_item,&parent->child_list);
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
    dentry_get(parent); // dentry 持有 parent 引用
    dentry->parent = parent;

    // 调用父目录的 create 方法
    if (!parent->inode->inode_ops || !parent->inode->inode_ops->create) {
        dentry_put(dentry);
        dentry_put(parent);
        return NULL;
    }
    ret = parent->inode->inode_ops->create(parent->inode, dentry, mode);
    if (ret < 0) {
        dentry_put(dentry);
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

    // 处理 O_TRUNC 等（暂略）
    return file;
}

ssize_t vfs_read(struct file *file, char *buf, size_t count)
{
    if (!file || !file->file_ops || !file->file_ops->read)
        return -1;
    return file->file_ops->read(file, buf, count, (int64_t*)&file->pos);
}

ssize_t vfs_write(struct file *file, const char *buf, size_t count)
{
    if (!file || !file->file_ops || !file->file_ops->write)
        return -1;
    return file->file_ops->write(file, buf, count, (int64_t*)&file->pos);
}

int vfs_close(struct file *file)
{
    if (!file)
        return -1;
    if (!atomic_dec_and_test(&file->refcount))
        return 0;  // 还有其他引用

    // 调用 release 方法
    if (file->file_ops && file->file_ops->release)
        file->file_ops->release(file->inode, file);

    // 释放 dentry 引用
    dentry_put(file->dentry);

    // 释放 file 结构自身
    kfree(file);
    return 0;
}

int sys_open(const char *path, int flags, int mode)
{
    struct file *file = vfs_open(path, flags, mode);
    if (!file)
        return -1;

    pcb_t *current = get_current();  // 假设有获取当前进程的函数
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
