
#ifndef OS_ELF_H
#define OS_ELF_H

#include <stdint.h>

#define EI_NIDENT 16

typedef struct elf64_header {
    uint8_t elf_ident[EI_NIDENT]; // 魔数和相关信息
    uint16_t elf_type; // 文件类型
    uint16_t elf_machine; // 体系结构
    uint32_t elf_version; // 版本信息
    uint64_t elf_entry; // 程序入口点（64位）
    uint64_t elf_part_header_offset; // 程序头表偏移（64位）
    uint64_t elf_section_header_offset; // 节头表偏移（64位）
    uint32_t elf_flags; // 处理器特定标志
    uint16_t elf_header_size; // ELF头大小
    uint16_t elf_part_header_entry_size; // 程序头表项大小
    uint16_t elf_part_header_num; // 程序头表项数量
    uint16_t elf_section_header_entry_size; // 节头表项大小
    uint16_t elf_section_header_num; // 节头表项数量
    uint16_t elf_section_header_string_index; // 节名字符串表索引
} __attribute__((packed)) elf64_header_t;

#define ELF_INDEX_BITSIZE 4
#define ELF_BITSIZE_32 1
#define ELF_BITSIZE_64 2

#define ELF_INDEX_BITORDER 5
#define ELF_BITORDER_SMALL 1
#define ELF_BITORDER_BIG 2

#define ELF_TYPE_NONE 0 // 未知类型
#define ELF_TYPE_REL 1 // 可重定位文件(.o)
#define ELF_TYPE_EXEC 2 // 可执行文件
#define ELF_TYPE_DYN 3 // 共享目标文件(.so)
#define ELF_TYPE_CORE 4 // 核心转储文件

#define ELF_MACHINE_X86_64 62 // AMD x86-64架构
#define ELF_MACHINE_AARCH64 183 // ARM 64位架构
#define ELF_MACHINE_RISCV 243 // RISC-V 64位架构
#define ELF_MACHINE_LOONGARCH 258 // 龙芯架构
#define ELF_MACHINE_SPARCV9 43 // SPARC 64位

typedef struct elf64_section_header {
    uint32_t section_header_name; // 节名在字符串表中的索引
    uint32_t section_header_type; // 节类型
    uint64_t section_header_flags; // 节标志
    uint64_t section_header_addr; // 节的虚拟地址
    uint64_t section_header_offset; // 节在文件中的偏移
    uint64_t section_header_size; // 节的大小
    uint32_t section_header_link; // 链接到其他节索引
    uint32_t section_header_info; // 附加信息
    uint64_t section_header_addr_align; // 节的对齐要求
    uint64_t section_header_entry_size; // 每个表项的大小（对于表结构的节）
} __attribute__((packed)) elf64_section_header_t;

/* 节类型 (sh_type) */
#define ELF_SECTION_TYPE_NULL 0
#define ELF_SECTION_TYPE_PROGBITS 1
#define ELF_SECTION_TYPE_SYMTAB 2
#define ELF_SECTION_TYPE_STRTAB 3
#define ELF_SECTION_TYPE_RELA 4
#define ELF_SECTION_TYPE_HASH 5
#define ELF_SECTION_TYPE_DYNAMIC 6
#define ELF_SECTION_TYPE_NOTE 7
#define ELF_SECTION_TYPE_NOBITS 8
#define ELF_SECTION_TYPE_REL 9
#define ELF_SECTION_TYPE_SHLIB 10
#define ELF_SECTION_TYPE_DYNSYM 11
#define ELF_SECTION_TYPE_INIT_ARRAY 14
#define ELF_SECTION_TYPE_FINI_ARRAY 15
#define ELF_SECTION_TYPE_PREINIT_ARRAY 16
#define ELF_SECTION_TYPE_GROUP 17
#define ELF_SECTION_TYPE_SYMTAB_SHNDX 18
#define ELF_SECTION_TYPE_LOOS 0x60000000
#define ELF_SECTION_TYPE_HIOS 0x6FFFFFFF
#define ELF_SECTION_TYPE_LOPROC 0x70000000
#define ELF_SECTION_TYPE_HIPROC 0x7FFFFFFF
#define ELF_SECTION_TYPE_LOUSER 0x80000000
#define ELF_SECTION_TYPE_HIUSER 0xFFFFFFFF

