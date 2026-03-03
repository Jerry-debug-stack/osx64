#include "lib/elf.h"
#include "const.h"
#include "fs/fs.h"
#include "fs/fsmod.h"
#include "mm/mm.h"
#include "lib/string.h"
#include "view/view.h"
#include "lib/io.h"

const char elfMagic[] = { 0x7f, 'E', 'L', 'F' };

elf64_header_t* elf_file_executable(int fd)
{
    elf64_header_t* header = kmalloc(sizeof(elf64_header_t));
    memset(header,0,sizeof(header));
    sys_read(fd,(void*)header,sizeof(elf64_header_t));
    if (!memcmp((void*)&elfMagic[0], &header->elf_ident[0], 4))
        goto fail;
    if (header->elf_ident[ELF_INDEX_BITSIZE] != ELF_BITSIZE_64)
        goto fail;
    if (header->elf_ident[ELF_INDEX_BITORDER] != ELF_BITORDER_SMALL)
        goto fail;
    if (header->elf_type != ELF_TYPE_EXEC)
        goto fail;
    if (header->elf_machine != ELF_MACHINE_X86_64)
        goto fail;
    elf64_part_header_t* p_header_tbl = kmalloc(sizeof(elf64_part_header_t) * header->elf_part_header_num);
    memset(p_header_tbl,0,sizeof(elf64_header_t));
    sys_lseek(fd,header->elf_part_header_offset,SEEK_SET);
    sys_read(fd,(void*)p_header_tbl,sizeof(elf64_header_t));
    for (int i = 0; i < header->elf_part_header_num; i++) {
        if (p_header_tbl[i].part_vaddr >= VIRTUAL_ADDR_USER_ELF_HIGHEST)
            goto fail1;
        if (p_header_tbl[i].part_vaddr + p_header_tbl[i].part_mem_size < p_header_tbl[i].part_vaddr)
            goto fail1;
        if (p_header_tbl[i].part_vaddr + p_header_tbl[i].part_mem_size >= VIRTUAL_ADDR_USER_ELF_HIGHEST)
            goto fail1;
    }
    kfree(p_header_tbl);
    return header;
fail1:
    kfree(p_header_tbl);
fail:
    kfree(header);
    return (void*)0;
}

int elf_file_copy(int fd, elf64_header_t* header, uint64_t cr3)
{
    elf64_part_header_t* p_header_tbl = kmalloc(sizeof(elf64_part_header_t) * header->elf_part_header_num);
    sys_lseek(fd,header->elf_part_header_offset,SEEK_SET);
    sys_read(fd,(void*)p_header_tbl,sizeof(elf64_part_header_t) * header->elf_part_header_num);
    uint64_t start_addr, end_addr;
    uint64_t temp;
    for (int i = 0; i < header->elf_part_header_num; i++) {
        if (p_header_tbl[i].part_type == ELF_PART_TYPE_LOAD) {
            start_addr = p_header_tbl[i].part_vaddr & 0xfffffffffffff000;
            end_addr = (p_header_tbl[i].part_vaddr + p_header_tbl[i].part_mem_size + 0xfff) & 0xfffffffffffff000;
            for (uint64_t j = start_addr; j <= end_addr; j += 4096) {
                if (!exist_page_4k(j,cr3)) {
                    temp = (uint64_t)easy_linear2phy(kmalloc(4096));
                    uint8_t intr = io_cli();
                    put_page_4k(temp,j,cr3,1);
                    io_set_intr(intr);
                }
            }
            sys_lseek(fd,p_header_tbl[i].part_offset,SEEK_SET);
            sys_read(fd,(void*)p_header_tbl[i].part_vaddr,p_header_tbl[i].part_file_size);
        }
    }
    return 0;
}
