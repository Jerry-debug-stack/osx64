#include "protect.h"
#include "const.h"
#include "machine/apic.h"
#include "mm/mm.h"
#include "string.h"
#include "view/view.h"

/* 所有的异常处理函数 */
void Divide_Error(void);
void Debug(void);
void Nmi(void);
void Int3(void);
void Overflow(void);
void Bounds(void);
void UndefinedOpcode(void);
void DevNotAvailable(void);
void DoubleFault(void);
void CoprocessorSegmentOverRun(void);
void InvalidTSS(void);
void SegmentNotPresent(void);
void StackSegmentFault(void);
void GeneralProtection(void);
void PageFault(void);
void x87FPUError(void);
void AlignmentCheck(void);
void MachineCheck(void);
void SIMDException(void);
void VirtualizationException(void);

void intr0(void);
void intr1(void);
void intr2(void);
void intr2_bsp(void);
void intr3(void);
void intr4(void);
void intr5(void);
void intr6(void);
void intr7(void);
void intr8(void);
void intr9(void);
void intr10(void);
void intr11(void);
void intr12(void);
void intr13(void);
void intr14(void);
void intr15(void);
void intr16(void);
void intr17(void);
void intr18(void);
void intr19(void);
void intr20(void);
void intr21(void);
void intr22(void);
void intr23(void);

void load_protect(uint32_t* gdt_ptr, uint32_t* idt_ptr);

void make_idt_descriptor(uint64_t* idt_table, uint32_t n, uint64_t addr, uint64_t ist, uint64_t dpl, uint64_t type);

