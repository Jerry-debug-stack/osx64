ENTRY(_start)

VIRTUAL_BASE        = 0xffff800000110000;
VIRTUAL_ADDR_0      = 0xffff800000000000;
AP_ADDR             = 0x10000;

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
    .ap_code : {
        ap_code_data_start = .;
        target/boot.o(.apboot16)
    }
    .apdata : ALIGN(32){
        target/boot.o(.apdata)
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
    . = ALIGN(4K);
    .data_k : AT(ADDR(.data_k) - VIRTUAL_ADDR_0) {
        *(.data)
        *(.data.rel)
        *(.data.rel.local) /* 添加数据重定位本地段 */
        *(.data.rel.*)     /* 添加其他数据重定位段（可选） */
    }
    .got : AT(ADDR(.got) - VIRTUAL_ADDR_0){ 
        *(.got.plt) 
        *(.got) 
    }
    .rela.dyn : AT(ADDR(.rela.dyn) - VIRTUAL_ADDR_0)
    {
      *(.rela.init)
      *(.rela.text .rela.text.*)
      *(.rela.fini)
      *(.rela.rodata .rela.rodata.*)
      *(.rela.data .rela.data.*)
      *(.rela.tdata .rela.tdata.*)
      *(.rela.tbss .rela.tbss.*)
      *(.rela.ctors)
      *(.rela.dtors)
      *(.rela.got)
      *(.rela.bss .rela.bss.*)
    }
    . = ALIGN(1M);
    .mrt (NOLOAD) : {
        _mrt_start = .;
        *(.mrt) /* mrt表 */
    }

    .debug_info 0 : { *(.debug_info) }
    .debug_abbrev 0 : { *(.debug_abbrev) }
    .debug_line 0 : { *(.debug_line) }
    .debug_frame 0 : { *(.debug_frame) }
    .debug_str 0 : { *(.debug_str) }
    .debug_loc 0 : { *(.debug_loc) }
}
