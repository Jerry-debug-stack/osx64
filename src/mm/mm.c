#include "mm/mm.h"
#include "lib/string.h"
#include "mm/page_pool.h"
#include "multiboot.h"

/// @brief memory reference table：定义在加载部分的尾部
uint8_t mrt[1] __attribute__((section(".mrt")));

PHYSIC_AREA_ITEM pais[DEFAULT_PAI_NUMBER];

MM_MANAGER mm;

static void flush_tlb(void);
static void invlpg_tlb(uint64_t addr);

static void get_total_memory(MULTIBOOT_INFO* info);
static void set_mrt_table(void);
static void set_kernel_area(void);

extern void init_slab(void);
extern void init_heap();

void init_mm(MULTIBOOT_INFO* info)
{
    get_total_memory(info);
    set_mrt_table();
    set_kernel_area();
    init_slab();
    init_heap();
}

static void get_total_memory(MULTIBOOT_INFO* info)
{
    if (!(info->flags & MULTIBOOT_INFO_MEM_MAP))
        halt();
    memset(pais, 0, sizeof(pais)); /* 清零pat */
    uint32_t i = 0;
    uint32_t mmap_end = info->mmap_addr + info->mmap_length;
    MULTIBOOT_MMAP_ENTRY* entry = easy_phy2linear(info->mmap_addr);
    mm.tfpp = 0;
    mm.hpa = 0x100000000UL;
    while ((uint32_t)(uint64_t)entry < mmap_end) {
        if (entry->type == MULTIBOOT_MEMORY_AVAILABLE) {
            pais[i].nfpa = pais[i].spa = (entry->addr + 0xfff) & 0xfffffffffffff000;
            pais[i].epa = (entry->addr + entry->len) & 0xfffffffffffff000;
            if (pais[i].spa < pais[i].epa) {
                pais[i].fpp = (pais[i].epa - pais[i].spa) >> 12;
                mm.hpa = (mm.hpa > pais[i].epa) ? mm.hpa : pais[i].epa;
                i++;
            }
        }
        entry = (void*)((uint64_t)entry + entry->size + 4);
    }
    mm.npai = i;
    mm.tpp = (mm.hpa + 0xfff) >> 12;
}

static void set_mrt_table(void)
{
    uint64_t addr = ((uint64_t)easy_linear2phy(mrt) + mm.tpp + 0xfff) & 0xfffffffffffff000;
    memset(mrt, (uint8_t)0xff, (addr >> 12));
    uint64_t last_addr = addr;
    for (uint32_t i = 0; i < mm.npai; i++) {
        if (pais[i].epa <= addr) {
            pais[i].fpp = 0;
        } else if (pais[i].spa < addr) {
            pais[i].spa = pais[i].nfpa = addr;
            pais[i].fpp = (pais[i].epa - addr) >> 12;
            last_addr = pais[i].epa;
            memset(&mrt[addr >> 12], 0, pais[i].fpp);
        } else {
            memset(&mrt[last_addr >> 12], 0xff, (pais[i].spa - last_addr) >> 12);
            memset(&mrt[pais[i].spa >> 12], 0, pais[i].fpp);
            last_addr = pais[i].epa;
        }
        mm.tfpp += pais[i].fpp;
    }
}

static void set_kernel_area(void)
{
    uint64_t max = (mm.hpa + 0x1fffff) >> TABLE_LEVEL_3_BITS;
    for (uint64_t i = 0; i < max; i++) {
        uint64_t addr = i << TABLE_LEVEL_3_BITS;
        put_page_2M(addr, (uint64_t)easy_phy2linear(addr), (uint64_t)ptable4);
    }
    for (uint32_t i = SLAB_START_ID_IN_PML4; i < SLAB_START_ID_IN_PML4 + NUMBER_OF_OTHER_IN_PML4; i++) {
        uint64_t phy = alloc_page_4k();
        memset((void*)easy_phy2linear(phy), 0, 4096);
        ptable4[i] = phy | PAGE_KERNEL_DIR;
    }
    flush_tlb();
}

