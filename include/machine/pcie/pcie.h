#ifndef OS_PCIE_H
#define OS_PCIE_H

#include <stdint.h>

#define PCIE_CONFIG_ADDRESS 0xcf8
#define PCIE_CONFIG_DATA 0xcfc

#define PCIE_CONFIG_ENABLE 1 << 31
#define PCIE_CONFIG_BUS_NUM_SHIFT 16
#define PCIE_CONFIG_DEVICE_NUM_SHIFT 11
#define PCIE_CONFIG_FUCTION_NUM_SHIFT 8

void write_pcie(uint32_t addr, uint32_t num);
uint32_t read_pcie(uint32_t addr);

/// PCI_HEADER
///  @brief |31-26 Device ID|15-0 Vender ID|
#define DEVICE_VENDER 0
/// @brief |31-26 Status|15-0 Command|
#define STATUS_COMMAND 0x4
/// @brief |31-24 Class Code|23-16 Sub Class|15-8 Prog IF|7-0 Revision ID|
#define CLASSCODE_SUBCLASS_PROGIF_REVISIONID 0x8
/// @brief |31-24 BIST|23-16 Header Type|15-8 Latency Timer|7-0 Cache Line Size
#define BIST_HEADERTYPE_LATENCYTIMER_CACHELINESIZE 0xc

/// Command Register
///|15-11|10               |9                |8   |7|6                    |5                |4                          |3             |2         |1           |0
///|R    |Interrupt Disable|Fast Back to Back|SERR|R|Parity Error Response|VGA Plaette Snoop|Memory Write And Invalidate|Special Cycles|Bus Master|Memory Space|IO Space

/// Status Register
///|15                   |14                   |13                   |12                   |11                   |10-9         |8                       |7                        |6|5             |4                |3               |2-0
///|Detected Parity Error|Signaled System Error|Received Master Abort|Received Target Abort|Signaled Target Abort|DEVSEL Timing|Master Data Parity Error|Fast Back-to-Back Capable|R|66 MHz Capable|Capabilities List|Interrupt Status|R

#define MAKE_PCIE_ADDR(bus, device, function, addr) ((bus << PCIE_CONFIG_BUS_NUM_SHIFT) | (device << PCIE_CONFIG_DEVICE_NUM_SHIFT) | (function << PCIE_CONFIG_FUCTION_NUM_SHIFT) | addr)
#define MAKE_PCIE_ADDR_1(bus_device, function, addr) (bus_device | (function << PCIE_CONFIG_FUCTION_NUM_SHIFT) | addr)
#define MAKE_PCIE_ADDR_2(bus_device_function, addr) (bus_device_function | addr)

#endif
