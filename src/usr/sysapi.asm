global get_ticks

section .text
    bits 64
    get_ticks:
        mov rax,0
        int 0x80
        ret