uint64_t alloc_page_4k(void)
{
    for (uint32_t i = 0; i < mm.npai; i++) {
        if (pais[i].fpp) {
            uint64_t ret = pais[i].nfpa;
            mrt[ret >> 12]++;
            pais[i].fpp--;
            if (pais[i].fpp) {
                for (uint64_t j = ret >> 12; j < pais[i].epa >> 12; j++) {
                    if (!mrt[j]) {
                        pais[i].nfpa = j << 12;
                        return ret;
                    }
                }
                halt();
            } else
                return ret;
        }
    }
    halt();
}

/// @return 正常的话返回获取到的地址，没有找到就返回0
uint64_t alloc_n_pages_4k(uint32_t n)
{
    if (n == 0)
        return 0;
    for (uint32_t i = 0; i < mm.npai; i++) {
        if (pais[i].fpp >= n) {
            uint64_t ret;
            uint32_t m = 0;
            for (uint64_t j = pais[i].nfpa >> 12; j < pais[i].epa >> 12; j++) {
                if (!mrt[j]) {
                    if (!m)
                        ret = j;
                    if (++m == n) {
                        for (uint32_t k = 0; k < n; k++)
                            mrt[ret + k] = 1;
                        pais[i].fpp -= n;
                        if (pais[i].fpp) {
                            for (uint64_t k = pais[i].nfpa >> 12; k < pais[i].epa >> 12; k++) {
                                if (!mrt[k]) {
                                    pais[i].nfpa = k << 12;
                                    return ret << 12;
                                }
                            }
                            halt();
                        }
                        return ret << 12;
                    }
                } else
                    m = 0;
            }
        }
    }
    return 0;
}

void decrease_reference_pages_4k(uint32_t n, uint64_t addr)
{
    if (addr & 0xfff) {
        halt();
    }
    if (n == 0)
        return;
    for (uint32_t i = 0; i < n; i++)
        decrease_reference_page_4k(addr + (i << 12));
}

uint8_t decrease_reference_page_4k(uint64_t addr)
{
    if (addr >= mm.hpa)
        halt();
    uint64_t index = addr >> 12;
    if (mrt[index] == 0xff || !mrt[index])
        halt();
    if (--mrt[index] == 0) {
        for (uint32_t i = 0; i < mm.npai; i++) {
            if (addr >= pais[i].spa && addr < pais[i].epa) {
                if (pais[i].fpp)
                    pais[i].nfpa = (pais[i].nfpa < addr) ? pais[i].nfpa : addr;
                else
                    pais[i].nfpa = addr;
                pais[i].fpp++;
                return 0;
            }
        }
        halt();
    } else
        return mrt[index];
}

uint8_t add_reference_page_4k(uint64_t addr)
{
    uint64_t index = addr >> 12;
    if (mrt[index] == 0xff || !mrt[index]) {
        halt();
    } else if (mrt[index] == 0xfe)
        return 1;
    else {
        mrt[index]++;
        return 0;
    }
}

/// @param type: 0 for kernel 1 for user4k other for user4k protected
void put_page_4k(uint64_t phy_addr, uint64_t vir_addr, uint64_t ptable_vir, uint8_t type)
{
    if (vir_addr & 0xfff) {
        halt();
    }
    uint16_t dir_type;
    uint16_t item_type;
    if (type == 0) {
        item_type = PAGE_KERNEL_4K;
        dir_type = PAGE_KERNEL_DIR;
    } else {
        dir_type = PAGE_USER_DIR;
        if (type == 1)
            item_type = PAGE_USER_4K;
        else
            item_type = PAGE_USER_4K_COPY_ON_WRITE;
    }
    uint64_t* ptable = (void*)ptable_vir;
    uint32_t layer[4];
    if (vir_addr >= VIRTUAL_ADDR_0)
        layer[0] = ((uint64_t)easy_linear2phy(vir_addr) >> TABLE_LEVEL_1_BITS) + 256;
    else
        layer[0] = vir_addr >> TABLE_LEVEL_1_BITS;
    layer[1] = (vir_addr >> TABLE_LEVEL_2_BITS) & 0x1FF;
    layer[2] = (vir_addr >> TABLE_LEVEL_3_BITS) & 0x1FF;
    layer[3] = (vir_addr >> TABLE_LEVEL_4_BITS) & 0x1FF;
    for (int i = 0; i < 3; i++) {
        if (ptable[layer[i]] & PAGE_PRESENT) {
            if (ptable[layer[i]] & PAGE_BIG_ENTRY) {
                halt();
            }
            ptable = easy_phy2linear(ptable[layer[i]] & 0xfffffffffffff000);
        } else {
            uint64_t temp = alloc_page_4k();
            ptable[layer[i]] = temp | dir_type;
            ptable = easy_phy2linear(temp);
            memset((void*)ptable, 0, 4096);
        }
    }
    if (ptable[layer[3]] & PAGE_PRESENT) {
        halt();
    }
    ptable[layer[3]] = phy_addr | item_type;
    invlpg_tlb(vir_addr);
}

