#include "ustring.h"
#include <stddef.h>
#include <stdint.h>

// 内存块头结构
typedef struct block_header {
    uint64_t size;
    struct block_header* next;
    int is_free;
} block_header_t;

// 内存池管理
#define HEAP_START 0x400000000000UL // 堆起始地址
#define HEAP_END 0x700000000000UL // 堆结束地址
#define PAGE_SIZE 4096
#define ALIGNMENT 8
#define MIN_BLOCK_SIZE (sizeof(block_header_t) + ALIGNMENT)

static block_header_t* free_list = NULL;
static void* heap_current = (void*)HEAP_START;

// 对齐工具函数
static inline uint64_t align_up(uint64_t size, uint64_t alignment)
{
    return (size + alignment - 1) & ~(alignment - 1);
}

static inline void* align_ptr(void* ptr, uint64_t alignment)
{
    return (void*)align_up((uint64_t)ptr, alignment);
}

// 扩展堆空间（触发缺页分配）
static void* sbrk(uint64_t size)
{
    void* old_brk = heap_current;
    void* new_brk = (void*)((char*)heap_current + size);

    if ((uintptr_t)new_brk > HEAP_END) {
        return NULL; // 堆溢出
    }

    heap_current = new_brk;
    return old_brk;
}

// 分割内存块
static void split_block(block_header_t* block, uint64_t size)
{
    if (block->size > size + MIN_BLOCK_SIZE) {
        block_header_t* new_block = (block_header_t*)((char*)block + sizeof(block_header_t) + size);
        new_block->size = block->size - size - sizeof(block_header_t);
        new_block->is_free = 1;
        new_block->next = block->next;

        block->size = size;
        block->next = new_block;

        // 将新块插入空闲链表
        new_block->next = free_list;
        free_list = new_block;
    }
}

// 合并相邻空闲块
static void coalesce_blocks()
{
    block_header_t* curr = free_list;

    while (curr && curr->next) {
        if ((char*)curr + sizeof(block_header_t) + curr->size == (char*)curr->next) {
            // 合并相邻块
            curr->size += sizeof(block_header_t) + curr->next->size;
            curr->next = curr->next->next;
        } else {
            curr = curr->next;
        }
    }
}

// malloc实现
void* malloc(uint64_t size)
{
    if (size == 0)
        return NULL;

    // 对齐大小，包含块头
    uint64_t aligned_size = align_up(size, ALIGNMENT);
    uint64_t total_size = aligned_size + sizeof(block_header_t);

    // 首次分配，初始化空闲链表
    if (free_list == NULL) {
        // 分配初始堆空间（触发缺页）
        uint64_t initial_size = align_up(total_size * 4, PAGE_SIZE);
        void* heap = sbrk(initial_size);
        if (!heap)
            return NULL;

        free_list = (block_header_t*)heap;
        free_list->size = initial_size - sizeof(block_header_t);
        free_list->is_free = 1;
        free_list->next = NULL;
    }

    // 首次适应算法寻找空闲块
    block_header_t* prev = NULL;
    block_header_t* curr = free_list;

    while (curr) {
        if (curr->is_free && curr->size >= aligned_size) {
            // 找到合适块
            if (prev) {
                prev->next = curr->next;
            } else {
                free_list = curr->next;
            }

            curr->is_free = 0;
            curr->next = NULL;

            // 分割块（如果可能）
            split_block(curr, aligned_size);

            return (void*)((char*)curr + sizeof(block_header_t));
        }

        prev = curr;
        curr = curr->next;
    }

    // 没有找到合适块，扩展堆
    uint64_t extend_size = align_up(total_size, PAGE_SIZE);
    block_header_t* new_block = (block_header_t*)sbrk(extend_size);
    if (!new_block)
        return NULL;

    new_block->size = extend_size - sizeof(block_header_t);
    new_block->is_free = 0;
    new_block->next = NULL;

    // 如果扩展的块与最后一个块相邻，合并它们
    if (prev && (char*)prev + sizeof(block_header_t) + prev->size == (char*)new_block) {
        prev->size += extend_size;
        return (void*)((char*)prev + sizeof(block_header_t));
    }

    return (void*)((char*)new_block + sizeof(block_header_t));
}

// free实现
void free(void* ptr)
{
    if (!ptr)
        return;

    block_header_t* block = (block_header_t*)((char*)ptr - sizeof(block_header_t));

    if (block->is_free) {
        return; // 重复释放
    }

    block->is_free = 1;

    // 插入到空闲链表头部
    block->next = free_list;
    free_list = block;

    // 合并相邻空闲块
    coalesce_blocks();
}

// realloc实现
void* realloc(void* ptr, uint64_t size)
{
    if (!ptr)
        return malloc(size);
    if (size == 0) {
        free(ptr);
        return NULL;
    }

    block_header_t* block = (block_header_t*)((char*)ptr - sizeof(block_header_t));
    uint64_t old_size = block->size;
    uint64_t aligned_size = align_up(size, ALIGNMENT);

    // 如果当前块足够大，直接使用
    if (old_size >= aligned_size) {
        split_block(block, aligned_size);
        return ptr;
    }

    // 检查是否可以与后面的空闲块合并
    if (block->next && block->next->is_free && old_size + sizeof(block_header_t) + block->next->size >= aligned_size) {

        // 合并块
        block->size += sizeof(block_header_t) + block->next->size;
        block->next = block->next->next;

        // 从空闲链表中移除被合并的块
        block_header_t* curr = free_list;
        block_header_t* prev = NULL;
        while (curr) {
            if (curr == block->next) {
                if (prev) {
                    prev->next = curr->next;
                } else {
                    free_list = curr->next;
                }
                break;
            }
            prev = curr;
            curr = curr->next;
        }

        split_block(block, aligned_size);
        return ptr;
    }

    // 需要分配新内存
    void* new_ptr = malloc(size);
    if (!new_ptr)
        return NULL;

    // 复制数据
    uint64_t copy_size = old_size < size ? old_size : size;
    memcpy(new_ptr, ptr, copy_size);

    free(ptr);
    return new_ptr;
}

// calloc实现
void* calloc(uint64_t num, uint64_t size)
{
    uint64_t total_size = num * size;
    void* ptr = malloc(total_size);
    if (ptr) {
        memset(ptr, 0, total_size);
    }
    return ptr;
}
