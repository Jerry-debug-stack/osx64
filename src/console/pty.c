#include "const.h"
#include "fs/fs.h"
#include "fs/pty.h"
#include "fs/fcntl.h"
#include "mm/mm.h"
#include "lib/io.h"
#include "lib/string.h"
#include "view/view.h"

pty_pair_t *pty_pairs;

static void ldisc_init(struct line_discipline *ld)
{
    memset(ld, 0, sizeof(*ld));
    spin_lock_init(&ld->lock);
    ld->termios.c_lflag = ECHO | ICANON | ISIG;
}

static void ldisc_echo(struct pty_pair *pair, const char *data, size_t len)
{
    // 将数据写入 s2m 缓冲区（回显）
    spin_lock(&pair->lock);
    for (size_t i = 0; i < len; i++) {
        uint32_t next = (pair->s2m_head + 1) % PTY_BUFSIZE;
        if (next == pair->s2m_tail) break; // 缓冲区满，丢弃
        pair->s2m_buf[pair->s2m_head] = data[i];
        pair->s2m_head = next;
    }
    spin_unlock(&pair->lock);
}

static void ldisc_input(struct pty_pair *pair, const char *data, size_t len)
{
    struct line_discipline *ld = &pair->ldisc;
    spin_lock(&ld->lock);

    for (size_t i = 0; i < len; i++) {
        char c = data[i];
        if (c == '\n') {
            // 回车：提交当前行
            if (ld->line_pos > 0) {
                char *line = kmalloc(ld->line_pos + 1);
                if (line) {
                    memcpy(line, ld->line_buf, ld->line_pos);
                    line[ld->line_pos] = '\0';
                    int next = (ld->completed_head + 1) % LINE_QUEUE_SIZE;
                    if (next != ld->completed_tail) {
                        ld->completed_lines[ld->completed_head] = line;
                        ld->completed_head = next;
                    } else {
                        kfree(line); // 队列满，丢弃
                    }
                }
                ld->line_pos = 0;
            }
            if (ld->termios.c_lflag){
                ldisc_echo(pair, "\n", 1);
            }
        } else if (c == '\b') {
            // 退格
            if (ld->line_pos > 0) {
                ld->line_pos--;
                if (ld->termios.c_lflag){
                    ldisc_echo(pair, "\b", 1);
                }
            }
        } else {
            // 普通字符
            if (ld->line_pos < LINE_MAX - 1) {
                ld->line_buf[ld->line_pos++] = c;
                if (ld->termios.c_lflag & ECHO) {
                    ldisc_echo(pair, &c, 1);
                }
            }
        }
        if ((ld->termios.c_lflag & ISIG) && c == ld->termios.c_cc[VINTR]) {
            
        }
    }
    spin_unlock(&ld->lock);
}

static int pty_master_open(struct inode *inode, struct file *file)
{
    int idx = (int)(uintptr_t)inode->private_data;
    if (idx < 0 || idx >= PTY_COUNT)
        return -1;
    pty_pair_t *pair = &pty_pairs[idx];

    spin_lock(&pair->lock);
    if (pair->master_ref == 0) {
        // 首次打开，初始化
        pair->allocated = true;
        pair->m2s_head = pair->m2s_tail = 0;
        pair->s2m_head = pair->s2m_tail = 0;
        ldisc_init(&pair->ldisc);
    }
    pair->master_ref++;
    spin_unlock(&pair->lock);

    file->private_data = pair;
    return 0;
}

static ssize_t pty_master_read(struct file *file, char *buf, size_t len, UNUSED loff_t *off)
{
    pty_pair_t *pair = file->private_data;
    size_t count = 0;

    uint8_t intr = io_cli();
    spin_lock(&pair->lock);
    while (count < len && pair->s2m_head != pair->s2m_tail) {
        buf[count++] = pair->s2m_buf[pair->s2m_tail];
        pair->s2m_tail = (pair->s2m_tail + 1) % PTY_BUFSIZE;
    }
    spin_unlock(&pair->lock);
    io_set_intr(intr);

    // 如果没有数据可读，返回0（非阻塞）
    return count;
}

static ssize_t pty_master_write(struct file *file, const char *buf, size_t len, UNUSED loff_t *off)
{
    struct pty_pair *pair = file->private_data;
    ldisc_input(pair, buf, len);
    return len;
}

