#ifndef OS_PROTECT_H
#define OS_PROTECT_H

#include <stdint.h>

typedef struct CPUCore {

} CPU_CORE;

/* Gate type */
#define IDT_INTERRUPT_GATE 0xE
#define IDT_TRAP_GATE 0xF

#define INT_CLOCK 0x0
#define INT_KEYBOARD 0x1
#define INT_SLAVE 0x2
#define INT_CHUAN2 0x3
#define INT_CHUAN1 0x4
#define INT_LPT2 0x5
#define INT_CD 0x6
#define INT_LPT1 0x7
#define INT_REAL_TIME_CLOCK 0x8
#define INT_REDIRECT_IRQ2 0x9
#define INT_PS2_MOUSE 0xc
#define INT_FPU_ERROR 0xd
#define INT_ATA_WINCHESTER 0xf

/* 中断向量 */
#define INT_VECTOR_IRQ0 0x20
#define INT_VECTOR_IRQ8 0x28

/* 8259A interrupt controller ports. */
#define INT_M_CTL 0x20 /* I/O port for interrupt controller         <Master> */
#define INT_M_CTLMASK 0x21 /* setting bits in this port disables ints   <Master> */
#define INT_S_CTL 0xA0 /* I/O port for second interrupt controller  <Slave>  */
#define INT_S_CTLMASK 0xA1 /* setting bits in this port disables ints   <Slave>  */

typedef struct tss {
    uint32_t reserved0;

    /* RSP stacks used when privilege level changes */
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;

    uint64_t reserved1;

    /* Interrupt Stack Table */
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;

    uint64_t reserved2;
    uint16_t reserved3;

    /* I/O bitmap offset */
    uint16_t io_map_base;
} __attribute__((packed)) tss_t;

// Access Byte
#define ACCESS_PRESENT 1 << 15
#define ACCESS_DPL1 1 << 13
#define ACCESS_DPL2 2 << 13
#define ACCESS_DPL3 3 << 13
#define ACCESS_SYSTEM 1 << 12
#define ACCESS_CODE_DATA 1 << 12
#define ACCESS_CODE 1 << 11
#define ACCESS_DATA 0
#define ACCESS_DIRECTION_UP 0
#define ACCESS_DIRECTION_DOWN 1 << 10
#define ACCESS_EXACT 0
#define ACCESS_LOWER 1 << 10
#define ACCESS_CODE_READABLE 1 << 9
#define ACCESS_CODE_INREADABLE 0
#define ACCESS_DATA_INWRITABLE 0
#define ACCESS_DATA_WRITABLE 1 << 9
#define ACCESS_ACCESSED 1 << 8
// Flags
#define FLAGS_1BYTE 0
#define FLAGS_4KB 1 << 23
#define FLAGS_32BIT 1 << 22
#define FLAGS_16BIT 0
#define FLAGS_LOOG 1 << 21

// 典型的错误代码场景
#define PAGEFAULT_PRESENT (1 << 0)
#define PAGEFAULT_WRITE (1 << 1)
#define PAGEFAULT_USER (1 << 2)
#define PAGEFAULT_RSVD (1 << 3)
#define PAGEFAULT_INSTRUCTION (1 << 4)

/* ========== 控制寄存器位掩码 ========== */
/* CR0 位 */
#define CR0_PE      (1 << 0)   /* Protection Enable */
#define CR0_MP      (1 << 1)   /* Monitor Coprocessor */
#define CR0_EM      (1 << 2)   /* Emulation */
#define CR0_TS      (1 << 3)   /* Task Switched */
#define CR0_ET      (1 << 4)   /* Extension Type */
#define CR0_NE      (1 << 5)   /* Numeric Error */
#define CR0_WP      (1 << 16)  /* Write Protect */
#define CR0_AM      (1 << 18)  /* Alignment Mask */
#define CR0_NW      (1 << 29)  /* Not Write-through */
#define CR0_CD      (1 << 30)  /* Cache Disable */
#define CR0_PG      (1 << 31)  /* Paging */

/* CR4 位 */
#define CR4_VME     (1 << 0)   /* Virtual-8086 Mode Extensions */
#define CR4_PVI     (1 << 1)   /* Protected-Mode Virtual Interrupts */
#define CR4_TSD     (1 << 2)   /* Time Stamp Disable */
#define CR4_DE      (1 << 3)   /* Debugging Extensions */
#define CR4_PSE     (1 << 4)   /* Page Size Extensions */
#define CR4_PAE     (1 << 5)   /* Physical Address Extension */
#define CR4_MCE     (1 << 6)   /* Machine-Check Enable */
#define CR4_PGE     (1 << 7)   /* Page Global Enable */
#define CR4_PCE     (1 << 8)   /* Performance-Monitoring Counter Enable */
#define CR4_OSFXSR  (1 << 9)   /* OS Support for FXSAVE/FXRSTOR */
#define CR4_OSXMMEXCPT (1 << 10) /* OS Support for Unmasked SIMD FPU Exceptions */
#define CR4_UMIP    (1 << 11)  /* User-Mode Instruction Prevention */
#define CR4_VMXE    (1 << 13)  /* VMX Enable */
#define CR4_SMXE    (1 << 14)  /* SMX Enable */
#define CR4_FSGSBASE (1 << 16) /* FSGSBASE Enable */
#define CR4_PCIDE   (1 << 17)  /* PCID Enable */
#define CR4_OSXSAVE (1 << 18)  /* XSAVE and Processor Extended States Enable */
#define CR4_SMEP    (1 << 20)  /* SMEP Enable */
#define CR4_SMAP    (1 << 21)  /* SMAP Enable */
#define CR4_PKE     (1 << 22)  /* Protection Key Enable */

#endif