void init_protect(void)
{
    uint64_t* gdt_table = kmalloc(64);
    uint64_t* idt_table = kmalloc(4096);
    uint32_t* gdt_ptr = kmalloc(16);
    uint32_t* idt_ptr = kmalloc(32);
    uint32_t* tss = kmalloc(132);
    memset(gdt_table, 0, 64);
    memset(idt_table, 0, 4096);
    memset(gdt_ptr, 0, 16);
    memset(idt_ptr, 0, 32);
    memset(tss, 0, 132);
    gdt_table[1] = ((uint64_t)(ACCESS_ACCESSED | ACCESS_CODE_DATA | ACCESS_CODE | ACCESS_CODE_READABLE | ACCESS_PRESENT | FLAGS_LOOG | ACCESS_SYSTEM)) << 32;
    gdt_table[2] = ((uint64_t)(ACCESS_ACCESSED | ACCESS_CODE_DATA | ACCESS_DATA | ACCESS_DATA_WRITABLE | ACCESS_PRESENT | ACCESS_DIRECTION_UP | FLAGS_LOOG | ACCESS_SYSTEM)) << 32;
    gdt_table[3] = ((uint64_t)(ACCESS_ACCESSED | ACCESS_CODE_DATA | ACCESS_CODE | ACCESS_CODE_READABLE | ACCESS_PRESENT | FLAGS_LOOG | ACCESS_DPL3)) << 32;
    gdt_table[4] = ((uint64_t)(ACCESS_ACCESSED | ACCESS_CODE_DATA | ACCESS_DATA | ACCESS_DATA_WRITABLE | ACCESS_PRESENT | ACCESS_DIRECTION_UP | FLAGS_LOOG | ACCESS_DPL3)) << 32;
    gdt_table[5] = (((uint64_t)(tss) & 0xFF000000) << 32) | (0xe90000000000) | (((uint64_t)(tss) & 0xFFFFFF) << 16) | sizeof(struct tss);
    gdt_table[6] = ((uint64_t)(tss) >> 32) & 0xFFFFFFFF;
    *((uint16_t*)(&gdt_ptr[0])) = 7 * 8 - 1;
    *((uint64_t*)((uint64_t)gdt_ptr + 2)) = (uint64_t)gdt_table;
    *((uint16_t*)(&idt_ptr[0])) = 8 * 2 * 256 - 1;
    *((uint64_t*)((uint64_t)idt_ptr + 2)) = (uint64_t)idt_table;

    make_idt_descriptor(idt_table, 0, (uint64_t)Divide_Error, 0, 3, IDT_INTERRUPT_GATE);
    make_idt_descriptor(idt_table, 1, (uint64_t)Debug, 0, 3, IDT_INTERRUPT_GATE);
    make_idt_descriptor(idt_table, 2, (uint64_t)Nmi, 1, 3, IDT_INTERRUPT_GATE);
    make_idt_descriptor(idt_table, 3, (uint64_t)Int3, 0, 3, IDT_INTERRUPT_GATE);
    make_idt_descriptor(idt_table, 4, (uint64_t)Overflow, 0, 3, IDT_INTERRUPT_GATE);
    make_idt_descriptor(idt_table, 5, (uint64_t)Bounds, 0, 3, IDT_INTERRUPT_GATE);
    make_idt_descriptor(idt_table, 6, (uint64_t)UndefinedOpcode, 0, 3, IDT_INTERRUPT_GATE);
    make_idt_descriptor(idt_table, 7, (uint64_t)DevNotAvailable, 0, 3, IDT_INTERRUPT_GATE);
    make_idt_descriptor(idt_table, 8, (uint64_t)DoubleFault, 1, 3, IDT_INTERRUPT_GATE);
    make_idt_descriptor(idt_table, 9, (uint64_t)CoprocessorSegmentOverRun, 0, 3, IDT_INTERRUPT_GATE);
    make_idt_descriptor(idt_table, 10, (uint64_t)InvalidTSS, 1, 3, IDT_INTERRUPT_GATE);
    make_idt_descriptor(idt_table, 11, (uint64_t)SegmentNotPresent, 0, 3, IDT_INTERRUPT_GATE);
    make_idt_descriptor(idt_table, 12, (uint64_t)StackSegmentFault, 0, 3, IDT_INTERRUPT_GATE);
    make_idt_descriptor(idt_table, 13, (uint64_t)GeneralProtection, 0, 3, IDT_INTERRUPT_GATE);
    make_idt_descriptor(idt_table, 14, (uint64_t)PageFault, 0, 3, IDT_INTERRUPT_GATE);
    make_idt_descriptor(idt_table, 16, (uint64_t)x87FPUError, 0, 3, IDT_INTERRUPT_GATE);
    make_idt_descriptor(idt_table, 17, (uint64_t)AlignmentCheck, 0, 3, IDT_INTERRUPT_GATE);
    make_idt_descriptor(idt_table, 18, (uint64_t)MachineCheck, 1, 3, IDT_INTERRUPT_GATE);
    make_idt_descriptor(idt_table, 19, (uint64_t)SIMDException, 0, 3, IDT_INTERRUPT_GATE);
    make_idt_descriptor(idt_table, 20, (uint64_t)VirtualizationException, 0, 3, IDT_INTERRUPT_GATE);

    make_idt_descriptor(idt_table, INTERRUPT_VECTOR_8259A_MASTER, (unsigned long)intr0, 0, 3, IDT_INTERRUPT_GATE);
    make_idt_descriptor(idt_table, INTERRUPT_VECTOR_KEYBOARD, (unsigned long)intr1, 0, 3, IDT_INTERRUPT_GATE);
    make_idt_descriptor(idt_table, INTERRUPT_VECTOR_TIMER, (unsigned long)intr2_bsp, 0, 3, IDT_INTERRUPT_GATE);
    make_idt_descriptor(idt_table, INTERRUPT_VECTOR_SERIAL_2_4, (unsigned long)intr3, 0, 3, IDT_INTERRUPT_GATE);
    make_idt_descriptor(idt_table, INTERRUPT_VECTOR_SERIAL_1_3, (unsigned long)intr4, 0, 3, IDT_INTERRUPT_GATE);
    make_idt_descriptor(idt_table, INTERRUPT_VECTOR_PARALLEL_2, (unsigned long)intr5, 0, 3, IDT_INTERRUPT_GATE);
    make_idt_descriptor(idt_table, INTERRUPT_VECTOR_CDROM, (unsigned long)intr6, 0, 3, IDT_INTERRUPT_GATE);
    make_idt_descriptor(idt_table, INTERRUPT_VECTOR_PARALLEL_1, (unsigned long)intr7, 0, 3, IDT_INTERRUPT_GATE);
    make_idt_descriptor(idt_table, INTERRUPT_VECTOR_RTC_HPET_1, (unsigned long)intr8, 0, 3, IDT_INTERRUPT_GATE);
    make_idt_descriptor(idt_table, INTERRUPT_VECTOR_9, (unsigned long)intr9, 0, 3, IDT_INTERRUPT_GATE);
    make_idt_descriptor(idt_table, INTERRUPT_VECTOR_10, (unsigned long)intr10, 0, 3, IDT_INTERRUPT_GATE);
    make_idt_descriptor(idt_table, INTERRUPT_VECTOR_HPET_2, (unsigned long)intr11, 0, 3, IDT_INTERRUPT_GATE);
    make_idt_descriptor(idt_table, INTERRUPT_VECTOR_HPET_3_MOUSE_PS2, (unsigned long)intr12, 0, 3, IDT_INTERRUPT_GATE);
    make_idt_descriptor(idt_table, INTERRUPT_VECTOR_FERR_DMA, (unsigned long)intr13, 0, 3, IDT_INTERRUPT_GATE);
    make_idt_descriptor(idt_table, INTERRUPT_VECTOR_MASTER_SATA, (unsigned long)intr14, 0, 3, IDT_INTERRUPT_GATE);
    make_idt_descriptor(idt_table, INTERRUPT_VECTOR_SLAVE_SATA, (unsigned long)intr15, 0, 3, IDT_INTERRUPT_GATE);
    make_idt_descriptor(idt_table, INTERRUPT_VECTOR_PIRQA, (unsigned long)intr16, 0, 3, IDT_INTERRUPT_GATE);
    make_idt_descriptor(idt_table, INTERRUPT_VECTOR_PIRQB, (unsigned long)intr17, 0, 3, IDT_INTERRUPT_GATE);
    make_idt_descriptor(idt_table, INTERRUPT_VECTOR_PIRQC, (unsigned long)intr18, 0, 3, IDT_INTERRUPT_GATE);
    make_idt_descriptor(idt_table, INTERRUPT_VECTOR_PIRQD, (unsigned long)intr19, 0, 3, IDT_INTERRUPT_GATE);
    make_idt_descriptor(idt_table, INTERRUPT_VECTOR_PIRQE, (unsigned long)intr20, 0, 3, IDT_INTERRUPT_GATE);
    make_idt_descriptor(idt_table, INTERRUPT_VECTOR_PIRQF, (unsigned long)intr21, 0, 3, IDT_INTERRUPT_GATE);
    make_idt_descriptor(idt_table, INTERRUPT_VECTOR_PIRQG, (unsigned long)intr22, 0, 3, IDT_INTERRUPT_GATE);
    make_idt_descriptor(idt_table, INTERRUPT_VECTOR_PIRQH, (unsigned long)intr23, 0, 3, IDT_INTERRUPT_GATE);

    uint64_t* phy_addr = (uint64_t*)(alloc_page_4k() + 0x1000);
    ((TSS*)tss)->ist1_low = (uint32_t)((uint64_t)easy_phy2linear(phy_addr) & 0xffffffff);
    ((TSS*)tss)->ist1_high = (uint32_t)((uint64_t)easy_phy2linear(phy_addr) >> 32);
    load_protect(gdt_ptr, idt_ptr);
}

