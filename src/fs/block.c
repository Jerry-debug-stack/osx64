#include "mm/mm.h"
#include "fs/block.h"
#include "fs/devfs.h"
#include "fs/fcntl.h"
#include "fs/fs.h"
#include "lib/string.h"
#include "view/view.h"
#include "lib/io.h"
#include "const.h"

static block_manager_t block_mgr;

extern void ext2_read_uuid(partition_t *part);
extern int ext2_write_uuid(partition_t *part, const char *uuid_str);

static int block_file_open(UNUSED struct inode *inode,UNUSED struct file *file);
static int block_file_release(UNUSED struct inode *inode,UNUSED struct file *file);
static ssize_t block_file_read(struct file *file, char __user *buf,size_t len, int64_t *ppos);
static ssize_t block_file_write(struct file *file, const char __user *buf,size_t len, int64_t *ppos);
static int block_file_fsync(UNUSED struct file *file);
static int block_file_readdir(UNUSED struct file *file, UNUSED struct dirent __user *dirp, UNUSED unsigned int count);

struct file_operations block_file_ops = {
    .fsync = block_file_fsync,
    .open = block_file_open,
    .read = block_file_read,
    .release = block_file_release,
    .write = block_file_write,
    .readdir = block_file_readdir
};

static inline void disk_index_to_name(uint64_t index, char *buf);
static inline uint64_t alloc_block_id(void);
static inline uint64_t alloc_device_id(void);
static void load_primary_partition(block_device_t* disk,mbr_entry_t* ent,int index,bool locked);

void init_block(void)
{
    block_mgr.block_device_id = 0;
    block_mgr.global_block_id = 0;
    spin_list_init(&block_mgr.block_list);
    spin_list_init(&block_mgr.device_list);
}

int block_register(block_device_t *dev,bool locked)
{
    dev->id = alloc_block_id();
    INIT_LIST_HEAD(&dev->global_list);
    uint8_t intr = io_cli();
    spin_list_add_tail(&dev->global_list, &block_mgr.block_list);
    if (dev->type == BLOCK_DISK)
    {
        real_device_t *real_device = (real_device_t *)dev;
        spin_list_init(&real_device->childs);
        INIT_LIST_HEAD(&real_device->device_list_item);
        spin_list_add_tail(&real_device->device_list_item, &block_mgr.device_list);
        char buf[18];
        disk_index_to_name(alloc_device_id(), &buf[0]);
        sprintf(&dev->name[0], "sd%s", 18, &buf[0]);
        io_set_intr(intr);
        wb_printf("[ BLOCK ] block device %lu named %s registered (%lu blocks)\n", dev->id, &dev->name[0], dev->total_blocks);
    }
    else
    {
        partition_t *partition = (partition_t *)dev;
        real_device_t *real_device = (real_device_t *)partition->parent;
        INIT_LIST_HEAD(&partition->childs_list_item);
        spin_list_add_tail(&partition->childs_list_item, &real_device->childs);
        io_set_intr(intr);
    }
    devfs_block_register(dev->name,O_RDWR,&block_file_ops,dev,DENTRY_BLOCK_DEV,locked);
    return 0;
}

int block_unregister(block_device_t *dev){
    if (dev->type == BLOCK_DISK){
        return -1;
    }else{
        partition_t *partition = (partition_t *)dev;
        list_del(&partition->childs_list_item);
        kfree(dev);
        return 0;
    }
}

static inline uint64_t alloc_block_id(void)
{
    return __sync_fetch_and_add(&block_mgr.global_block_id, 1);
}

static inline uint64_t alloc_device_id(void)
{
    return __sync_fetch_and_add(&block_mgr.block_device_id, 1);
}

static inline void disk_index_to_name(uint64_t index, char *buf)
{
    char tmp[16];
    int i = 0;

    do
    {
        tmp[i++] = 'a' + (index % 26);
        index /= 26;
        if (index == 0)
            break;
        index--;
    } while (1);

    int j = 0;
    while (i--)
        buf[j++] = tmp[i];
    buf[j] = '\0';
}

int partition_read(block_device_t *dev,uint64_t lba,uint32_t cnt,void *buf)
{
    if (dev->type == BLOCK_PARTITION){
        partition_t *part = (partition_t *)dev;
        if (lba + cnt > part->sector_count)
            return -1;
        return part->parent->read(part->parent,part->start_lba + lba,cnt,buf);
    }else{
        return dev->read(dev,lba,cnt,buf);
    }
}

