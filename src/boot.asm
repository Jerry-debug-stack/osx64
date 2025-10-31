; 常量定义
MBALIGN  equ  1<<1              ; 对齐加载的模块
MEMINFO  equ  1<<1              ; 提供内存映射
FLAGS    equ  MBALIGN | MEMINFO ; Multiboot标志字段
MAGIC    equ  0x1BADB002        ; Multiboot魔数
CHECKSUM equ -(MAGIC + FLAGS)   ; 校验和
%include "pm.inc"

global _start
extern cstart

section .multiboot
align 4
    dd MAGIC
    dd FLAGS
    dd CHECKSUM

section .text32
bits 32
_start:
;过程中不能改变rbx
    ;cmp eax,0x628936d7
    ;jne .error
    mov esp,stack_top
    call test_long_mode
.set_page_table:
    mov ecx,PAGE_USED|PAGE_WRITABLE|PAGE_WRITE_THROUGH|PAGE_LEVEL_CACHE_ENABLE;|PAGE_SYSTEM_MODE
    mov eax,ecx
    or eax,ptable3_0
    mov dword[ptable4],eax
    mov eax,ecx
    or eax,ptable3_1
    mov dword[ptable4 + 2048],eax
    mov eax,ecx
    or eax,ptable2_0
    mov dword[ptable3_0],eax
    mov eax,ecx
    or eax,ptable2_1
    mov dword[ptable3_1],eax
.enter64bit:
    db 0x66
    lgdt[GDTR64]
    mov eax,cr4
    bts eax,5
    mov cr4,eax
    mov eax,ptable4
    mov cr3,eax
    mov ecx,0xc0000080
    rdmsr
    bts eax,8
    wrmsr
    mov eax,cr0
    bts eax,0
    bts eax,31
    mov cr0,eax
    jmp SELECTOR_KERNEL_CS:long_mode_low
.error:
    jmp $

test_long_mode:
    mov eax,0x80000000
    cpuid
    cmp eax,0x80000001
    setnb al
    jb .1
    mov eax,0x80000001
    cpuid
    bt edx,29
    setc al
.1:
    movzx eax,al
    ret
.no_support:
    jmp $

section .text64low
bits 64
    long_mode_low:
        mov rax,SELECTOR_KERNEL_DS
        mov ds,rax
        mov es,rax
        mov ss,rax
        mov fs,rax
        mov rax,long_mode_high
        jmp rax