void make_idt_descriptor(uint64_t* idt_table, uint32_t n, uint64_t addr, uint64_t ist, uint64_t dpl, uint64_t type)
{
    if (n > 256)
        return;
    *(idt_table + 2 * n) = ((addr & 0xFFFF0000) << 32) | (0x800000000000) | (dpl << 45) | (type << 40) | (ist << 32) | (SELECTOR_KERNEL_CS << 16) | (addr & 0xFFFF);
    *(idt_table + 2 * n + 1) = addr >> 32;
}

void exception_handler(
    UNUSED unsigned long ignore1, UNUSED unsigned long ignore2, UNUSED unsigned long ignore3, UNUSED unsigned long ignore4, UNUSED unsigned long ignore5, UNUSED unsigned long ignore6,
    UNUSED unsigned long ignore7, UNUSED unsigned long ignore8, UNUSED unsigned long ignore9, UNUSED unsigned long ignore10, UNUSED unsigned long ignore11, UNUSED unsigned long ignore12,
    UNUSED unsigned long ignore13, UNUSED unsigned long ignore14, UNUSED unsigned long ignore15, UNUSED unsigned long ignore16, UNUSED unsigned long ignore17, UNUSED unsigned long ignore18,
    UNUSED unsigned long ignore19, UNUSED unsigned long ignore20, UNUSED unsigned long ignore21, UNUSED unsigned long ignore22, UNUSED unsigned long ignore23,
    unsigned long interrupt_num, unsigned long error_no,
    unsigned long rip, unsigned long cs, unsigned long rflags,
    unsigned long rsp, unsigned long ss)
{
    __asm__ __volatile__("cli");
    low_printf("ERROR HAPPENDED\nIntr:%d err_no:%#lx\ncs:%#lx rip:%#lx\nss:%#lx rsp:%#lx\nrflags:%#lx",
        VIEW_COLOR_RED, VIEW_COLOR_WHITE, interrupt_num, error_no, cs, rip, ss, rsp, rflags);
    while (1) {
        __asm__ __volatile__("cli;hlt;");
    }
}
