ENTRY(_start)

VIRTUAL_BASE        = 0xffff800000110000;
VIRTUAL_ADDR_0      = 0xffff800000000000;

SECTIONS
{
    . = 0x100000;
    .multiboot : {
        target/boot.o(.multiboot)
    }
    .enter : {
        target/boot.o(.text32)
        target/boot.o(.text64low)
    }
    .bss : ALIGN(4K){
        target/boot.o(.bss)
    }
    .data : ALIGN(4K){
        target/boot.o(.data)
    }
    . = VIRTUAL_BASE;
    .bit64enter : AT(ADDR(.bit64enter) - VIRTUAL_ADDR_0){
        target/boot.o(.text64high)
        target/boot.o(.data64)
    }
    .kernel : AT(ADDR(.kernel) - VIRTUAL_ADDR_0) {
        *(.text)
    }
    . = ALIGN(4K);
    .bss_k :  AT(ADDR(.bss_k) - VIRTUAL_ADDR_0){
        target/boot.o(.kernel.bss)
        *(.bss)
    }
    . = ALIGN(4K);
    .rodata_k : AT(ADDR(.rodata_k) - VIRTUAL_ADDR_0) {
        *(.rodata)
        *(.rodata.*)
        *(.eh_frame)      /* 将.eh_frame放在rodata_k中 */
    }
    . = ALIGN(1M);
    .mrt (NOLOAD) : {
        *(.mrt) /* mrt表 */
    }

    .debug_info 0 : { *(.debug_info) }
    .debug_abbrev 0 : { *(.debug_abbrev) }
    .debug_line 0 : { *(.debug_line) }
    .debug_frame 0 : { *(.debug_frame) }
    .debug_str 0 : { *(.debug_str) }
    .debug_loc 0 : { *(.debug_loc) }
}
