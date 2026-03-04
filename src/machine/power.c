#include "const.h"
#include "lib/io.h"

#define REBOOT_CMD_POWER_OFF 0
#define REBOOT_CMD_RESTART 1

int sys_sync(void);

_Noreturn void acpi_shutdown()
{
    asm volatile("cli");
    
    io_outword(0xb004, 0x2000);
    io_outword(0x604, 0x2000);
    io_outword(0x4004, 0x3400);
    while (1)
        asm volatile("hlt");
}

_Noreturn void system_reboot()
{
    asm volatile("cli");
    char status;
    do {
        status = io_inbyte(0x64);
        if (!(status & 0x02))
            break;
    } while (1);
    io_outbyte(0x64, 0xFE);
    while (1)
        asm volatile("hlt");
}

int sys_reboot(int cmd)
{
    if (cmd == REBOOT_CMD_POWER_OFF){
        sys_sync();
        acpi_shutdown();
    }
    else if (cmd == REBOOT_CMD_RESTART){
        sys_sync();
        system_reboot();
    }
    return -1;
}
