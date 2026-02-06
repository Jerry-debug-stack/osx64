global get_ticks

section .text
    bits 64
    get_ticks:
        mov rax,0
        int 0x80
        ret

    exit:
        mov rax,1
        int 0x80
        ret
    
    sleep:
        mov rax,2
        int 0x80
        ret

    $wait:
        mov rax,3
        int 0x80
        ret
