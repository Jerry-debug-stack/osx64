#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "const.h"
#include "view/view.h"
#include "machine/cpu.h"
#include "mm/mm.h"
#include "lib/string.h"

GLOBAL_CPU* cpus;
// ACPI相关结构
struct rsdp_descriptor {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
} __attribute__((packed));

struct rsdp_descriptor_2 {
    struct rsdp_descriptor first_part;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t ext_checksum;
    uint8_t reserved[3];
} __attribute__((packed));

struct acpi_sdt_header {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed));

struct madt {
    struct acpi_sdt_header header;
    uint32_t local_controller_addr;
    uint32_t flags;
    // 后面跟着一系列子结构
} __attribute__((packed));

// Local APIC结构（类型0）
struct madt_local_apic {
    uint8_t type;           // 0x00
    uint8_t length;         // 0x08
    uint8_t acpi_processor_id;
    uint8_t apic_id;
    uint32_t flags;         // 第0位：启用标志
} __attribute__((packed));

struct madt *madt;
struct rsdp_descriptor *rsdp;

// 验证ACPI表校验和
static bool validate_checksum(void *table, size_t length) {
    uint8_t sum = 0;
    uint8_t *ptr = (uint8_t *)table;
    
    for (size_t i = 0; i < length; i++) {
        sum += ptr[i];
    }
    
    return (sum == 0);
}

// 查找RSDP
static struct rsdp_descriptor *find_rsdp(void) {
    // 搜索区域：0x000E0000 - 0x000FFFFF
    uint8_t *start = easy_phy2linear(0x000E0000);
    uint8_t *end = easy_phy2linear(0x000FFFFF);
    
    for (uint8_t *ptr = start; ptr < end; ptr += 16) {
        struct rsdp_descriptor *rsdp = (struct rsdp_descriptor *)ptr;
        
        // 检查签名
        if (rsdp->signature[0] == 'R' && rsdp->signature[1] == 'S' &&
            rsdp->signature[2] == 'D' && rsdp->signature[3] == ' ' &&
            rsdp->signature[4] == 'P' && rsdp->signature[5] == 'T' &&
            rsdp->signature[6] == 'R' && rsdp->signature[7] == ' ') {
            
            // 验证校验和
            if (validate_checksum(rsdp, sizeof(struct rsdp_descriptor))) {
                return rsdp;
            }
        }
    }
    
    return NULL;
}

// 查找MADT表
static struct madt *find_madt(void) {
    if (!rsdp) {
        return NULL;
    }
    
    // 获取RSDT地址
    struct acpi_sdt_header *rsdt = easy_phy2linear(rsdp->rsdt_address);
    if (!validate_checksum(rsdt, rsdt->length)) {
        return NULL;
    }
    
    // 计算RSDT中的条目数
    uint32_t entry_count = (rsdt->length - sizeof(struct acpi_sdt_header)) / 4;
    uint32_t *entries = (uint32_t *)((uintptr_t)rsdt + sizeof(struct acpi_sdt_header));
    
    // 查找APIC表
    for (uint32_t i = 0; i < entry_count; i++) {
        struct acpi_sdt_header *header = easy_phy2linear(entries[i]);
        
        if (header->signature[0] == 'A' && header->signature[1] == 'P' &&
            header->signature[2] == 'I' && header->signature[3] == 'C') {
            
            if (validate_checksum(header, header->length)) {
                return (struct madt *)header;
            }
        }
    }
    return NULL;
}

// 通过MADT统计处理器
static void alloc_logic_cpu_id(void) {
    // 遍历MADT中的条目
    uint8_t *madt_end = (uint8_t *)madt + madt->header.length;
    uint8_t *entry = (uint8_t *)madt + sizeof(struct madt);
    
    while (entry < madt_end) {
        uint8_t type = entry[0];
        uint8_t length = entry[1];
        
        if (type == 0x00) { // Local APIC
            struct madt_local_apic *local_apic = (struct madt_local_apic *)entry;
            if (cpus->total_num == MAX_CPU_NUM){
                break;
            }
            cpus->physic_apic_id[cpus->total_num] = local_apic->apic_id;
            cpus->total_num++;
        }
        // 移动到下一个条目
        entry += length;
        // 安全保护
        if (length == 0) {
            break;
        }
    }
}

uint32_t get_logic_cpu_id(void){
    uint32_t apic_id = get_apic_id();
    for (uint32_t i = 0; i < cpus->total_num; i++)
    {
        if(cpus->physic_apic_id[i] == apic_id)
            return i;
    }
    color_print("[ PANIC ] Logic cpu id get failed!!!",VIEW_COLOR_BLACK,VIEW_COLOR_WHITE);
    halt();
}

void init_acpi_madt(void){
    cpus = kmalloc(sizeof(GLOBAL_CPU));
    memset(cpus,0,sizeof(GLOBAL_CPU));
    rsdp = find_rsdp();
    if (!rsdp){
        color_printf("[ PANIC ] rsdp not found!!!",VIEW_COLOR_RED,VIEW_COLOR_WHITE);
        halt();
    }
    madt = find_madt();
    if (!madt){
        color_printf("[ PANIC ] madt not found!!!",VIEW_COLOR_RED,VIEW_COLOR_WHITE);
        halt();
    }
    cpus->total_num = 0;
    alloc_logic_cpu_id();
    // 如果需要在更多核数的情况下快速查找,应当对physic_apic_id排序
    for (size_t i = 0; i < cpus->total_num; i++)
    {
        color_printf("[  CPU  ] alloc logic id %d for cpu apic %#x\n",VIEW_COLOR_BLACK,VIEW_COLOR_WHITE,i,cpus->physic_apic_id[i]);
    }
    color_printf("[  CPU  ] %d CPUs in total\n",VIEW_COLOR_BLACK,VIEW_COLOR_WHITE,cpus->total_num);
}