void rm_page_4k(uint64_t vir_addr, uint64_t ptable_vir)
{
    if (vir_addr & 0xfff) {
        halt();
    }
    uint64_t* ptable = (void*)ptable_vir;
    uint32_t layer[4];
    if (vir_addr >= VIRTUAL_ADDR_0)
        layer[0] = ((uint64_t)easy_linear2phy(vir_addr) >> TABLE_LEVEL_1_BITS) + 256;
    else
        layer[0] = vir_addr >> TABLE_LEVEL_1_BITS;
    layer[1] = (vir_addr >> TABLE_LEVEL_2_BITS) & 0x1FF;
    layer[2] = (vir_addr >> TABLE_LEVEL_3_BITS) & 0x1FF;
    layer[3] = (vir_addr >> TABLE_LEVEL_4_BITS) & 0x1FF;
    for (int i = 0; i < 3; i++) {
        if ((ptable[layer[i]] & PAGE_PRESENT) && (!(ptable[layer[i]] & PAGE_BIG_ENTRY)))
            ptable = easy_phy2linear(ptable[layer[i]] & 0xfffffffffffff000);
        else {
            halt();
        }
    }
    if (!(ptable[layer[3]] & PAGE_PRESENT)) {
        halt();
    }
    ptable[layer[3]] = 0;
    invlpg_tlb(vir_addr);
}

void put_page_2M(uint64_t phy_addr, uint64_t vir_addr, uint64_t ptable_vir)
{
    if ((vir_addr & 0x1fffff) || (phy_addr & 0x1fffff)) {
        halt();
    }
    uint64_t* ptable = (void*)ptable_vir;
    uint32_t layer[3];
    if (vir_addr >= VIRTUAL_ADDR_0)
        layer[0] = ((uint64_t)easy_linear2phy(vir_addr) >> TABLE_LEVEL_1_BITS) + 256;
    else
        layer[0] = vir_addr >> TABLE_LEVEL_1_BITS;
    layer[1] = (vir_addr >> TABLE_LEVEL_2_BITS) & 0x1FF;
    layer[2] = (vir_addr >> TABLE_LEVEL_3_BITS) & 0x1FF;
    for (int i = 0; i < 2; i++) {
        if (ptable[layer[i]] & PAGE_PRESENT) {
            if (ptable[layer[i]] & PAGE_BIG_ENTRY) {
                halt();
            }
            ptable = easy_phy2linear(ptable[layer[i]] & 0xfffffffffffff000);
        } else {
            uint64_t temp = alloc_page_4k();
            memset((void*)easy_phy2linear(temp), 0, 4096);
            ptable[layer[i]] = temp | PAGE_KERNEL_DIR;
            ptable = easy_phy2linear(temp);
        }
    }
    ptable[layer[2]] = phy_addr | PAGE_KERNEL_2M;
    invlpg_tlb(vir_addr);
}

static inline void flush_tlb(void)
{
    __asm__ __volatile__("movq %%cr3,%%rax;movq %%rax,%%cr3;" ::: "rax");
}

static inline void invlpg_tlb(uint64_t addr)
{
    __asm__ __volatile__("invlpg (%0);" ::"r"(addr) : "memory");
}
