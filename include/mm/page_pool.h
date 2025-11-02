#ifndef OS_PAGE_POOL_H
#define OS_PAGE_POOL_H
#include <stdint.h>

///@brief maximum memory of 1024G=1T
#define MAX_MEM_SUPPORTED               1024UL*1024UL*1024UL*1024UL
#define ADDRESS_SPACE_GAP               MAX_MEM_SUPPORTED

typedef struct PhysicAreaItem
{
    /// @brief Starting Physical Address
    uint64_t spa;
    /// @brief Ending Physical Address
    uint64_t epa;
    /// @brief Number of Free Physical Pages
    uint64_t fpp;
    /// @brief Next Free Page Address
    uint64_t nfpa;
} PHYSIC_AREA_ITEM;

#define DEFAULT_PAI_NUMBER              128

#define PAGE_ENTRY_NUMBER               (0x1000 / 8)
#define TABLE_LEVEL_4_SIZE              0x1000
#define TABLE_LEVEL_4_BITS              12
#define TABLE_LEVEL_3_SIZE              0x200000
#define TABLE_LEVEL_3_BITS              21
#define TABLE_LEVEL_2_SIZE              0x40000000
#define TABLE_LEVEL_2_BITS              30
#define TABLE_LEVEL_1_SIZE              0x8000000000
#define TABLE_LEVEL_1_BITS              39

#define PAGE_USED                       1
#define PAGE_WRITABLE                   ((uint64_t)1 << 1)
#define PAGE_USER_MODE                  ((uint64_t)1 << 2)
#define PAGE_SYSTEM_MODE                ((uint64_t)0 << 2)
#define PAGE_WRITE_THROUGH              ((uint64_t)1 << 3)
#define PAGE_LEVEL_CACHE_DISABLE        ((uint64_t)1 << 4)
#define PAGE_LEVEL_CACHE_ENABLE         ((uint64_t)0 << 4)
#define PAGE_ACCESSED                   ((uint64_t)1 << 5)
#define PAGE_DIRECTORY_ENTRY            ((uint64_t)0 << 7)
#define PAGE_BIG_ENTRY                  ((uint64_t)1 << 7)
#define PAGE_GLOBAL                     ((uint64_t)1 << 8)
#define PAGE_FULL                       ((uint64_t)1 << 9)

#endif
