#include "machine/pcie/pcie.h"

/// @brief 写入pcie配置空间
/// @param addr pcie地址
/// @param num 数值
void write_pcie(uint32_t addr , uint32_t num)
{
    __asm__ __volatile__("outl %%eax,%%dx;" ::"d"(PCIE_CONFIG_ADDRESS), "a"(PCIE_CONFIG_ENABLE | addr):"memory");
    __asm__ __volatile__("outl %%eax,%%dx;" ::"d"(PCIE_CONFIG_DATA), "a"(num):"memory");
}

uint32_t read_pcie(uint32_t addr)
{
    uint32_t ret;
    __asm__ __volatile__("outl %%eax,%%dx;" ::"d"(PCIE_CONFIG_ADDRESS), "a"(PCIE_CONFIG_ENABLE | addr):"memory");
    __asm__ __volatile__("inl %%dx,%%eax;" : "=a"(ret) : "d"(PCIE_CONFIG_DATA) :);
    return ret;
}
