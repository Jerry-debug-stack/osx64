#include <stdint.h>
#include "const.h"
#include "multiboot.h"
#include "mm/page_pool.h"
#include "lib/string.h"

/// @brief memory reference table：定义在加载部分的尾部
uint8_t mrt[1] __attribute__((section(".mrt")));

PHYSIC_AREA_ITEM pais[DEFAULT_PAI_NUMBER];

struct 
{
    /// @brief Highest Physical Address
    uint64_t hpa;
    /// @brief Total Number of Physical Pages
    uint64_t tpp;
    /// @brief Total Number of Free Physical Pages
    uint64_t tfpp;
    uint32_t npai;
}mm;

void get_total_memory(MULTIBOOT_INFO* info);
void set_mrt_table(void);

void init_mm(MULTIBOOT_INFO* info){
    get_total_memory(info);
    set_mrt_table();
}

void get_total_memory(MULTIBOOT_INFO* info){
    if(!(info->flags & MULTIBOOT_INFO_MEM_MAP))
        halt();
    memset(pais,0,sizeof(pais));        /* 清零pat */
    uint32_t i = 0;
    uint32_t mmap_end = info->mmap_addr + info->mmap_length;
    MULTIBOOT_MMAP_ENTRY* entry = easy_phy2linear(info->mmap_addr);
    mm.tfpp = 0;
    mm.hpa = 0x100000000UL;
    while ((uint32_t)(uint64_t)entry < mmap_end){
        if(entry->type == MULTIBOOT_MEMORY_AVAILABLE){
            pais[i].nfpa = pais[i].spa = (entry->addr + 0xfff) & 0xfffffffffffff000;
            pais[i].epa = (entry->addr + entry->len) & 0xfffffffffffff000;
            if(pais[i].spa < pais[i].epa){
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

void set_mrt_table(void){
    uint64_t addr = ((uint64_t)easy_linear2phy(mrt) + mm.tpp + 0xfff) & 0xfffffffffffff000;
    memset(mrt,(uint8_t)0xff,(addr >> 12));
    uint64_t last_addr = addr;
    for (uint32_t i = 0; i < mm.npai; i++)
    {
        if (pais[i].epa <= addr){
            pais[i].fpp = 0;
        }else if (pais[i].spa < addr){
            pais[i].spa = pais[i].nfpa = addr;
            pais[i].fpp = (pais[i].epa - addr) >> 12;
            last_addr = pais[i].epa;
            memset(&mrt[addr >> 12],0,pais[i].fpp);
        }else{
            memset(&mrt[last_addr >> 12],0xff,(pais[i].spa - last_addr) >> 12);
            memset(&mrt[pais[i].spa >> 12],0,pais[i].fpp);
            last_addr = pais[i].epa;
        }
        mm.tfpp += pais[i].fpp;
    }
}

void set_default_page_table(void){

}
 
uint64_t alloc_page_4k(void){
    for (uint32_t i = 0; i < mm.npai; i++)
    {
        if (pais[i].fpp){
            uint64_t ret = pais[i].nfpa;
            mrt[ret >> 12]++;
            pais[i].fpp--;
            if(pais[i].fpp){
                for (uint64_t j = ret >> 12; j < pais[i].epa >> 12; j++)
                {
                    if(!mrt[j]){
                        pais[i].nfpa = j << 12;
                        return ret;
                    }
                }
                halt();
            }else return ret;
        }
    }
    halt();
}

uint8_t decreasze_reference_page_4k(uint64_t addr){
    if(addr >= mm.hpa)
        halt();
    uint64_t index = addr >> 12;
    if(mrt[index] == 0xff || !mrt[index])
        halt();
    if(--mrt[index] == 0){
        for (uint32_t i = 0; i < mm.npai; i++)
        {
            if(addr >= pais[i].spa && addr < pais[i].epa){
                if(pais[i].fpp)
                    pais[i].nfpa = (pais[i].nfpa < addr) ? pais[i].nfpa : addr;
                else
                    pais[i].nfpa = addr;
                pais[i].fpp++;
                return 0;
            }
        }
        halt();
    }else return mrt[index];
}

uint8_t add_reference_page_4k(uint64_t addr){
    uint64_t index = addr >> 12;
    if(mrt[index] == 0xff || !mrt[index]){
        halt();
    }else if (mrt[index] == 0xfe)
        return 1;
    else{
        mrt[index]++;
        return 0;
    }
}

void put_page_4k(uint64_t vir_addr,uint64_t ptable_vir){
    if(vir_addr & 0xfff){ halt(); }
    uint64_t* ptable = (void*)ptable_vir;
    uint32_t layer1,layer2,layer3,layer4;
    if (vir_addr >= VIRTUAL_ADDR_0)
        layer1 = ((uint64_t)easy_linear2phy(vir_addr) >> TABLE_LEVEL_1_BITS) + 256;
    else layer1 = vir_addr >> TABLE_LEVEL_1_BITS;
    layer2 = (vir_addr >> TABLE_LEVEL_2_BITS) & 0x1FF;
    layer3 = (vir_addr >> TABLE_LEVEL_3_BITS) & 0x1FF;
    layer4 = (vir_addr >> TABLE_LEVEL_4_BITS) & 0x1FF;
    
}
