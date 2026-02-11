#ifndef OS_PAGE_POOL_H
#define OS_PAGE_POOL_H

#include "const.h"
#include <stdint.h>

///@brief maximum memory of 1024G=1T
#define MAX_MEM_SUPPORTED 1024UL * 1024UL * 1024UL * 1024UL
#define ADDRESS_SPACE_GAP MAX_MEM_SUPPORTED

/// 我们假定arena的单个区域不会超过512G也就是一个Buffer
#define NUMBER_OF_OTHER_IN_PML4 (7 + 1)
#define SLAB_START_ID_IN_PML4 (256 + 2)

typedef struct PhysicAreaItem {
    /// @brief Starting Physical Address
    uint64_t spa;
    /// @brief Ending Physical Address
    uint64_t epa;
    /// @brief Number of Free Physical Pages
    uint64_t fpp;
    /// @brief Next Free Page Address
    uint64_t nfpa;
} PHYSIC_AREA_ITEM;

#include "mm/slab.h"
#include "lib/safelist.h"

typedef struct Heap {
    struct Heap* next;
    struct Heap* last;
    struct Heap* nextf;
    struct Heap* lastf;
    uint64_t start_addr;
    uint64_t size;
    uint8_t used;
} HEAP;

typedef struct MmManager {
    /// @brief Highest Physical Address
    uint64_t hpa;
    /// @brief Total Number of Physical Pages
    uint64_t tpp;
    /// @brief Total Number of Free Physical Pages
    uint64_t tfpp;
    /// @brief Number of physic area items
    uint32_t npai;
    /// @brief Slab End ID
    uint64_t slabei[7];

    /// @brief Next free Slab ID
    uint64_t nfslabi[7];

    /// @brief Medium Slab pointers
    struct SlabMiddle* mslab[2];

    /// @brief Next Free Medium Slab pointers
    struct SlabMiddle* mnfslab[2];

    /// @brief start heap pointer
    struct Heap* shp;
    /// @brief next free heap pointer
    struct Heap* nfhp;
    /// @brief heap end
    uint64_t he;
    spin_lock_t lock;
} MM_MANAGER;

#define DEFAULT_PAI_NUMBER 128

#define PAGE_ENTRY_NUMBER (0x1000 / 8)
#define TABLE_LEVEL_4_SIZE 0x1000
#define TABLE_LEVEL_4_BITS 12
#define TABLE_LEVEL_3_SIZE 0x200000
#define TABLE_LEVEL_3_BITS 21
#define TABLE_LEVEL_2_SIZE 0x40000000
#define TABLE_LEVEL_2_BITS 30
#define TABLE_LEVEL_1_SIZE 0x8000000000
#define TABLE_LEVEL_1_BITS 39

#define PAGE_PRESENT 1
#define PAGE_WRITABLE ((uint64_t)1 << 1)
#define PAGE_USER_MODE ((uint64_t)1 << 2)
#define PAGE_SYSTEM_MODE ((uint64_t)0 << 2)
#define PAGE_WRITE_THROUGH ((uint64_t)1 << 3)
#define PAGE_LEVEL_CACHE_DISABLE ((uint64_t)1 << 4)
#define PAGE_LEVEL_CACHE_ENABLE ((uint64_t)0 << 4)
#define PAGE_ACCESSED ((uint64_t)1 << 5)
#define PAGE_DIRECTORY_ENTRY ((uint64_t)0 << 7)
#define PAGE_BIG_ENTRY ((uint64_t)1 << 7)
#define PAGE_GLOBAL ((uint64_t)1 << 8)
#define PAGE_FULL ((uint64_t)1 << 9)

#define PAGE_KERNEL_4K (PAGE_PRESENT | PAGE_WRITABLE | PAGE_SYSTEM_MODE | PAGE_LEVEL_CACHE_ENABLE | PAGE_GLOBAL)
#define PAGE_KERNEL_DIR PAGE_KERNEL_4K
#define PAGE_KERNEL_2M (PAGE_PRESENT | PAGE_WRITABLE | PAGE_SYSTEM_MODE | PAGE_LEVEL_CACHE_ENABLE | PAGE_GLOBAL | PAGE_BIG_ENTRY)
#define PAGE_USER_4K (PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER_MODE | PAGE_LEVEL_CACHE_ENABLE)
#define PAGE_USER_DIR PAGE_USER_4K
#define PAGE_USER_4K_COPY_ON_WRITE (PAGE_PRESENT | PAGE_USER_MODE | PAGE_LEVEL_CACHE_ENABLE)

extern uint64_t ptable4[512];

#endif