static int pty_master_release(UNUSED struct inode *inode, struct file *file)
{
    pty_pair_t *pair = file->private_data;

    spin_lock(&pair->lock);
    pair->master_ref--;
    if (pair->master_ref == 0 && pair->slave_ref == 0) {
        pair->allocated = false; // 可重用
    }
    spin_unlock(&pair->lock);
    return 0;
}

static struct file_operations pty_master_fops = {
    .open   = pty_master_open,
    .read   = pty_master_read,
    .write  = pty_master_write,
    .release = pty_master_release,
    .readdir = NULL
};

static int pty_slave_open(struct inode *inode, struct file *file)
{
    int idx = (int)(uintptr_t)inode->private_data;
    if (idx < 0 || idx >= PTY_COUNT)
        return -1;
    pty_pair_t *pair = &pty_pairs[idx];

    spin_lock(&pair->lock);
    if (!pair->allocated || pair->master_ref == 0) {
        spin_unlock(&pair->lock);
        return -1;
    }
    pair->slave_ref++;
    spin_unlock(&pair->lock);

    file->private_data = pair;
    return 0;
}

static ssize_t pty_slave_read(struct file *file, char *buf, size_t len, UNUSED loff_t *off)
{
    struct pty_pair *pair = file->private_data;
    struct line_discipline *ld = &pair->ldisc;
    size_t count = 0;

    spin_lock(&ld->lock);
    if (ld->completed_head == ld->completed_tail) {
        spin_unlock(&ld->lock);
        return 0; // 无数据，非阻塞
    }
    char *line = ld->completed_lines[ld->completed_tail];
    int line_len = strlen(line);
    if ((unsigned long)line_len > len) {
        // 如果用户缓冲区不够大，只拷贝前 len 字节（通常应处理剩余，但简化）
        memcpy(buf, line, len);
        memmove(line, line + len, line_len - len);
        count = len;
    } else {
        memcpy(buf, line, line_len);
        kfree(line);
        ld->completed_tail = (ld->completed_tail + 1) % LINE_QUEUE_SIZE;
        count = line_len;
    }
    spin_unlock(&ld->lock);
    return count;
}

static ssize_t pty_slave_write(struct file *file, const char *buf, size_t len, UNUSED loff_t *off)
{
    pty_pair_t *pair = file->private_data;
    size_t count = 0;

    uint8_t intr = io_cli();
    spin_lock(&pair->lock);
    while (count < len) {
        uint32_t next = (pair->s2m_head + 1) % PTY_BUFSIZE;
        if (next == pair->s2m_tail)
            break;
        pair->s2m_buf[pair->s2m_head] = buf[count++];
        pair->s2m_head = next;
    }
    spin_unlock(&pair->lock);
    io_set_intr(intr);
    return count;
}

static int pty_slave_release(UNUSED struct inode *inode, struct file *file)
{
    pty_pair_t *pair = file->private_data;

    spin_lock(&pair->lock);
    pair->slave_ref--;
    if (pair->master_ref == 0 && pair->slave_ref == 0) {
        pair->allocated = false;
    }
    spin_unlock(&pair->lock);
    return 0;
}

static struct file_operations pty_slave_fops = {
    .open   = pty_slave_open,
    .read   = pty_slave_read,
    .write  = pty_slave_write,
    .release = pty_slave_release,
};

extern int devfs_chr_register(const char *name, int mode, struct file_operations *fops, void *private_data, uint64_t flags, bool locked);

void pty_init(void)
{
    pty_pairs = kmalloc(sizeof(pty_pair_t) * PTY_COUNT);
    memset(pty_pairs,0,sizeof(pty_pair_t) * PTY_COUNT);
    // 创建设备文件（假设VFS有mknod系统调用）
    for (int i = 0; i < PTY_COUNT; i++) {
        char name[32];
        sprintf(name, "pty_m%d", sizeof(name), i);
        devfs_chr_register(name, 0666, &pty_master_fops, (void*)(uintptr_t)i, DENTRY_CHARACTER_DEV, false);

        sprintf(name, "pty_s%d", sizeof(name), i);
        devfs_chr_register(name, 0666, &pty_slave_fops, (void*)(uintptr_t)i, DENTRY_CHARACTER_DEV, false);
    }
}
