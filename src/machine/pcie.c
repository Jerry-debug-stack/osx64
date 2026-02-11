#include "machine/pcie/pcie.h"
#include "mm/mm.h"
#include "view/view.h"
#include "lib/string.h"
#include <stdint.h>

void *init_ahci_disk(int pcie_addr);
static void pci_match_driver(uint8_t bus, uint8_t dev, uint8_t func, uint8_t class, uint8_t subclass, uint8_t progif);
void init_ahci_mem(void);

static void pci_scan_function(uint8_t bus, uint8_t dev, uint8_t func)
{
    uint32_t addr = MAKE_PCIE_ADDR(bus, dev, func, 0);

    uint32_t vendor = read_pcie(addr);
    if (vendor == 0xffffffff)
        return;
    uint32_t cspr = read_pcie(MAKE_PCIE_ADDR_1(addr, 0, CLASSCODE_SUBCLASS_PROGIF_REVISIONID));
    uint8_t class = cspr >> 24;
    uint8_t subclass = (cspr >> 16) & 0xff;
    uint8_t progif = (cspr >> 8) & 0xff;
    wb_printf("[  PCI  ] %d:%d:%d class %x subclass %x progif %x\n", bus, dev, func, class, subclass, progif);
    pci_match_driver(bus, dev, func, class, subclass, progif);
}

void enumerate_pcie_devices(void)
{
    init_ahci_mem();

    for (uint16_t bus = 0; bus < 256; bus++)
    {
        for (uint8_t dev = 0; dev < 32; dev++)
        {
            uint32_t addr = MAKE_PCIE_ADDR(bus, dev, 0, 0);
            if (read_pcie(addr) == 0xffffffff)
                continue;
            uint32_t header = read_pcie(MAKE_PCIE_ADDR_1(addr, 0, BIST_HEADERTYPE_LATENCYTIMER_CACHELINESIZE));
            uint8_t header_type = (header >> 16) & 0xff;
            pci_scan_function(bus, dev, 0);
            if (header_type & 0x80)
            {
                for (uint8_t func = 1; func < 8; func++)
                    pci_scan_function(bus, dev, func);
            }
        }
    }
}

static void pci_match_driver(uint8_t bus, uint8_t dev, uint8_t func, uint8_t class, uint8_t subclass, uint8_t progif)
{
    switch (class)
    {

    case 0x01: // Mass storage
        if (subclass == 0x06 && progif == 0x01)
            init_ahci_disk(MAKE_PCIE_ADDR(bus, dev, func, 0));
        break;

    case 0x0C: // Serial bus
               // if (subclass == 0x03)
               //{
               //    if (progif == 0x30)
               //        init_xhci(bus, dev, func);
               //    else if (progif == 0x20)
               //        init_ehci(bus, dev, func);
               //    else if (progif == 0x00)
               //        init_uhci(bus, dev, func);
               //}
               // break;

    default:
        break;
    }
}

/// @brief 写入pcie配置空间
/// @param addr pcie地址
/// @param num 数值
void write_pcie(uint32_t addr, uint32_t num)
{
    __asm__ __volatile__("outl %%eax,%%dx;" ::"d"(PCIE_CONFIG_ADDRESS), "a"(PCIE_CONFIG_ENABLE | addr) : "memory");
    __asm__ __volatile__("outl %%eax,%%dx;" ::"d"(PCIE_CONFIG_DATA), "a"(num) : "memory");
}

uint32_t read_pcie(uint32_t addr)
{
    uint32_t ret;
    __asm__ __volatile__("outl %%eax,%%dx;" ::"d"(PCIE_CONFIG_ADDRESS), "a"(PCIE_CONFIG_ENABLE | addr) : "memory");
    __asm__ __volatile__("inl %%dx,%%eax;" : "=a"(ret) : "d"(PCIE_CONFIG_DATA) :);
    return ret;
}
