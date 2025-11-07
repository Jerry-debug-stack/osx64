FLAGS    equ  0x7 ; Multiboot标志字段
MAGIC    equ  0x1BADB002        ; Multiboot魔数
CHECKSUM equ -(MAGIC + FLAGS)   ; 校验和

PAGE_ITEM_SMALL equ PAGE_PRESENT|PAGE_WRITABLE|PAGE_WRITE_THROUGH|PAGE_LEVEL_CACHE_ENABLE;
PAGE_ITEM       equ PAGE_ITEM_SMALL|PAGE_BIG_ENTRY

%include "pm.inc"

global _start
global ptable4
global load_protect
global Debug,Nmi,Int3,Overflow,Bounds,UndefinedOpcode,DevNotAvailable,DoubleFault,CoprocessorSegmentOverRun
global DoubleFault,CoprocessorSegmentOverRun,InvalidTSS,SegmentNotPresent,GeneralProtection,PageFault,x87FPUError
global AlignmentCheck,MachineCheck,SIMDException,VirtualizationException,StackSegmentFault,Divide_Error

extern cstart,exception_handler

section .multiboot
align 4
    dd MAGIC
    dd FLAGS
    dd CHECKSUM
    header_addr: dd 0
    load_addr: dd 0
    load_end_addr: dd 0
    bss_end_addr: dd 0
    entry_addr:dd 0
    mode_type:dd 0
    width:dd 1280
    height: dd 720
    depth:dd 32
section .text32
bits 32
_start:
    cmp eax,0x2badb002
    jne .error
    mov esp,stack_top
    mov dword[multiboot_info],ebx
    call test_long_mode
.set_page_table:
    mov ecx,PAGE_ITEM_SMALL
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
        dd 0x0|PAGE_ITEM
        times 1023 dd 0
    align 4096
    ptable2_1:
        %assign addr 0x0
        %rep 256
            dd addr|PAGE_ITEM, 0
            %assign addr addr + 0x200000
        %endrep
        times 512 dd 0
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
        mov rdi,qword[abs multiboot_info]
        call cstart
        jmp $

%macro irq_err_save 0
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rdi
    push rsi

    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rbx,es
    push rbx
    mov rbx,ds
    push rbx

    mov rbx,0x10
    mov es,rbx
    mov ds,rbx
%endmacro

%macro go_out 0
    pop rbx
    mov ds,rbx
    pop rbx
    mov es,rbx
    
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rsi
    pop rdi
    pop rbp
    pop rdx
    pop rcx
    pop rbx
    pop rax
    cli
    iretq
%endmacro

%macro intr 1
    irq_err_save
    ;call [rel intr_handler + %1 * 8]
    go_out
%endmacro

align 16
Divide_Error:
    push -1
    push 0
    irq_err_save
    jmp handler
align 16
Debug:
    push -1
    push 1
    irq_err_save
    jmp handler
align 16
Nmi:
    push -1
    push 2
    irq_err_save
    jmp handler
align 16
Int3:
    push -1
    push 3
    irq_err_save
    jmp handler
align 16
Overflow:
    push -1
    push 4
    irq_err_save
    jmp handler
align 16
Bounds:
    push -1
    push 5
    irq_err_save
    jmp handler
align 16
UndefinedOpcode:
    push -1
    push 6
    irq_err_save
    jmp handler
align 16
DevNotAvailable:
    push -1
    push 7
    irq_err_save
    jmp handler
align 16
DoubleFault:
    push -1
    push 8
    irq_err_save
    jmp handler
align 16
CoprocessorSegmentOverRun:
    push -1
    push 9
    irq_err_save
    jmp handler
align 16
InvalidTSS:
    push 10
    irq_err_save
    jmp handler
align 16
SegmentNotPresent:
    push 11
    irq_err_save
    jmp handler
align 16
StackSegmentFault:
    push 12
    irq_err_save
    jmp handler
align 16
GeneralProtection:
    push 13
    irq_err_save
    jmp handler
align 16
PageFault:
    push 14
    irq_err_save
    jmp handler
align 16
x87FPUError:
    push -1
    push 16
    irq_err_save
    jmp handler
align 16
AlignmentCheck:
    push -1
    push 17
    irq_err_save
    jmp handler
align 16
MachineCheck:
    push -1
    push 18
    irq_err_save
    jmp handler
align 16
SIMDException:
    push -1
    push 19
    irq_err_save
    jmp handler
align 16
VirtualizationException:
    push -1
    push 20
    irq_err_save
    jmp handler
handler:
    call exception_handler
    pop rax
    mov ds,rax
    pop rax
    mov es,rax
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rsi
    pop rdi
    pop rbp
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp,16
    iretq

load_protect:
    lgdt [rdi]
    lidt [rsi]
    mov ax,(5<<3)|0
    ltr ax
    ret

section .kernel.bss nobits alloc
    align 16
    kernel_stack_bottom:
        resb 4096 ;栈空间
    kernel_stack_top:
