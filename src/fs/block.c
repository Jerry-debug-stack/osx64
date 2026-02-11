#include "mm/mm.h"
#include "fs/block.h"
#include "lib/string.h"
#include "view/view.h"

static block_manager_t block_mgr;

void init_block(void)
{
    memset(&block_mgr, 0, sizeof(block_mgr));
}

int block_register(block_device_t* dev)
{
    if (block_mgr.count >= BLOCK_DEV_MAX)
        return -1;
    dev->id = block_mgr.count;
    block_mgr.devs[block_mgr.count++] = dev;
    wb_printf("[ BLOCK ] device %d registered (%ld blocks)\n",
            dev->id, dev->total_blocks);
    return 0;
}

uint64_t alloc_block_id(void)
{
    return __sync_fetch_and_add(&block_mgr.global_block_id, 1);
}
