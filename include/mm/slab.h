#ifndef OS_SLAB_H
#define OS_SLAB_H

#include "page_pool.h"

#define SLAB_START_32          (((uint64_t)ARENA_START_ID_IN_PML4 - 256) << 39) + VIRTUAL_ADDR_0
#define SLAB_START_64          (SLAB_START_32 + ((uint64_t)1 << 39))
#define SLAB_START_128         (SLAB_START_64 + ((uint64_t)1 << 39))
#define SLAB_START_256         (SLAB_START_128 + ((uint64_t)1 << 39))
#define SLAB_START_512         (SLAB_START_256 + ((uint64_t)1 << 39))

#define SLAB_START_1024        (SLAB_START_512 + ((uint64_t)1 << 39))
#define SLAB_START_2048        (SLAB_START_1024 + ((uint64_t)1 << 39))

typedef struct SLab
{
    uint8_t nextfree;
    uint8_t totalfree;
    uint8_t bitmap[16];
} SLAB;

typedef struct SlabInfo{
    uint64_t area_start_addr;
    uint16_t size;
    uint16_t emptynum;
} SLAB_INFO;

typedef struct SlabMiddle
{
    struct SlabMiddle* next;
    struct SlabMiddle* past;
    uint32_t totalfree;
    uint32_t nextfree;
    uint32_t id;
    uint8_t bitmap[32];
} SLAB_MIDDLE;

#endif
