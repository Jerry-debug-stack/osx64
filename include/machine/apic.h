#ifndef OS_APIC_H
#define OS_APIC_H

#include <stdint.h>

#define PHYSIC_ADDR_LOCAL_APIC 0xFEE00000
#define PHYSIC_ADDR_IO_APIC 0xFEC00000

#define LocalAPICId 8
#define LocalAPICVersion 12
#define TPR 32
#define APR 36
#define PPR 40
#define EOI 44
#define RRD 48
#define LDR 52
#define DFR 56
#define SVR 60
#define ISRbit31to0 64
#define ISRbit63to32 68
#define ISRbit95to64 72
#define ISRbit127to96 76
#define ISRbit159to128 80
#define ISRbit191to160 84
#define ISRbit223to192 88
#define ISRbit255to224 92
#define TMRbit31to0 96
#define TMRbit63to32 100
#define TMRbit95to64 104
#define TMRbit127to96 108
#define TMRbit159to128 112
#define TMRbit191to160 116
#define TMRbit223to192 120
#define TMRbit255to224 124
#define IRRbit31to0 128
#define IRRbit63to32 132
#define IRRbit95to64 136
#define IRRbit127to96 140
#define IRRbit159to128 144
#define IRRbit191to160 148
#define IRRbit223to192 152
#define IRRbit255to224 156
#define ESR 160
#define LVTCMCI 188
#define ICRbit31to0 192
#define ICRbit63to32 196
#define LVTTimer 200
#define LVTTemperature 204
#define LVTPerformance 208
#define LVTLINT0 212
#define LVTLINT1 216
#define LVTFault 220
#define InitialCounter 224
#define NowCounter 228
#define FrequencyDivision 248

/* Local APIC Version */
/// 31-25   |24                     |23-16    |15-8    |7-0
/// Reserved|CanProhibitBroadcastEOI|LVTNumber|Reserved|VersionId

/* IA32 APIC Base */
/// 63-12                    |11        |10          |9   |8    |7-0
/// PysicAddrForAPICRegistors|EnableAPIC|Enablex2APIC|RSVD|IsBSP|RSVD
#define IA32_APIC_BASE 0x1b
#define IA32_APIC_BASE_ENABLE_xAPIC ((uint32_t)1 << 11)
#define IA32_APIC_BASE_ENABLE_x2APIC ((uint32_t)3 << 10)
/* Local APIC Id */
/// for xAPIC Id is from bit 24 to bit 31
/// for x2APIC Id is all 32 bits

/* LVT Table */
/// Now R represents Reserved
#define LVT_MASKED ((uint32_t)1 << 16)
/* LVT Timer */
/// 31-19|18-17     |16  |15-13|12             |11-8|7-0
/// R    |Timer Mode|Mask|R    |Dilivery Status|R   |Interrupt Vector
#define TIMER_MODE_ONESHOT 0
#define TIMER_MODE_PERIODIC ((uint32_t)1 << 17)
#define TIMER_TSC_DEADLINE ((uint32_t)2 << 17)
/* LVT CMCI */
/// 31-17|16  |15-13|12             |11|10-8         |7-0
/// R    |Mask|R    |Dilivery Status|R |Dilivery Mode|Interrupt Vector
#define DILIVERY_MODE_FIXED 0
#define DILIVERY_MODE_SMI ((uint32_t)2 << 8)
#define DILIVERY_MODE_NMI ((uint32_t)4 << 8)
#define DILIVERY_MODE_EXINT ((uint32_t)7 << 8)
#define DILIVERY_MODE_INT ((uint32_t)5 << 8)

/// LVT表项应设置为边缘触发
/* LVT LINT0 */
/// 31-17|16  |15          |14       |13                          |12             |11|10-8         |7-0
/// R    |Mask|Trigger Mode|RemoteIRR|Interrupt Input Pin Polarity|Dilivery Status|R |Dilivery Mode|Interrupt Vector

#define TRIGGER_MODE_EDGE 0
#define TRIGGER_MODE_LEVEL ((uint32_t)1 << 15)
#define INTERRUPT_INPUT_PIN_POSITIVE ((uint32_t)1 << 13)

/* LVT LINT1 */
/// 31-17|16  |15          |14       |13                          |12             |11|10-8         |7-0
/// R    |Mask|Trigger Mode|RemoteIRR|Interrupt Input Pin Polarity|Dilivery Status|R |Dilivery Mode|Interrupt Vector

/* LVT FAULT */
/// 31-17|16  |15-13|12             |11-8|7-0
/// R    |Mask|R    |Dilivery Status|R   |Interrupt Vector

