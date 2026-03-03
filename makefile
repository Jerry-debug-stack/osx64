# tools are here.
nasm 			= nasm
ld   			= ld
gcc  			= gcc -o
objcopy			= objcopy

inc				= include
gccBuild 		= -m64 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector -mcmodel=large -mno-red-zone -Wall -Wextra -I$(inc) -c -g
gccBuild 		+= -mgeneral-regs-only -mno-mmx -mno-sse -mno-sse2 -mno-80387

c_srcs			= $(shell find src -name "*.c")

asm_objs		= target/boot.o
c_objs			= $(patsubst src/%.c,target/%.o,$(c_srcs))

objs			= $(asm_objs) $(c_objs)
# $(patsubst %.asm,target/%.o,$(asm_srcs))
target_elf		= target/kernel.elf

usr_lib			= target/usr/pub/sysapi.o target/usr/pub/user_mem.o target/usr/pub/user_printf.o target/usr/pub/user_string.o

bash_elf		= target/bash.elf
bash_elf_req 	= $(usr_lib) target/usr/bash/bash.o

target			= target/kernel.bin

img				= target/hdd.img

$(target_elf): $(objs)
	$(ld) -m elf_x86_64 -Map target/kernel.map -g -T linker.ld -o $@ $^
$(target): $(target_elf)
	$(objcopy) -O binary -S $^ $@

$(bash_elf): $(bash_elf_req)
	$(ld) -m elf_x86_64 --entry=bash_main -o $@ $^

target/boot.o: src/boot.asm
	$(nasm) -f elf64 -g -I$(inc) -o $@ $^

target/usr/pub/sysapi.o: usr/pub/sysapi.asm
	mkdir -p target/usr/pub
	$(nasm) -f elf64 -g -o $@ $^

target/usr/pub/user_mem.o: usr/pub/user_mem.c
	mkdir -p target/usr/pub
	$(gcc) $@ $(gccBuild) $^

target/usr/pub/user_printf.o: usr/pub/user_printf.c
	mkdir -p target/usr/pub
	$(gcc) $@ $(gccBuild) $^

target/usr/pub/user_string.o: usr/pub/user_string.c
	mkdir -p target/usr/pub
	$(gcc) $@ $(gccBuild) $^

target/usr/bash/bash.o: usr/bash/bash.c
	mkdir -p target/usr/bash
	$(gcc) $@ $(gccBuild) $^

target/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(gcc) $@ $(gccBuild) $^

$(img): $(target) 
	bash ./makeiso.sh

all: $(target_elf) $(bash_elf) $(target) $(img)
	sudo mount /dev/loop1p1 /mnt/osdev
	sudo cp $(bash_elf) /mnt/osdev/bash.elf
	sudo umount /mnt/osdev
clean:
	rm -rf target
	mkdir target
run:$(target_elf) $(bash.elf) $(target) $(img)
	qemu-system-x86_64 -smp 4 -m 4096 -vga std -device ahci,id=ahci -drive file=$(img),if=none,id=disk0,format=raw -device ide-hd,drive=disk0,bus=ahci.0 \
	-drive file=extended.img,if=none,id=disk1,format=raw -device ide-hd,drive=disk1,bus=ahci.1
test:$(target_elf) $(bash.elf) $(target) $(img)
	qemu-system-x86_64 -s -S -smp 4 -m 4096 -vga std -device ahci,id=ahci -drive file=$(img),if=none,id=disk0,format=raw -device ide-hd,drive=disk0,bus=ahci.0 \
	-drive file=extended.img,if=none,id=disk1,format=raw -device ide-hd,drive=disk1,bus=ahci.1
.PHONY: run all clean test

