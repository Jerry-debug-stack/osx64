#include "mm/mm.h"
#include "lib/string.h"
#include "mm/page_pool.h"
#include "multiboot.h"

/// @brief memory reference table：定义在加载部分的尾部
extern uint8_t _mrt_start[];

PHYSIC_AREA_ITEM pais[DEFAULT_PAI_NUMBER];

MM_MANAGER mm;

static void flush_tlb(void);
static void invlpg_tlb(uint64_t addr);

static void get_total_memory(MULTIBOOT_INFO* info);
static void set_mrt_table(void);
static void set_kernel_area(void);

extern void init_slab(void);
extern void init_heap();

extern uint32_t ptable4[];
uint64_t *vir_ptable4;

void init_mm(MULTIBOOT_INFO* info)
{
    uint64_t a;
    __asm__("movq $ptable4, %0" : "=r"(a));
    vir_ptable4 = easy_phy2linear(a);

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
    uint64_t addr = ((uint64_t)easy_linear2phy(_mrt_start) + mm.tpp + 0xfff) & 0xfffffffffffff000;
    memset(_mrt_start, (uint8_t)0xff, (addr >> 12));
    uint64_t last_addr = addr;
    for (uint32_t i = 0; i < mm.npai; i++) {
        if (pais[i].epa <= addr) {
            pais[i].fpp = 0;
        } else if (pais[i].spa < addr) {
            pais[i].spa = pais[i].nfpa = addr;
            pais[i].fpp = (pais[i].epa - addr) >> 12;
            last_addr = pais[i].epa;
            memset(&_mrt_start[addr >> 12], 0, pais[i].fpp);
        } else {
            memset(&_mrt_start[last_addr >> 12], 0xff, (pais[i].spa - last_addr) >> 12);
            memset(&_mrt_start[pais[i].spa >> 12], 0, pais[i].fpp);
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
        put_page_2M(addr, (uint64_t)easy_phy2linear(addr), (uint64_t)vir_ptable4);
    }
    for (uint32_t i = SLAB_START_ID_IN_PML4; i < SLAB_START_ID_IN_PML4 + NUMBER_OF_OTHER_IN_PML4; i++) {
        uint64_t phy = alloc_page_4k();
        memset((void*)easy_phy2linear(phy), 0, 4096);
        vir_ptable4[i] = phy | PAGE_KERNEL_DIR;
    }
    flush_tlb();
}

uint64_t alloc_page_4k(void)
{
    for (uint32_t i = 0; i < mm.npai; i++) {
        if (pais[i].fpp) {
            uint64_t ret = pais[i].nfpa;
            _mrt_start[ret >> 12]++;
            pais[i].fpp--;
            if (pais[i].fpp) {
                for (uint64_t j = ret >> 12; j < pais[i].epa >> 12; j++) {
                    if (!_mrt_start[j]) {
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
static uint64_t __alloc_n_pages_4k_locked(uint32_t n)
{
    if (n == 0)
        return 0;
    for (uint32_t i = 0; i < mm.npai; i++) {
        if (pais[i].fpp >= n) {
            uint64_t ret;
            uint32_t m = 0;
            for (uint64_t j = pais[i].nfpa >> 12; j < pais[i].epa >> 12; j++) {
                if (!_mrt_start[j]) {
                    if (!m)
                        ret = j;
                    if (++m == n) {
                        for (uint32_t k = 0; k < n; k++)
                            _mrt_start[ret + k] = 1;
                        pais[i].fpp -= n;
                        if (pais[i].fpp) {
                            for (uint64_t k = pais[i].nfpa >> 12; k < pais[i].epa >> 12; k++) {
                                if (!_mrt_start[k]) {
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

uint64_t alloc_n_pages_4k(uint32_t n){
    spin_lock(&mm.lock);
    uint64_t phy_addr = __alloc_n_pages_4k_locked(n);
    spin_unlock(&mm.lock);
    return phy_addr;
}

void free_n_pages_4k(uint32_t n, uint64_t addr)
{
    if (addr & 0xfff) {
        halt();
    }
    if (n == 0)
        return;
    spin_lock(&mm.lock);
    for (uint32_t i = 0; i < n; i++)
        decrease_reference_page_4k(addr + (i << 12));
    spin_unlock(&mm.lock);
}

uint8_t decrease_reference_page_4k(uint64_t addr)
{
    if (addr >= mm.hpa)
        halt();
    uint64_t index = addr >> 12;
    if (_mrt_start[index] == 0xff || !_mrt_start[index])
        halt();
    if (--_mrt_start[index] == 0) {
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
        return _mrt_start[index];
}

uint8_t add_reference_page_4k(uint64_t addr)
{
    uint64_t index = addr >> 12;
    if (_mrt_start[index] == 0xff || !_mrt_start[index]) {
        halt();
    } else if (_mrt_start[index] == 0xfe)
        return 1;
    else {
        _mrt_start[index]++;
        return 0;
    }
}

/// @param type: 0 for kernel 1 for user4k other for user4k protected
void __put_page_4k_locked(uint64_t phy_addr, uint64_t vir_addr, uint64_t ptable_vir, uint8_t type)
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

void put_page_4k(uint64_t phy_addr, uint64_t vir_addr, uint64_t ptable_vir, uint8_t type){
    spin_lock(&mm.lock);
    __put_page_4k_locked(phy_addr,vir_addr,ptable_vir,type);
    spin_unlock(&mm.lock);
}

int exist_page_4k(uint64_t vir_addr, uint64_t ptable_vir) {
    if (vir_addr & 0xfff) 
        halt();

    uint64_t* ptable = (uint64_t*)ptable_vir;
    uint32_t layer[4];

    if (vir_addr >= VIRTUAL_ADDR_0) {
        layer[0] = ((uint64_t)easy_linear2phy(vir_addr) >> TABLE_LEVEL_1_BITS) + 256;
    } else {
        layer[0] = vir_addr >> TABLE_LEVEL_1_BITS;
    }
    layer[1] = (vir_addr >> TABLE_LEVEL_2_BITS) & 0x1FF;
    layer[2] = (vir_addr >> TABLE_LEVEL_3_BITS) & 0x1FF;
    layer[3] = (vir_addr >> TABLE_LEVEL_4_BITS) & 0x1FF;

    for (int i = 0; i < 4; i++) {
        if (!(ptable[layer[i]] & PAGE_PRESENT)) {
            return 0;
        }

        if (i < 3 && (ptable[layer[i]] & PAGE_BIG_ENTRY)) {
            return 1;
        }
        uint64_t next_phy = ptable[layer[i]] & 0xfffffffffffff000;
        ptable = (uint64_t*)easy_phy2linear(next_phy);
    }
    return 1;
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

static void free_pagetable_level(uint64_t table_vir, int level) {
    uint64_t* table = (uint64_t*)table_vir;
    for (int i = 0; i < 512; i++) {
        if (table[i] & PAGE_PRESENT) {
            uint64_t next_phy = table[i] & 0xfffffffffffff000;
            uint64_t next_vir = (uint64_t)easy_phy2linear(next_phy);

            if (level < 3) {
                free_pagetable_level(next_vir, level + 1);
                kfree((void*)next_vir);
            } else {
                kfree((void*)next_vir);
            }
        }
    }
}

void free_ptable_and_mem(uint64_t pml4_vir) {
    if ((pml4_vir & 0xfff) != 0)
        halt();
    if (pml4_vir == (uint64_t)vir_ptable4)
        return;
    uint64_t* pml4 = (uint64_t*)pml4_vir;
    for (int i = 0; i < 256; i++) {
        if (pml4[i] & PAGE_PRESENT) {
            uint64_t pdpt_phy = pml4[i] & 0xfffffffffffff000;
            uint64_t pdpt_vir = (uint64_t)easy_phy2linear(pdpt_phy);
            free_pagetable_level(pdpt_vir, 1);
            kfree((void*)pdpt_vir);
            pml4[i] = 0;
        }
    }
    kfree((void *)pml4_vir);
}

static void copy_user_pagetable(uint64_t *src_pt, uint64_t *dst_pt, int level) {
    // level: 1=PDPT, 2=PD, 3=PT
    for (int i = 0; i < 512; i++) {
        uint64_t src_pte = src_pt[i];
        if (!(src_pte & PAGE_PRESENT)){
            dst_pt[i] = 0;
            continue;
        }

        // 检查是否为大页（仅当 level < 3 时可能）
        if (src_pte & PAGE_BIG_ENTRY) {
            // 系统假设只使用4K页，遇到大页则停机
            halt();
        }

        if (level == 3) {
            // 最后一级页表：复制物理页
            uint64_t src_phy = src_pte & 0xfffffffffffff000;
            void *src_vir = easy_phy2linear(src_phy);
            void *new_page = kmalloc(4096);
            uint64_t new_phy = (uint64_t)easy_linear2phy(new_page);
            memcpy(new_page, src_vir, 4096);
            uint64_t flags = src_pte & 0xfff;          // 保留原权限位
            dst_pt[i] = new_phy | flags;
        } else {
            // 中间级页表：确保目标项存在，并递归下一级
            uint64_t src_next_phy = src_pte & 0xfffffffffffff000;
            void *src_next_vir = easy_phy2linear(src_next_phy);

            if (!(dst_pt[i] & PAGE_PRESENT)) {
                void *new_table = kmalloc(4096);
                uint64_t new_table_phy = (uint64_t)easy_linear2phy(new_table);
                memset(new_table, 0, 4096);
                uint64_t flags = src_pte & 0xfff;      // 保留原权限位
                dst_pt[i] = new_table_phy | flags;
            }

            uint64_t dst_next_phy = dst_pt[i] & 0xfffffffffffff000;
            void *dst_next_vir = easy_phy2linear(dst_next_phy);
            copy_user_pagetable((uint64_t*)src_next_vir, (uint64_t*)dst_next_vir, level + 1);
        }
    }
}

void copy_pagetable_and_mem(uint64_t dest, uint64_t source) {
    // 1. 直接复制内核部分（后256项）
    memcpy((void*)(dest + 256 * 8), (void*)(source + 256 * 8), 256 * 8);

    // 2. 处理用户部分（前256项）
    uint64_t *src_pml4 = (uint64_t*)source;
    uint64_t *dst_pml4 = (uint64_t*)dest;
    for (int i = 0; i < 256; i++) {
        uint64_t src_pte = src_pml4[i];
        if (!(src_pte & PAGE_PRESENT)){
            dst_pml4[i] = 0;
            continue;
        }

        // 检查PML4层的大页（一般不会出现）
        if (src_pte & PAGE_BIG_ENTRY) {
            halt();
        }

        uint64_t src_next_phy = src_pte & 0xfffffffffffff000;
        void *src_next_vir = easy_phy2linear(src_next_phy);

        // 确保目标PML4项存在
        if (!(dst_pml4[i] & PAGE_PRESENT)) {
            void *new_table = kmalloc(4096);
            uint64_t new_table_phy = (uint64_t)easy_linear2phy(new_table);
            memset(new_table, 0, 4096);
            uint64_t flags = src_pte & 0xfff;          // 保留原权限位
            dst_pml4[i] = new_table_phy | flags;
        }

        uint64_t dst_next_phy = dst_pml4[i] & 0xfffffffffffff000;
        void *dst_next_vir = easy_phy2linear(dst_next_phy);
        copy_user_pagetable((uint64_t*)src_next_vir, (uint64_t*)dst_next_vir, 1);
    }
}

static inline void flush_tlb(void)
{
    __asm__ __volatile__("movq %%cr3,%%rax;movq %%rax,%%cr3;" ::: "rax");
}

static inline void invlpg_tlb(uint64_t addr)
{
    __asm__ __volatile__("invlpg (%0);" ::"r"(addr) : "memory");
}

int copy_to_user(void *dest,void *source,uint32_t length){
    // 暂时先直接用memcpy
    memcpy(dest,source,length);
    return 0;
}

int put_user(char num,char *buf){
    *buf = num;
    return 0;
}