/* LVT PERFORMANCE COUNTER */
/// 31-17|16  |15-13|12             |11|10-8         |7-0
/// R    |Mask|R    |Dilivery Status|R |Dilivery Mode|Interrupt Vector

/* LVT TEMPERATURE */
/// 31-17|16  |15-13|12             |11|10-8         |7-0
/// R    |Mask|R    |Dilivery Status|R |Dilivery Mode|Interrupt Vector

/* ESR */
/// 31-8|7                |6                            |5                        |4      |3                  |2                |1                   |0
/// R   |Bad Rigister Addr|Bad Interrupt Vector Received|Bad Interrupt Vector Sent|Bad IPI|Faults in Receiving|Faults in Sending|Sum Error(Receiving)|Sum Error(Sending)

/* TPR */
/// 31-8|7-4                |3-0
/// R   |Task Priority Level|Task Priority Child Level

/* PPR */
/// 31-8|7-4                |3-0
/// R   |Task Priority Level|Task Priority Child Level

/* CR8 */
/// 63-4|3-0
/// R   |Task Priority Level
/// CR8[3:0] == TPR[7:4]

/* IRR */
/// 255-16|15-0
/// Used  |R

/* ISR */
/// 255-16|15-0
/// Used  |R

/* TMR */
/// 255-16|15-0
/// Used  |R

/* EOI */

/* SVR */
/// 31-13|12                  |11-10|9                |8         |7-0
/// R    |ProhibitBroadcastEOI|R    |焦点处理器检测标志位|EnableAPIC|Pretend Interrupt Vector
#define SVR_PROHIBIT_BTOADCAST_EOI ((uint32_t)1 << 12)
#define SVR_ENABLE_APIC ((uint32_t)1 << 8)

/// For io apic
#define IO_REGISTER_SELECT 0xfec00000
#define IO_WINDOW 0xfec00010
#define IO_EOI_REGISTER 0xfec00040

///|31-28|27-24|23-0|
///|R    |Id   |R   |
#define IO_APIC_ID 0x0
///|31-24|23-16     |15-8|7-0
///|R    |RTE Number|R   |IO APIC Version
#define IO_APIC_VERSION 0x1
///|63-56            |55-17|16  |15          |14 |13              |12            |11         |10-8        |7-0
///|Destination Field|R    |Mask|Trigger Mode|IRR|Trigger Polarity|Diliver Status|Target Mode|Deliver Mode|Interrupt Vector
#define IO_APIC_REDIRECTION_TABLE_LOW(num) (0x10 + (num << 1))
#define IO_APIC_REDIRECTION_TABLE_HIGH(num) (0x11 + (num << 1))
#define DESTINATION_PHYSUC 0
#define DESTINATION_LOGICAL ((uint32_t)1 << 11)
#define IO_APIC_REDIRECTION_MASKED ((uint32_t)1 << 16)

#define INTERRUPT_VECTOR_8259A_MASTER 0x20
#define INTERRUPT_VECTOR_KEYBOARD 0x21
#define INTERRUPT_VECTOR_TIMER 0x22
#define INTERRUPT_VECTOR_SERIAL_2_4 0x23
#define INTERRUPT_VECTOR_SERIAL_1_3 0x24
#define INTERRUPT_VECTOR_PARALLEL_2 0x25
#define INTERRUPT_VECTOR_CDROM 0x26
#define INTERRUPT_VECTOR_PARALLEL_1 0x27
#define INTERRUPT_VECTOR_RTC_HPET_1 0x28
#define INTERRUPT_VECTOR_9 0x29
#define INTERRUPT_VECTOR_10 0x2a
#define INTERRUPT_VECTOR_HPET_2 0x2b
#define INTERRUPT_VECTOR_HPET_3_MOUSE_PS2 0x2c
#define INTERRUPT_VECTOR_FERR_DMA 0x2d
#define INTERRUPT_VECTOR_MASTER_SATA 0x2e
#define INTERRUPT_VECTOR_SLAVE_SATA 0x2f
#define INTERRUPT_VECTOR_PIRQA 0x30
#define INTERRUPT_VECTOR_PIRQB 0x31
#define INTERRUPT_VECTOR_PIRQC 0x32
#define INTERRUPT_VECTOR_PIRQD 0x33
#define INTERRUPT_VECTOR_PIRQE 0x34
#define INTERRUPT_VECTOR_PIRQF 0x35
#define INTERRUPT_VECTOR_PIRQG 0x36
#define INTERRUPT_VECTOR_PIRQH 0x37

typedef struct IoAPIC {
    uint8_t* RegisterSelect;
    uint32_t* Data;
    uint32_t* IoEOI;
    uint64_t RTENumber;
    uint64_t BspApicId;
} IO_APIC;

#endif
