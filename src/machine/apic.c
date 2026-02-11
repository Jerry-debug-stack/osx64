#include "machine/apic.h"
#include "const.h"
#include "machine/pcie/pcie.h"
#include "mm/mm.h"
#include "protect.h"
#include "lib/string.h"
#include "view/view.h"
#include "machine/cpu.h"
extern GLOBAL_CPU *cpus;

uint32_t* LocalAPIC;
IO_APIC IoAPIC;

void make_idt_descriptor(uint64_t* idt_table, uint32_t n, uint64_t addr, uint64_t ist, uint64_t dpl, uint64_t type);
static void default_intr_soft(void);

void disable_irq(uint16_t irq);
void enable_irq(uint16_t irq);

static void write_io_apic(uint64_t Register, uint64_t Data);
static uint64_t read_io_apic(uint64_t Register);

uint64_t intr_handler[24];

void init_apic_ap(){
    LocalAPIC = (uint32_t*)easy_phy2linear(PHYSIC_ADDR_LOCAL_APIC);
    uint32_t a, b, c, d;
    /* Local APIC */
    __asm__ __volatile__("cpuid;" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "0"(0), "2"(1));
    __asm__ __volatile__("movw %0,%%dx;outb %%al,%%dx;" ::"i"(0x22), "al"(0x70) :);
    __asm__ __volatile__("movw %0,%%dx;outb %%al,%%dx;" ::"i"(0x23), "al"(0x01) :);
    /* Disable i8259a */
    __asm__ __volatile__("movw %0,%%dx;outb %%al,%%dx;" ::"i"(0x21), "al"(0xff) :);
    __asm__ __volatile__("movw %0,%%dx;outb %%al,%%dx;" ::"i"(0xa1), "al"(0xff) :);

    unsigned long low, high,content;
    __asm__ __volatile__("rdmsr;" : "=a"(low), "=d"(high) : "c"(IA32_APIC_BASE));
    content = low | (high << 32) | IA32_APIC_BASE_ENABLE_xAPIC;
    __asm__ __volatile__("wrmsr;" ::"a"(content), "d"(content >> 32), "c"(IA32_APIC_BASE));
    // LocalAPIC[SVR] |= SVR_PROHIBIT_BTOADCAST_EOI | SVR_ENABLE_APIC;
    LocalAPIC[SVR] |= SVR_ENABLE_APIC;
    LocalAPIC[LVTTimer] = LVT_MASKED | TIMER_MODE_PERIODIC | INTERRUPT_VECTOR_TIMER;
    LocalAPIC[LVTCMCI] = LVT_MASKED | DILIVERY_MODE_FIXED | 0;
    LocalAPIC[LVTLINT0] = LVT_MASKED | DILIVERY_MODE_FIXED | 0 | INTERRUPT_INPUT_PIN_POSITIVE;
    LocalAPIC[LVTLINT1] = LVT_MASKED | DILIVERY_MODE_FIXED | 0 | INTERRUPT_INPUT_PIN_POSITIVE;
    LocalAPIC[LVTFault] = LVT_MASKED | 0;
    LocalAPIC[LVTPerformance] = LVT_MASKED | DILIVERY_MODE_FIXED | 0;
    LocalAPIC[LVTTemperature] = LVT_MASKED | DILIVERY_MODE_FIXED | 0;
}

void init_apic_bsp(void)
{
    init_apic_ap();
    /* I/O APIC */
    IoAPIC.RegisterSelect = (uint8_t*)easy_phy2linear(IO_REGISTER_SELECT);
    IoAPIC.Data = (uint32_t*)easy_phy2linear(IO_WINDOW);
    IoAPIC.BspApicId = (uint64_t)LocalAPIC[LocalAPICId] >> 24;
    IoAPIC.IoEOI = (uint32_t*)easy_phy2linear(IO_EOI_REGISTER);
    uint64_t chip_configuation = (uint64_t)easy_phy2linear(read_pcie(MAKE_PCIE_ADDR(0, 31, 0, 0xf0)) & 0xfffffc000);
    /* OIC寄存器开启IO APIC */
    *((uint32_t*)(chip_configuation + 0x31fe)) = 1 << 8;
    IoAPIC.RTENumber = (read_io_apic(IO_APIC_VERSION) >> 16) + 1;
    for (uint64_t i = 0; i < IoAPIC.RTENumber; i++) {
        write_io_apic(IO_APIC_REDIRECTION_TABLE_HIGH(i), IoAPIC.BspApicId << 24);
        write_io_apic(IO_APIC_REDIRECTION_TABLE_LOW(i), LVT_MASKED | (0x20 + i));
        intr_handler[i] = (uint64_t)default_intr_soft;
    }
    /* 对时钟的初始化，直接通过ioapic进行广播 */
    write_io_apic(IO_APIC_REDIRECTION_TABLE_HIGH(2),0xff000000);
    write_io_apic(IO_APIC_REDIRECTION_TABLE_LOW(2),LVT_MASKED | 0x22);
}