/* 节标志 (sh_flags) */
#define ELF_SECTION_FLAGS_ALLOC (1 << 1)
#define ELF_SECTION_FLAGS_WRITE (1 << 0)
#define ELF_SECTION_FLAGS_EXECINSTR (1 << 2)
#define ELF_SECTION_FLAGS_MERGE (1 << 4)
#define ELF_SECTION_FLAGS_STRINGS (1 << 5)
#define ELF_SECTION_FLAGS_INFO_LINK (1 << 6)
#define ELF_SECTION_FLAGS_LINK_ORDER (1 << 7)
#define ELF_SECTION_FLAGS_OS_NONCONFORMING (1 << 8)
#define ELF_SECTION_FLAGS_GROUP (1 << 9)
#define ELF_SECTION_FLAGS_TLS (1 << 10)
#define ELF_SECTION_FLAGS_COMPRESSED (1 << 11)
#define ELF_SECTION_FLAGS_MASKOS 0x0FF00000
#define ELF_SECTION_FLAGS_MASKPROC 0xF0000000

/* 特殊节索引 */
#define ELF_SPEC_SECTION_NUMBER_UNDEF 0
#define ELF_SPEC_SECTION_NUMBER_LORESERVE 0xFF00
#define ELF_SPEC_SECTION_NUMBER_LOPROC 0xFF00
#define ELF_SPEC_SECTION_NUMBER_HIPROC 0xFF1F
#define ELF_SPEC_SECTION_NUMBER_ABS 0xFFF1
#define ELF_SPEC_SECTION_NUMBER_COMMON 0xFFF2
#define ELF_SPEC_SECTION_NUMBER_HIRESERVE 0xFFFF

/* 常用节名 */
#define ELF_SECTION_TEXT ".text"
#define ELF_SECTION_DATA ".data"
#define ELF_SECTION_BSS ".bss"
#define ELF_SECTION_RODATA ".rodata"
#define ELF_SECTION_SYMTAB ".symtab"
#define ELF_SECTION_STRTAB ".strtab"
#define ELF_SECTION_SHSTRTAB ".shstrtab"
#define ELF_SECTION_DYNAMIC ".dynamic"
#define ELF_SECTION_GOT ".got"
#define ELF_SECTION_PLT ".plt"
#define ELF_SECTION_INIT ".init"
#define ELF_SECTION_FINI ".fini"

typedef struct elf64_part_header {
    uint32_t part_type; // 段类型
    uint32_t part_flags; // 段标志
    uint64_t part_offset; // 段在文件中的偏移
    uint64_t part_vaddr; // 段的虚拟地址
    uint64_t part_paddr; // 段的物理地址
    uint64_t part_file_size; // 段在文件中的大小
    uint64_t part_mem_size; // 段在内存中的大小
    uint64_t part_align; // 段的对齐
} __attribute__((packed)) elf64_part_header_t;

/* 段类型 (p_type) */
#define ELF_PART_TYPE_NULL 0
#define ELF_PART_TYPE_LOAD 1
#define ELF_PART_TYPE_DYNAMIC 2
#define ELF_PART_TYPE_INTERP 3
#define ELF_PART_TYPE_NOTE 4
#define ELF_PART_TYPE_SHLIB 5
#define ELF_PART_TYPE_PHDR 6
#define ELF_PART_TYPE_TLS 7
#define ELF_PART_TYPE_LOOS 0x60000000
#define ELF_PART_TYPE_HIOS 0x6FFFFFFF
#define ELF_PART_TYPE_LOPROC 0x70000000
#define ELF_PART_TYPE_HIPROC 0x7FFFFFFF

/* GNU扩展段类型 */
#define ELF_PART_TYPE_GNU_EH_FRAME 0x6474E550
#define ELF_PART_TYPE_GNU_STACK 0x6474E551
#define ELF_PART_TYPE_GNU_RELRO 0x6474E552
#define ELF_PART_TYPE_GNU_PROPERTY 0x6474E553

/* 段标志 (p_flags) */
#define ELF_PART_FLAGS_X (1 << 0)
#define ELF_PART_FLAGS_W (1 << 1)
#define ELF_PART_FLAGS_R (1 << 2)
#define ELF_PART_FLAGS_MASKOS 0x0FF00000
#define ELF_PART_FLAGS_MASKPROC 0xF0000000

/* 段标志组合 */
#define ELF_PART_FLAGS_RW (ELF_PART_FLAGS_R | ELF_PART_FLAGS_W)
#define ELF_PART_FLAGS_RX (ELF_PART_FLAGS_R | ELF_PART_FLAGS_X)
#define ELF_PART_FLAGS_RWX (ELF_PART_FLAGS_R | ELF_PART_FLAGS_W | ELF_PART_FLAGS_X)

/* 段对齐 */
#define ELF_PAGE_SIZE 4096
#define ELF_SEGMENT_ALIGN 0x1000

#include "fs/fs.h"
elf64_header_t* elf_file_executable(int fd);
int elf_file_copy(int fd, elf64_header_t* header, uint64_t cr3);

#endif