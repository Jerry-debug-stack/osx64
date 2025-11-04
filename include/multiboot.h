#ifndef OS_MULTIBOOT_H
#define OS_MULTIBOOT_H

#include <stdint.h>

typedef struct MultibootInfo {
    uint32_t flags; // 必需的标志位，指示哪些字段有效
    uint32_t mem_lower; // 可用内存低端部分（KB），从0开始
    uint32_t mem_upper; // 可用内存高端部分（KB），从1MB开始
    uint32_t boot_device; // 引导设备ID
    uint32_t cmdline; // 内核命令行字符串地址
    uint32_t mods_count; // 模块数量
    uint32_t mods_addr; // 模块信息结构体数组地址

    // ELF段头信息
    union {
        struct {
            uint32_t num; // 段头数量
            uint32_t size; // 每个段头大小
            uint32_t addr; // 段头表物理地址
            uint32_t shndx; // 段名字符串表索引
        } elf_sec;
    } elf;

    uint32_t mmap_length; // 内存映射缓冲区长度
    uint32_t mmap_addr; // 内存映射缓冲区物理地址

    uint32_t drives_length; // 驱动器信息长度
    uint32_t drives_addr; // 驱动器信息地址

    uint32_t config_table; // ROM配置表地址
    uint32_t boot_loader_name; // 引导加载程序名字符串地址

    uint32_t apm_table; // APM表地址

    // VBE信息
    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;

    uint64_t framebuffer_addr; // 帧缓冲区物理地址
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t framebuffer_bpp;
    uint8_t framebuffer_type;

    // 颜色信息（根据framebuffer_type不同而不同）
    union {
        struct {
            uint32_t framebuffer_palette_addr;
            uint16_t framebuffer_palette_num_colors;
        } framebuffer_palette;
        struct {
            uint8_t framebuffer_red_field_position;
            uint8_t framebuffer_red_mask_size;
            uint8_t framebuffer_green_field_position;
            uint8_t framebuffer_green_mask_size;
            uint8_t framebuffer_blue_field_position;
            uint8_t framebuffer_blue_mask_size;
        } framebuffer_color_info;
    } framebuffer;
} __attribute__((packed)) MULTIBOOT_INFO;

#define MULTIBOOT_INFO_MEMORY 0x00000001
#define MULTIBOOT_INFO_BOOTDEV 0x00000002
#define MULTIBOOT_INFO_CMDLINE 0x00000004
#define MULTIBOOT_INFO_MODS 0x00000008
#define MULTIBOOT_INFO_AOUT_SYMS 0x00000010
#define MULTIBOOT_INFO_ELF_SHDR 0x00000020
#define MULTIBOOT_INFO_MEM_MAP 0x00000040
#define MULTIBOOT_INFO_DRIVE_INFO 0x00000080
#define MULTIBOOT_INFO_CONFIG_TABLE 0x00000100
#define MULTIBOOT_INFO_BOOT_LOADER_NAME 0x00000200
#define MULTIBOOT_INFO_APM_TABLE 0x00000400
#define MULTIBOOT_INFO_VIDEO_INFO 0x00000800

typedef struct MultibootMmapEntry {
    uint32_t size; // 结构体大小（不包括这个字段）
    uint64_t addr; // 起始地址
    uint64_t len; // 区域长度
    uint32_t type; // 区域类型
} __attribute__((packed)) MULTIBOOT_MMAP_ENTRY;

// 内存区域类型
#define MULTIBOOT_MEMORY_AVAILABLE 1
#define MULTIBOOT_MEMORY_RESERVED 2
#define MULTIBOOT_MEMORY_ACPI_RECLAIMABLE 3
#define MULTIBOOT_MEMORY_NVS 4
#define MULTIBOOT_MEMORY_BADRAM 5

typedef struct MultibootModList {
    uint32_t mod_start; // 模块起始地址
    uint32_t mod_end; // 模块结束地址
    uint32_t cmdline; // 模块命令行字符串地址
    uint32_t pad; // 填充（保留）
} __attribute__((packed)) MULTIBOOT_MOD_LIST;

#endif
