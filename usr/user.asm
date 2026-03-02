global _start
section .text:
_start:
    bits 64
    mov rax,3
    mov rdi,0
    mov rsi,string
    mov rdx,34
    int 0x80
    jmp $
section .data:
    string:
        dd "Hello from the first usr process!!"
