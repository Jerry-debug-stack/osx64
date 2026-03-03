global _start
section .text:
_start:
    bits 64
    mov rax,3
    mov rdi,0
    mov rsi,string
    mov rdx,34
    int 0x80
    mov rax,3
    mov rdi,0
    mov rsi,string2
    mov rdx,1
    mov rax,20
    int 0x80
    jmp $
section .data:
    string:
        db "Hello from the first usr process!"
    string2:
        db 10