section .data
    align 4096
    ptable4: times 1024 dd 0
    align 4096
    ptable3_0:times 1024 dd 0
    align 4096
    ptable3_1:times 1024 dd 0
    align 4096
    ptable2_0:
        dd 0x0|PAGE_USED|PAGE_WRITABLE|PAGE_WRITE_THROUGH|PAGE_LEVEL_CACHE_ENABLE|PAGE_BIG_ENTRY;|PAGE_SYSTEM_MODE
        times 1023 dd 0
    align 4096
    ptable2_1:
        dd 0x0|PAGE_USED|PAGE_WRITABLE|PAGE_WRITE_THROUGH|PAGE_LEVEL_CACHE_ENABLE|PAGE_BIG_ENTRY;|PAGE_SYSTEM_MODE
        dd 0
        dd 0x200000|PAGE_USED|PAGE_WRITABLE|PAGE_WRITE_THROUGH|PAGE_LEVEL_CACHE_ENABLE|PAGE_BIG_ENTRY;|PAGE_SYSTEM_MODE
        dd 0
        dd 0x400000|PAGE_USED|PAGE_WRITABLE|PAGE_WRITE_THROUGH|PAGE_LEVEL_CACHE_ENABLE|PAGE_BIG_ENTRY;|PAGE_SYSTEM_MODE
        dd 0
        dd 0x600000|PAGE_USED|PAGE_WRITABLE|PAGE_WRITE_THROUGH|PAGE_LEVEL_CACHE_ENABLE|PAGE_BIG_ENTRY;|PAGE_SYSTEM_MODE
        dd 0
        dd 0x800000|PAGE_USED|PAGE_WRITABLE|PAGE_WRITE_THROUGH|PAGE_LEVEL_CACHE_ENABLE|PAGE_BIG_ENTRY;|PAGE_SYSTEM_MODE
        dd 0
        dd 0xa00000|PAGE_USED|PAGE_WRITABLE|PAGE_WRITE_THROUGH|PAGE_LEVEL_CACHE_ENABLE|PAGE_BIG_ENTRY;|PAGE_SYSTEM_MODE
        dd 0
        dd 0xc00000|PAGE_USED|PAGE_WRITABLE|PAGE_WRITE_THROUGH|PAGE_LEVEL_CACHE_ENABLE|PAGE_BIG_ENTRY;|PAGE_SYSTEM_MODE
        dd 0
        dd 0xe00000|PAGE_USED|PAGE_WRITABLE|PAGE_WRITE_THROUGH|PAGE_LEVEL_CACHE_ENABLE|PAGE_BIG_ENTRY;|PAGE_SYSTEM_MODE
        dd 0
        dd 0x1000000|PAGE_USED|PAGE_WRITABLE|PAGE_WRITE_THROUGH|PAGE_LEVEL_CACHE_ENABLE|PAGE_BIG_ENTRY;|PAGE_SYSTEM_MODE
        dd 0
        dd 0x1200000|PAGE_USED|PAGE_WRITABLE|PAGE_WRITE_THROUGH|PAGE_LEVEL_CACHE_ENABLE|PAGE_BIG_ENTRY;|PAGE_SYSTEM_MODE
        dd 0
        dd 0x1400000|PAGE_USED|PAGE_WRITABLE|PAGE_WRITE_THROUGH|PAGE_LEVEL_CACHE_ENABLE|PAGE_BIG_ENTRY;|PAGE_SYSTEM_MODE
        dd 0
        dd 0x1600000|PAGE_USED|PAGE_WRITABLE|PAGE_WRITE_THROUGH|PAGE_LEVEL_CACHE_ENABLE|PAGE_BIG_ENTRY;|PAGE_SYSTEM_MODE
        dd 0
        dd 0x1800000|PAGE_USED|PAGE_WRITABLE|PAGE_WRITE_THROUGH|PAGE_LEVEL_CACHE_ENABLE|PAGE_BIG_ENTRY;|PAGE_SYSTEM_MODE
        dd 0
        dd 0x1a00000|PAGE_USED|PAGE_WRITABLE|PAGE_WRITE_THROUGH|PAGE_LEVEL_CACHE_ENABLE|PAGE_BIG_ENTRY;|PAGE_SYSTEM_MODE
        dd 0
        dd 0x1c00000|PAGE_USED|PAGE_WRITABLE|PAGE_WRITE_THROUGH|PAGE_LEVEL_CACHE_ENABLE|PAGE_BIG_ENTRY;|PAGE_SYSTEM_MODE
        dd 0
        dd 0x1e00000|PAGE_USED|PAGE_WRITABLE|PAGE_WRITE_THROUGH|PAGE_LEVEL_CACHE_ENABLE|PAGE_BIG_ENTRY;|PAGE_SYSTEM_MODE
        dd 0
        times 1024-32 dd 0
    multiboot_info: dd 0,0
    ALIGN 32
    ;64bitGDT等
    GDT64_0:dq 0
    GDT64_KERNEL_CS:dd 0,ACCESS_ACCESSED|ACCESS_CODE_DATA|ACCESS_CODE|ACCESS_CODE_READABLE|ACCESS_PRESENT|FLAGS_LOOG
    GDT64_KERNEL_DS:dd 0,ACCESS_ACCESSED|ACCESS_CODE_DATA|ACCESS_DATA|ACCESS_DATA_WRITABLE|ACCESS_PRESENT|ACCESS_DIRECTION_UP|FLAGS_LOOG
    GDTR64: dw 23
            dq GDT64_0
section .bss
    align 16
    stack_bottom:
        resb 4096 ;栈空间
    stack_top:

section .text64high
bits 64
    long_mode_high:
        mov rsp,kernel_stack_top
        mov rdi,rbx
        call cstart
        jmp $

section .kernel.bss
    align 16
    kernel_stack_bottom:
        resb 4096 ;栈空间
    kernel_stack_top:
