#include <stdint.h>
#include "view/view.h"
#include "const.h"

// --- HPET 寄存器偏移 (基于你的HPET基地址) ---
#define HPET_GEN_CAP           0x000   // 通用能力寄存器
#define HPET_GEN_CONF          0x010   // 通用配置寄存器
#define HPET_INT_COND          0x020   // 中断状态寄存器
#define HPET_MAIN_COUNTER      0x0F0   // 主计数器
#define TIMER0_CONF_CAP        0x100   // 定时器0的配置/能力
#define TIMER0_COMPARATOR      0x108   // 定时器0的比较器
#define TIMER0_FSB_ADDR         0x110   // Timer 0 FSB中断地址 (如果支持MSI)

// --- HPET 常用位定义 ---
// 通用能力寄存器 (GEN_CAP)
#define HPET_CAP_LEG_RT         (1ULL << 1)  // Legacy Replacement 路由能力
#define HPET_CAP_COUNT_SIZE     (1ULL << 13) // 计数器大小 (1=64位, 0=32位)
#define HPET_CAP_NUM_TIM_SHIFT  8            // 定时器数量移位

// 通用配置寄存器 (GEN_CONF)
#define HPET_CONF_ENABLE        (1ULL << 0)  // 主计数器使能
#define HPET_CONF_LEG_RT        (1ULL << 1)  // Legacy Replacement 路由使能

// 定时器配置寄存器 (TIMERn_CONF)
#define HPET_TN_INT_ENABLE      (1ULL << 2)  // 中断使能
#define HPET_TN_PERIODIC        (1ULL << 3)  // 周期性定时器
#define HPET_TN_PER_INT_CAP     (1ULL << 4)  // 支持周期中断
#define HPET_TN_SIZE_CAP        (1ULL << 5)  // 支持32位/64位
#define HPET_TN_SET_ACCUM       (1ULL << 6)  // 设置累计 (周期模式)
#define HPET_TN_32BIT           (1ULL << 8)  // 32位模式
#define HPET_TN_INT_ROUTE_SHIFT 9            // 中断路由移位
#define HPET_TN_INT_ROUTE_MASK  0x3E00        // 中断路由掩码
#define HPET_TN_FSB_ENABLE      (1ULL << 15) // FSB中断使能
#define HPET_TN_FSB_INT_DEL     (1ULL << 16) // FSB中断延迟
#define HPET_TN_TYPE_LEVEL      (1ULL << 1)  // 电平触发 (0=边沿)

uint64_t hpet_base = 0;
uint64_t hpet_frequency_hz;

// --- HPET 读写辅助函数 (假设内存映射) ---
static inline uint64_t hpet_read(int reg) {
    return *(volatile uint64_t*)(hpet_base + reg);
}

static inline void hpet_write(int reg, uint64_t val) {
    *(volatile uint64_t*)(hpet_base + reg) = val;
}

// --- HPET 初始化函数 ---
int init_hpet_timer(void) {
    uint64_t cap;
    int num_timers;

    // 1. 读取HPET能力，确认硬件存在且支持周期性中断
    cap = hpet_read(HPET_GEN_CAP);
    num_timers = (cap >> HPET_CAP_NUM_TIM_SHIFT) & 0x1F;
    
    // 至少需要一个定时器，且定时器0应支持周期模式
    if (num_timers < 1) {
        // 降级到8254或其他处理
        return -1;
    }

    // 2. 禁用HPET主计数器（如果正在运行）
    hpet_write(HPET_GEN_CONF, 0);
    
    // 3. 配置定时器0为周期性中断
    uint64_t timer_conf = hpet_read(TIMER0_CONF_CAP);
    
    // 确保定时器0支持周期模式
    if (!(timer_conf & HPET_TN_PER_INT_CAP)) {
        return -1;
    }
    
    // 构建定时器配置值
    timer_conf = 0;
    timer_conf |= HPET_TN_INT_ENABLE;      // 中断使能
    timer_conf |= HPET_TN_PERIODIC;        // 周期性
    timer_conf |= HPET_TN_SET_ACCUM;       // 设置累计（减少首次延迟）
    
    timer_conf |= (2 << HPET_TN_INT_ROUTE_SHIFT); // 路由到IRQ2
    
    // 可选：设置为电平触发（根据你的IOAPIC配置）
    // timer_conf |= HPET_TN_TYPE_LEVEL;
    
    hpet_write(TIMER0_CONF_CAP, timer_conf);
    
    cap = hpet_read(HPET_GEN_CAP);  // 再次读取能力寄存器（或使用之前读过的值）
    uint32_t period_fs = cap >> 32;          // 周期值，单位飞秒（10^-15 秒）
    if (period_fs == 0) {
        wb_printf("[HPET] Warning: Invalid period in capability register, using default 14.31818 MHz\n");
        period_fs = 69841279; // 对应 14.31818 MHz 的周期飞秒值：10^15 / 14318180 ≈ 69841
    }
    
    // 计算 HPET 计数器频率（Hz）
    hpet_frequency_hz = 1000000000000000ULL / period_fs;  // 10^15 / period_fs
    
    // 计算所需的计数器周期数（计数器增量）
    // 使用四舍五入： (hpet_frequency_hz + TARGET_FREQ_HZ/2) / TARGET_FREQ_HZ
    uint64_t period_ticks = (hpet_frequency_hz + CLOCK_FREQ / 2) / CLOCK_FREQ;
    
    // 可选：打印调试信息
    wb_printf("[ HPET  ] Frequency: %lu Hz, period_ticks for %lu Hz: %lu\n",hpet_frequency_hz, CLOCK_FREQ, period_ticks);
    
    // 设置定时器0的比较器值和周期值（替换原有的 RELOAD_TICKS_100HZ）
    hpet_write(TIMER0_COMPARATOR, period_ticks);
    
    // 6. 启用HPET主计数器
    uint64_t main_conf = HPET_CONF_ENABLE;

    hpet_write(HPET_GEN_CONF, main_conf);
    return 0;
}

void hpet_udelay(uint64_t us)
{
    uint64_t start = hpet_read(HPET_MAIN_COUNTER);
    uint64_t delta = (us * hpet_frequency_hz) / 1000000ULL;

    while ((hpet_read(HPET_MAIN_COUNTER) - start) < delta)
        __asm__ __volatile__("pause");
}