int partition_write(block_device_t *dev,uint64_t lba,uint32_t cnt,const void *buf)
{
    if (dev->type == BLOCK_PARTITION){
        partition_t *part = (partition_t *)dev;
        if (lba + cnt > part->sector_count)
            return -1;
        return part->parent->write(part->parent,part->start_lba + lba,cnt,buf);
    }else{
        return dev->write(dev,lba,cnt,buf);
    }
}

static int block_file_open(UNUSED struct inode *inode,UNUSED struct file *file){
    return 0;
}

static int block_file_release(UNUSED struct inode *inode,UNUSED struct file *file){
    return 0;
}

static ssize_t block_file_read(struct file *file, char __user *buf,size_t len, int64_t *ppos){
    if (!file || !file->inode || !file->inode->private_data)
        return -1;
    block_device_t *dev = file->inode->private_data;
    ssize_t ret = partition_read(dev,*ppos,len,buf);
    if (ret == 0){
        *ppos += len;
        return len;
    }else{
        return -1;
    }
}

static ssize_t block_file_write(struct file *file, const char __user *buf,size_t len, int64_t *ppos){
    if (!file || !file->inode || !file->inode->private_data)
        return -1;
    block_device_t *dev = file->inode->private_data;
    ssize_t ret = partition_write(dev,*ppos,len,buf);
    if (ret == 0){
        *ppos += len;
        return len;
    }else{
        return -1;
    }
}

static int block_file_fsync(UNUSED struct file *file){
    return 0;
}

static int block_file_readdir(UNUSED struct file *file, UNUSED struct dirent __user *dirp, UNUSED unsigned int count){
    return -1;
}

static void read_uuid(partition_t *part){
    switch (part->part_type)
    {
    case PARTITION_LINUX:
        ext2_read_uuid(part);
        return;
    default:
        return;
    }
}

int write_uuid(partition_t *part, char uuid[37]){
    char *new_uuid = kmalloc(37);
    if (!new_uuid){
        return -1;
    }
    memcpy(new_uuid, uuid, 37);
    switch (part->part_type)
    {
    case PARTITION_LINUX:
        int ret = ext2_write_uuid(part, (void *)uuid);
        if (!ret){
            if (part->uuid)
                kfree(part->uuid);
            part->uuid = new_uuid;
            return 0;
        }else{
            kfree(new_uuid);
            return ret;
        }
    default:
        kfree(new_uuid);
        return -1;
    }
}

static void load_primary_partition(block_device_t* disk,mbr_entry_t* ent,int index,bool locked)
{
    if (ent->sector_count == 0)
        return;

    partition_t* part = kmalloc(sizeof(partition_t));
    memset(part, 0, sizeof(partition_t));

    part->parent       = disk;
    part->start_lba    = ent->start_lba;
    part->sector_count = ent->sector_count;
    part->part_type    = ent->type;
    part->bootable     = ent->bootable;
    part->mounted_sb   = NULL;
    part->uuid         = NULL;

    part->device.type         = BLOCK_PARTITION;
    part->device.block_size   = disk->block_size;
    part->device.total_blocks = (uint64_t)ent->sector_count;
    part->device.read         = partition_read;
    part->device.write        = partition_write;
    part->device.private_data = part;

    sprintf(&part->device.name[0],"%s%d",18,&part->parent->name[0],index + 1);

    block_register(&part->device,locked);

    read_uuid(part);

    wb_printf("[  MBR  ] loaded primary partition %s start=%u sectors=%u type=0x%x uuid=%s \n",
        part->device.name,
        ent->start_lba,
        ent->sector_count,
        ent->type,
        part->uuid ? part->uuid : "unknown"
    );
}

static int is_extended_type(uint8_t type)
{
    return (type == 0x05 || type == 0x0F || type == 0x85);
}

