#ifndef OS_BLOCK_H
#define OS_BLOCK_H

#include <stdint.h>
#include "lib/safelist.h"

#define BLOCK_DEV_MAX 32

enum block_device_type{
    BLOCK_DISK,
    BLOCK_PARTITION
};

typedef struct block_device {
    uint64_t id;
    enum block_device_type type;
    char name[18];
    void *private_data;
    uint64_t block_size;
    uint32_t total_blocks;

    list_head_t global_list;

    int (*read)(struct block_device* dev,uint64_t lba,uint32_t count,void* buffer);
    int (*write)(struct block_device* dev,uint64_t lba,uint32_t count,const void* buffer);
} block_device_t;

typedef struct partition
{
    block_device_t device;
    block_device_t *parent;
    list_head_t childs_list_item;
    uint64_t start_lba;
    uint64_t sector_count;
    uint8_t bootable;
    uint8_t part_type;
} partition_t;

typedef struct real_device
{
    block_device_t device;
    spin_list_head_t childs;
    list_head_t device_list_item;
} real_device_t ;

typedef struct {
    volatile uint64_t global_block_id;
    volatile uint64_t block_device_id;
    spin_list_head_t block_list;
    spin_list_head_t device_list;
} block_manager_t;

int block_register(block_device_t* dev);

#define MBR_SIGNATURE 0xAA55
#define MBR_PART_COUNT 4
#define GPT_PROTECTIVE_TYPE 0xEE

typedef struct {
    uint8_t  bootable;
    uint8_t  start_chs[3];
    uint8_t  type;
    uint8_t  end_chs[3];
    uint32_t start_lba;
    uint32_t sector_count;
} __attribute__((packed)) mbr_entry_t;

typedef struct {
    uint8_t       bootstrap[446];
    mbr_entry_t   entry[4];
    uint16_t      signature;
} __attribute__((packed)) mbr_t;

#endif