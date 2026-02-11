#ifndef OS_BLOCK_H
#define OS_BLOCK_H

#include <stdint.h>

#define BLOCK_DEV_MAX 32

typedef struct block_device {
    uint64_t id;              // 全局唯一编号
    uint64_t total_blocks;    // 总扇区数
    uint32_t block_size;      // 扇区大小（通常512）
    void *private_data;            // 驱动私有数据
    int (*read)(struct block_device* dev,uint64_t lba,uint32_t count,void* buffer);
    int (*write)(struct block_device* dev,uint64_t lba,uint32_t count,const void* buffer);
} block_device_t;

typedef struct {
    block_device_t* devs[BLOCK_DEV_MAX];
    uint32_t count;
} block_manager_t;

#endif