/// @brief 设置中断处理程序
/// @param irq 中断号
/// @param addr 处理程序地址
void set_handler(uint64_t irq, uint64_t addr)
{
    if(irq < 24){
        intr_handler[irq] = addr;   
    }
}

/// @brief 不允许某中断
/// @param irq 中断号
void disable_irq(uint16_t irq)
{
    write_io_apic(IO_APIC_REDIRECTION_TABLE_LOW(irq), (read_io_apic(IO_APIC_REDIRECTION_TABLE_LOW(irq)) | (LVT_MASKED)));
}

/// @brief 允许某中断
/// @param irq
void enable_irq(uint16_t irq)
{
    write_io_apic(IO_APIC_REDIRECTION_TABLE_LOW(irq), (read_io_apic(IO_APIC_REDIRECTION_TABLE_LOW(irq)) & (~((uint64_t)LVT_MASKED))));
}

void set_EOI()
{
    LocalAPIC[EOI] = 0;
}

/// @brief 默认的中断处理程序
static void default_intr_soft(void)
{
    set_EOI();
}

static inline void write_io_apic(uint64_t Register, uint64_t Data)
{
    __asm__ __volatile__("mfence;");
    *IoAPIC.RegisterSelect = (uint8_t)Register;
    __asm__ __volatile__("mfence");
    *IoAPIC.Data = (uint32_t)Data;
}

static inline uint64_t read_io_apic(uint64_t Register)
{
    __asm__ __volatile__("mfence");
    *IoAPIC.RegisterSelect = (uint8_t)Register;
    __asm__ __volatile__("mfence");
    return *IoAPIC.Data;
}

uint32_t get_apic_id()
{
    return (LocalAPIC[LocalAPICId] >> 24); /// 这是APIC
}

///// @brief 向其他处理器发送IPI信息
///// @param InterruptVector 中断向量号
//static void broadcast_ipi_no_self(uint8_t InterruptVector)
//{
//    LocalAPIC[ICRbit63to32] = 0;
//    LocalAPIC[ICRbit31to0] = (0x3 << 18) | (0x1 << 14) | InterruptVector;
//}
//

/// @brief 向其他处理器发送IPI INIT信息
static void broadcast_ipi_init(void)
{
    LocalAPIC[ICRbit31to0] = 0xc4500;
}

/// @brief 启动其他处理器
/// @param StartUpPhyAddr 地址，需要是0xmn000的形式，这时0xmn为interruptvector
static void broadcast_ipi_startup(void)
{
    LocalAPIC[ICRbit31to0] = 0xc4600 | (PHYSIC_ADDR_AP_CODE_DATA >> 12);
}

extern uint8_t ap_code_data_start[];
uint32_t ap_startup_lock;
uint32_t ap_startup_count;
uint32_t ap_ready_num;
void init_ap(void){
    memcpy(easy_phy2linear(PHYSIC_ADDR_AP_CODE_DATA),ap_code_data_start,512);
    broadcast_ipi_init();
    for (uint32_t i = 0; i < 100000; i++)
    {
        __asm__ __volatile__("nop");
    }
    ap_startup_lock = 0;
    broadcast_ipi_startup();
    while(cpus->total_num > ap_ready_num + 1) __asm__ __volatile__("pause");
    color_print("[BspCore] All AP start up finished!\n",VIEW_COLOR_BLACK,VIEW_COLOR_WHITE);
}
