#include "fs/fs.h"
#include "fs/pipe.h"
#include "fs/fcntl.h"
#include "const.h"
#include "mm/mm.h"
#include "task.h"

int fd_alloc(pcb_t *proc);

static int pipe_delete(struct inode *dir){
    if (dir->private_data)
        kfree(dir->private_data);
    return 0;
}

ssize_t pipe_read(struct file *file, char __user *buf, size_t len, UNUSED int64_t *ppos)
{
    pipe_t *pipe = file->inode->private_data;
    size_t count = 0;
    uint32_t newly_read = 0;

    spin_lock(&pipe->lock);
    while (count < len) {
        if (pipe->buf_head == pipe->buf_tail) {
            if (pipe->single)
                break;
            if (newly_read)
                wake_up_all(&pipe->write_wq);
            newly_read = 0;
            spin_lock(&pipe->read_wq.lock);
            spin_unlock(&pipe->lock);
            sleep_on_locked(&pipe->read_wq);
            spin_lock(&pipe->lock);
            continue;
        }
        buf[count++] = pipe->buf[pipe->buf_tail];
        pipe->buf_tail = (pipe->buf_tail + 1) % PIPE_BUFSIZE;
        newly_read++;
    }
    spin_unlock(&pipe->lock);
    return count;
}

ssize_t pipe_write(struct file *file, const char __user *buf,size_t len, UNUSED int64_t *ppos){
    pipe_t *pipe = file->inode->private_data;
    size_t count = 0;
    uint32_t newly_write = 0;

    spin_lock(&pipe->lock);
    while (count < len) {
        uint32_t next = (pipe->buf_head + 1) % PIPE_BUFSIZE;
        if (next == pipe->buf_tail) {
            if (pipe->single)
                break;
            if (newly_write)
                wake_up_all(&pipe->read_wq);
            newly_write = 0;
            spin_lock(&pipe->write_wq.lock);
            spin_unlock(&pipe->lock);
            sleep_on_locked(&pipe->write_wq);
            spin_lock(&pipe->lock);
            continue;
        }
        pipe->buf[pipe->buf_head] = buf[count++];
        pipe->buf_head = next;
        newly_write++;
    }
    spin_unlock(&pipe->lock);
    return count;
}

static int pipe_release(struct inode *inode, UNUSED struct file *file){
    pipe_t *pipe = inode->private_data;
    if (pipe->single)
        return 0;
    pipe->single = true;
    // 总之肯定有一端空出了,空出的这一端的wq一定为空
    wake_up_all(&pipe->read_wq);
    wake_up_all(&pipe->write_wq);
    return 0;
}

static inode_operations_t pipe_inode_ops = {
    .create = NULL,
    .delete = pipe_delete,
    .lookup = NULL,
    .mkdir = NULL,
    .rename = NULL,
    .rmdir = NULL,
    .setattr = NULL,
    .unlink = NULL  
};

static file_operations_t pipe_file_ops = {
    .fsync = NULL,
    .open = NULL,
    .read = pipe_read,
    .readdir = NULL,
    .release = pipe_release,
    .write = pipe_write
};

static pipe_t *new_pipe(void){
    pipe_t *new = kmalloc(sizeof(pipe_t));
    if (!new)
        return NULL;
    new->single = false;
    spin_lock_init(&new->lock);
    new->buf_head = new->buf_tail = 0;
    wait_queue_init(&new->read_wq);
    wait_queue_init(&new->write_wq);
    return new;
}

static inode_t *pipe_new_inode(pipe_t *pipe){
    if (!pipe)
        return NULL;
    inode_t *new = kmalloc(sizeof(inode_t));
    if (!new)
        return NULL;
    
    new->ino = 0;
    new->size = 0;
    new->sb = NULL;
    new->deleting = false;
    new->mode = DT_FIFO;

    new->default_file_ops = &pipe_file_ops;
    new->inode_ops = &pipe_inode_ops;
    new->private_data = pipe;

    mutex_init(&new->i_data_lock);
    rwlock_init(&new->i_meta_lock);
    
    atomic_set(&new->refcount,2);
    atomic_set(&new->link_count,1);
    INIT_LIST_HEAD(&new->lru_node);
    return new;
}

int sys_pipe(int pipe_buf[2]){
    ///@todo eval addr valid
    pcb_t *current = get_current();
    int a = fd_alloc(current);
    if (a >= 0)
        current->files[a] = (void *)114514;
    int b = fd_alloc(current);
    if (a == -1 || b == -1)
        goto out_fd;
    pipe_t *pipe = new_pipe();
    if (!pipe)
        goto out_fd;
    inode_t *inode = pipe_new_inode(pipe);
    if (!inode)
        goto out_pipe;
    file_t *file1 = kmalloc(sizeof(file_t));
    if (!file1)
        goto out_inode;
    file1->dentry = NULL;
    file1->file_ops = &pipe_file_ops;
    file1->flags = O_RDONLY;
    file1->inode = inode;
    mutex_init(&file1->lock);
    file1->pos = 0;
    file1->private_data = NULL;
    atomic_set(&file1->refcount,1);
    file_t *file2 = kmalloc(sizeof(file_t));
    if (!file2)
        goto out_file1;
    file2->dentry = NULL;
    file2->file_ops = &pipe_file_ops;
    file2->flags = O_WRONLY;
    file2->inode = inode;
    mutex_init(&file2->lock);
    file2->pos = 0;
    file2->private_data = NULL;
    atomic_set(&file2->refcount,1);

    current->files[a] = file1;
    current->files[b] = file2;

    pipe_buf[0] = a;
    pipe_buf[1] = b;
    return 0;
out_file1:
    kfree(file1);
out_inode:
    kfree(inode);
out_pipe:
    kfree(pipe);
out_fd:
    if (a != -1)
        current->files[a] = NULL;
    if (b != -1)
        current->files[b] = NULL;
    return -1;
}