static void scan_extended(block_device_t* disk,uint32_t ext_base_lba,bool locked)
{
    uint8_t buffer[512];
    uint32_t current_ebr = ext_base_lba;
    uint32_t next_ebr_relative = 0;
    int logical_index = 5;   // 逻辑分区从5开始
    int safety = 0;
    while (1)
    {
        if (safety++ > 32) {
            wb_printf("[  MBR  ] extended chain too long, abort\n");
            return;
        }
        if (disk->read(disk, current_ebr, 1, buffer) != 0) {
            wb_printf("[  MBR  ] failed reading EBR\n");
            return;
        }
        mbr_t* ebr = (mbr_t*)buffer;
        if (ebr->signature != MBR_SIGNATURE) {
            wb_printf("[  MBR  ] invalid EBR signature\n");
            return;
        }
        mbr_entry_t* part_entry = &ebr->entry[0];
        mbr_entry_t* next_entry = &ebr->entry[1];
        if (part_entry->type != 0 &&
            part_entry->sector_count != 0)
        {
            uint32_t abs_start =
                current_ebr + part_entry->start_lba;

            partition_t* part = kmalloc(sizeof(partition_t));
            memset(part, 0, sizeof(partition_t));

            part->parent       = disk;
            part->start_lba    = abs_start;
            part->sector_count = part_entry->sector_count;
            part->part_type    = part_entry->type;
            part->bootable     = part_entry->bootable;
            part->mounted_sb   = NULL;
            part->uuid         = NULL;

            part->device.type         = BLOCK_PARTITION;
            part->device.block_size   = disk->block_size;
            part->device.total_blocks = (uint64_t)part_entry->sector_count;
            part->device.read         = partition_read;
            part->device.write        = partition_write;
            part->device.private_data = part;
            sprintf(part->device.name,"%s%d",(uint32_t)sizeof(part->device.name),disk->name,logical_index++);
            block_register(&part->device,locked);

            read_uuid(part);

            wb_printf("[  MBR  ] loaded logical partition %s start=%u size=%u type=0x%x uuid=%s\n",
                part->device.name,
                abs_start,
                part_entry->sector_count,
                part_entry->type,
                part->uuid ? part->uuid : "unknown"
            );
        }
        if (next_entry->type == 0 ||
            next_entry->sector_count == 0)
        {
            break;
        }

        next_ebr_relative = next_entry->start_lba;
        current_ebr = ext_base_lba + next_ebr_relative;
    }
}

void read_partitions(void){
    list_head_t *pos;
    list_for_each(pos,&block_mgr.device_list.list){
        real_device_t *device = container_of(pos,real_device_t,device_list_item);
        mbr_scan(&device->device,false);
    }
}

int mbr_scan(block_device_t* disk,bool locked)
{
    uint8_t buffer[512];
    if (disk->block_size != 512) {
        wb_printf("[  MBR  ] unsupported sector size\n");
        return -1;
    }
    if (disk->read(disk, 0, 1, buffer) != 0) {
        wb_printf("[  MBR  ] failed to read LBA0\n");
        return -1;
    }
    mbr_t* mbr = (mbr_t*)buffer;
    if (mbr->signature != MBR_SIGNATURE) {
        wb_printf("[  MBR  ] invalid signature\n");
        return -1;
    }
    for (int i = 0; i < MBR_PART_COUNT; i++) {
        if (mbr->entry[i].type == GPT_PROTECTIVE_TYPE) {
            wb_printf("[  MBR  ] GPT detected — not supported\n");
            return -1;
        }
    }
    for (int i = 0; i < MBR_PART_COUNT; i++) {
        mbr_entry_t* ent = &mbr->entry[i];
        if (ent->type == 0)
            continue;
        if (is_extended_type(ent->type)) {
            wb_printf("[  MBR  ] extended found at %u\n",ent->start_lba);
            scan_extended(disk, ent->start_lba,locked);
        }
        else {
            load_primary_partition(disk, ent, i,locked);
        }
    }
    return 0;
}

extern char rootuuid[37];

void mount_root(void){
    block_device_t *dev;
    partition_t *target = NULL; 
    list_for_each_entry(dev,&block_mgr.block_list.list,global_list){
        if (dev->type == BLOCK_PARTITION){
            partition_t *part = (void*)dev;
            if (part->uuid && !strncmp((const char *)part->uuid,(const char *)rootuuid,37)){
                target = part;
                break;
            }
        }
    }
    if (!target){
        color_printf("[ PANIC ] root uuid(%s) not found!!!\n",VIEW_COLOR_RED,VIEW_COLOR_WHITE,rootuuid);
    }else{
        if (vfs_mount(target,"/root",0)){
            color_printf("[ PANIC ] mount root uuid(%s) failed!!!\n",VIEW_COLOR_RED,VIEW_COLOR_WHITE,rootuuid);
        }else{
            wb_printf("[  VFS  ] root mount successfully (uuid %s)\n",rootuuid);
        }
    }
}
