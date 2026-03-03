global get_ticks
global open
global read
global write
global close
global lseek
global mkdir
global rmdir
global unlink
global chdir
global ftruncate
global truncate
global rename
global dup
global dup2
global getcwd
global mount
global umount
global reload_partition
global getdent
global exit
global yield
global waitpid
global sync

section .text
    bits 64
    get_ticks:
        mov rax,0
        int 0x80
        ret

    open:
        mov rax,1
        int 0x80
        ret

    read:
        mov rax,2
        int 0x80
        ret

    write:
        mov rax,3
        int 0x80
        ret

    close:
        mov rax,4
        int 0x80
        ret

    lseek:
        mov rax,5
        int 0x80
        ret

    mkdir:
        mov rax,6
        int 0x80
        ret

    rmdir:
        mov rax,7
        int 0x80
        ret

    unlink:
        mov rax,8
        int 0x80
        ret

    chdir:
        mov rax,9
        int 0x80
        ret

    ftruncate:
        mov rax,10
        int 0x80
        ret

    truncate:
        mov rax,11
        int 0x80
        ret

    rename:
        mov rax,12
        int 0x80
        ret

    dup:
        mov rax,13
        int 0x80
        ret

    dup2:
        mov rax,14
        int 0x80
        ret

    getcwd:
        mov rax,15
        int 0x80
        ret

    mount:
        mov rax,16
        int 0x80
        ret

    umount:
        mov rax,17
        int 0x80
        ret

    reload_partition:
        mov rax,18
        int 0x80
        ret

    getdent:
        mov rax,19
        int 0x80
        ret

    exit:
        mov rax,20
        int 0x80
        ret
    
    yield:
        mov rax,21
        int 0x80
        ret
    
    waitpid:
        mov rax,22
        int 0x80
        ret

    sync:
        mov rax,23
        int 0x80
        ret
