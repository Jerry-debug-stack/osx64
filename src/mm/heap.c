#include "const.h"
#include "mm/mm.h"
#include "mm/page_pool.h"
#include "mm/slab.h"

extern MM_MANAGER mm;

void init_heap()
{
    mm.nfhp = mm.shp = kmalloc(sizeof(HEAP));
    mm.nfhp->last = mm.nfhp->next = mm.nfhp->nextf = mm.nfhp->lastf = (void*)0;
    mm.nfhp->used = 0;
    mm.he = mm.nfhp->start_addr = HEAP_ADDR_START;
    mm.nfhp->size = HEAP_SIZE_MAX;
}

void *slab_alloc(uint32_t size);

uint64_t heap_alloc(uint32_t size)
{
    HEAP* heap = mm.nfhp;
    uint32_t aligned_size = (size + 0xfff) & 0xfffff000;
    while (heap) {
        if (heap->size >= aligned_size) {
            uint64_t ret = heap->start_addr;
            heap->size -= aligned_size;
            if (heap->size) {
                heap->start_addr += aligned_size;
                HEAP* new = slab_alloc(sizeof(HEAP));
                new->used = 1;
                new->size = aligned_size;
                new->start_addr = ret;
                new->lastf = new->nextf = (void*)0;
                new->next = heap;
                if (heap->last) {
                    heap->last->next = new;
                    new->last = heap->last;
                    heap->last = new;
                } else {
                    heap->last = new;
                    new->last = (void*)0;
                    mm.shp = new;
                }
            } else {
                heap->size = aligned_size;
                heap->used = 1;
                if (heap->lastf) {
                    heap->lastf->nextf = heap->nextf;
                    if (heap->nextf)
                        heap->nextf->lastf = heap->lastf;
                } else {
                    if (heap->nextf) {
                        heap->nextf->lastf = (void*)0;
                        mm.nfhp = heap->nextf;
                    } else {
                        halt();
                    }
                }
            }
            if (ret + aligned_size > mm.he) {
                for (uint64_t addr = mm.he; addr < ret + aligned_size; addr += 4096) {
                    uint64_t phy_addr = alloc_page_4k();
                    put_page_4k(phy_addr, addr, (uint64_t)easy_phy2linear(ptable4), 0);
                }
                mm.he = ret + aligned_size;
            }
            return ret;
        } else
            heap = heap->nextf;
    }
    halt();
}

uint8_t slab_free(void *vir_addr);
void heap_free(uint64_t addr)
{
    HEAP* heap = mm.shp;
    while (heap) {
        if (heap->used && heap->start_addr == addr) {
            heap->used = 0;
            if (heap->last && heap->next && !heap->last->used && !heap->next->used) {
                heap->last->size += heap->size + heap->next->size;
                heap->last->next = heap->next->next;
                heap->last->nextf = heap->next->nextf;
                if (heap->last->next)
                    heap->last->next->last = heap->last;
                if (heap->last->nextf)
                    heap->last->nextf->lastf = heap->last;
                slab_free(heap->next);
                slab_free(heap);
                return;
            } else if (heap->last && !heap->last->used) {
                heap->last->size += heap->size;
                heap->last->next = heap->next;
                if (heap->last->next)
                    heap->last->next->last = heap->last;
                slab_free(heap);
                return;
            } else if (heap->next && !heap->next->used) {
                heap->next->size += heap->size;
                heap->next->start_addr -= heap->size;
                heap->next->last = heap->last;
                if (heap->next->last)
                    heap->last->next = heap->next;
                else
                    mm.shp = heap->next;
                slab_free(heap);
                return;
            } else {
                /// tem不能是(void*)0,有heep_alloc保证
                HEAP* tem = mm.nfhp;
                HEAP* last = tem;
                if (tem->start_addr > heap->start_addr) {
                    tem->lastf = heap;
                    heap->nextf = tem;
                    heap->lastf = (void*)0;
                    mm.nfhp = heap;
                    return;
                }
                while (tem) {
                    if (last->start_addr < heap->start_addr && tem->start_addr > heap->start_addr) {
                        last->nextf = heap;
                        tem->lastf = heap;
                        heap->lastf = last;
                        heap->nextf = tem;
                        return;
                    }
                    last = tem;
                    tem = tem->nextf;
                }
                last->nextf = heap;
                heap->lastf = last;
                heap->nextf = (void*)0;
                return;
            }
        } else
            heap = heap->next;
    }
    halt();
}
