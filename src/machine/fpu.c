#include <stdint.h>
#include "lib/io.h"
#include "view/view.h"
#include "const.h"
#include "protect.h"

/* CPUID 叶 1 返回的 EDX 位 */
#define CPUID_FPU   (1 << 0)   /* x87 FPU on chip */
#define CPUID_MMX   (1 << 23)  /* MMX technology */
#define CPUID_FXSR  (1 << 24)  /* FXSAVE/FXRSTOR */
#define CPUID_SSE   (1 << 25)  /* SSE extensions */
#define CPUID_SSE2  (1 << 26)  /* SSE2 extensions */
#define CPUID_SSE3  (1 << 0)   /* SSE3 (in ECX) */

/* CPUID 叶 1 返回的 ECX 位 */
#define CPUID_SSE3   (1 << 0)  /* SSE3 */
#define CPUID_SSSE3  (1 << 9)  /* SSSE3 */
#define CPUID_SSE41  (1 << 19) /* SSE4.1 */
#define CPUID_SSE42  (1 << 20) /* SSE4.2 */
#define CPUID_XSAVE  (1 << 26) /* XSAVE */
#define CPUID_AVX    (1 << 28) /* AVX */

/* ========== FPU/SSE 初始化 ========== */
void init_fpu_sse(void) {
    uint64_t eax, ebx, ecx, edx;
    uint64_t cr0, cr4;

    /* 1. 检测 CPU 特性 */
    cpuid(1, &eax, &ebx, &ecx, &edx);

    if (!(edx & CPUID_FPU)) {
        wb_printf("ERROR: x87 FPU not supported.\n");
        halt();
    }
    if (!(edx & CPUID_MMX)) {
        wb_printf("ERROR: MMX not supported.\n");
        halt();
    }
    if (!(edx & CPUID_FXSR)) {
        wb_printf("ERROR: FXSAVE/FXRSTOR not supported.\n");
        halt();
    }
    if (!(edx & CPUID_SSE)) {
        wb_printf("ERROR: SSE not supported.\n");
        halt();
    }

    /* 2. 配置控制寄存器 */
    cr0 = read_cr0();
    cr0 &= ~(CR0_EM);           /* 清除 EM 位：禁用仿真，使用硬件 FPU */
    cr0 |=  (CR0_MP | CR0_TS);  /* 设置 MP 和 TS 位：启用 WAIT 监控，并立即开启懒加载 */
    write_cr0(cr0);

    cr4 = read_cr4();
    cr4 |= (CR4_OSFXSR | CR4_OSXMMEXCPT);  /* 告知 CPU 操作系统支持 FXSAVE 和 SSE 异常 */
    write_cr4(cr4);

    /* 注意：不执行 fninit，也不设置 MXCSR。这些将在 #NM 处理程序中按需完成。 */
    wb_printf("[FPU/SSE] control registers configured, lazy FPU enabled (CR0.TS=1).\n");
}
