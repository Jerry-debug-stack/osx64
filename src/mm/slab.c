#include "mm/slab.h"
#include "lib/string.h"
#include "mm/mm.h"

extern MM_MANAGER mm;

extern uint64_t heap_alloc(uint32_t size);
extern void heap_free(uint64_t addr);

static const SLAB_INFO slab_info[] = {
    { SLAB_START_32, 32, 127 },
    { SLAB_START_64, 64, 63 },
    { SLAB_START_128, 128, 31 },
    { SLAB_START_256, 256, 15 },
    { SLAB_START_512, 512, 7 },
    { SLAB_START_1024, 1024, 64 },
    { SLAB_START_2048, 2048, 64 },
};

void init_slab(void)
{
    for (uint64_t i = 0; i < 5; i++) {
        mm.slabei[i] = 0;
        mm.nfslabi[i] = 0;
        uint64_t phy_page = alloc_page_4k();
        SLAB* slab = (void*)(SLAB_START_32 + (i << 39));
        put_page_4k(phy_page, (uint64_t)slab, (uint64_t)easy_phy2linear(ptable4), 0);
        memset(slab, 0, 4096);
        slab->bitmap[0] = 1;
        slab->totalfree = slab_info[i].emptynum;
        slab->nextfree = 1;
    }

    for (uint32_t i = 0; i < 2; i++) {
        mm.mnfslab[i] = mm.mslab[i] = kmalloc(sizeof(SLAB_MIDDLE));
        memset(mm.mslab[i], 0, sizeof(SLAB_MIDDLE));
        mm.mslab[i]->totalfree = 32 * 8;
        for (uint32_t j = 0; j < (uint32_t)64 << i; j++) {
            uint64_t phy_addr = alloc_page_4k();
            put_page_4k(phy_addr, slab_info[i + 5].area_start_addr + (j << 12), (uint64_t)easy_phy2linear(ptable4), 0);
        }
    }
}

void* kmalloc(uint32_t size)
{
    if (size == 0)
        return (void*)0;
    for (uint64_t i = 0; i < 5; i++) {
        if (size <= slab_info[i].size) {
            SLAB* slab = (void*)(slab_info[i].area_start_addr + (mm.nfslabi[i] << 12));
            uint64_t addr = (uint64_t)slab + slab->nextfree * slab_info[i].size;
            slab->bitmap[slab->nextfree >> 3] |= (1 << (slab->nextfree & 7));
            if (--slab->totalfree) {
                for (uint16_t j = slab->nextfree + 1; j <= slab_info[i].emptynum; j++) {
                    if (!(slab->bitmap[j >> 3] & (1 << (j & 7)))) {
                        slab->nextfree = j;
                        return (void*)addr;
                    }
                }
                halt();
            } else {
                slab->nextfree = 0;
                for (uint64_t j = mm.nfslabi[i] + 1; j <= mm.slabei[i]; j++) {
                    SLAB* temp = (void*)(slab_info[i].area_start_addr + (j << 12));
                    if (temp->totalfree) {
                        mm.nfslabi[i] = j;
                        return (void*)addr;
                    }
                }
                uint64_t phy_page = alloc_page_4k();
                mm.slabei[i]++;
                mm.nfslabi[i] = mm.slabei[i];
                SLAB* newslab = (void*)(slab_info[i].area_start_addr + (mm.slabei[i] << 12));
                put_page_4k(phy_page, (uint64_t)newslab, (uint64_t)ptable4, 0);
                memset(newslab, 0, 4096);
                newslab->bitmap[0] = 1;
                newslab->totalfree = slab_info[i].emptynum;
                newslab->nextfree = 1;
                return (void*)addr;
            }
        }
    }
    for (uint32_t i = 0; i < 2; i++) {
        SLAB_INFO* info = (SLAB_INFO*)&slab_info[5 + i];
        if (size <= info->size) {
            SLAB_MIDDLE* slab = mm.mnfslab[i];
            uint64_t addr = info->area_start_addr + (slab->id << (18 + i)) + (slab->nextfree << (10 + i));
            slab->bitmap[slab->nextfree >> 3] |= (1 << (slab->nextfree & 7));
            if (--slab->totalfree) {
                for (uint32_t j = slab->nextfree; j < 32 * 8; j++) {
                    if (!(slab->bitmap[j >> 3] & (1 << (j & 7)))) {
                        slab->nextfree = j;
                        return (void*)addr;
                    }
                }
                halt();
            } else {
                slab->nextfree = 0;
                SLAB_MIDDLE* current = mm.mslab[i];
                SLAB_MIDDLE* last = current;
                while (current != (void*)0) {
                    if (current->totalfree) {
                        mm.mnfslab[i] = current;
                        return (void*)addr;
                    } else {
                        last = current;
                        current = current->next;
                    }
                }
                mm.mnfslab[i] = current = kmalloc(sizeof(SLAB_MIDDLE));
                memset(current, 0, sizeof(SLAB_MIDDLE));
                current->past = last;
                last->next = current;
                current->id = last->id + 1;
                current->totalfree = 32 * 8;
                for (uint32_t j = 0; j < info->size >> 4; j++) {
                    uint64_t phy_addr = alloc_page_4k();
                    put_page_4k(phy_addr, info->area_start_addr + (current->id << (18 + i)) + (j << 12), (uint64_t)easy_phy2linear(ptable4), 0);
                }
                return (void*)addr;
            }
        }
    }
    if (size <= 4096) {
        return easy_phy2linear(alloc_page_4k());
    }
    return (void*)heap_alloc(size);
}

void kfree(void* vir_addr)
{
    uint64_t addr = (uint64_t)vir_addr;
    if (addr < VIRTUAL_ADDR_0) {
        halt();
    }
    if (addr < SLAB_START_32)
        decrease_reference_page_4k((uint64_t)easy_linear2phy(addr));
    else {
        for (uint64_t i = 0; i < 5; i++) {
            if (addr < slab_info[i].area_start_addr + (1UL << 39)) {
                SLAB* slab = (void*)(addr & 0xfffffffffffff000);
                uint64_t slab_id = ((uint64_t)slab - slab_info[i].area_start_addr) >> 12;
                if (slab_id > mm.slabei[i]) {
                    halt();
                }
                mm.nfslabi[i] = (slab_id < mm.nfslabi[i]) ? slab_id : mm.nfslabi[i];
                uint8_t id = (uint16_t)(addr & 0xfff) / slab_info[i].size;
                if (!id || !(slab->bitmap[id >> 3] & (1 << (id & 7)))) {
                    halt();
                }
                slab->bitmap[id >> 3] &= ~(1 << (id & 7));
                if (slab->nextfree > id)
                    slab->nextfree = id;
                slab->totalfree++;
                return;
            }
        }
        for (uint64_t i = 0; i < 2; i++) {
            SLAB_INFO* info = (SLAB_INFO*)&slab_info[i + 5];
            if (addr < info->area_start_addr + (1UL << 39)) {
                uint32_t total_id = (addr - info->area_start_addr) >> (10 + i);
                uint32_t id = total_id >> 8;
                uint32_t inside_index = total_id & 0xff;
                SLAB_MIDDLE* current = mm.mslab[i];
                while (current->id < id) {
                    current = current->next;
                    if (!current) {
                        halt();
                    }
                }
                current->bitmap[inside_index >> 3] &= ~(1 << (inside_index & 7));
                current->totalfree++;
                if (current->nextfree > inside_index)
                    current->nextfree = inside_index;
                if (current->id < mm.mnfslab[i]->id)
                    mm.mnfslab[i] = current;
                return;
            }
        }
        heap_free((uint64_t)addr);
    }
}
