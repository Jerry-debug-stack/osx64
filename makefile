# tools are here.
nasm 			= nasm
ld   			= ld
gcc  			= gcc -o2 -o
objcopy			= objcopy

inc				= include
gccBuild 		= -m64 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector -mcmodel=large -mno-red-zone -Wall -Wextra -I$(inc) -c -g

c_srcs			= $(shell find src -name "*.c")

asm_objs		= target/boot.o target/sysapi.o
c_objs			= $(patsubst src/%.c,target/%.o,$(c_srcs))

objs			= $(asm_objs) $(c_objs)
# $(patsubst %.asm,target/%.o,$(asm_srcs))
target_elf		= target/kernel.elf
target			= target/kernel.bin

img				= target/hdd.img

$(target_elf): $(objs)
	$(ld) -m elf_x86_64 -Map target/kernel.map -g -T linker.ld -o $@ $^
$(target): $(target_elf)
	$(objcopy) -O binary -S $^ $@

target/boot.o: src/boot.asm
	$(nasm) -f elf64 -g -I$(inc) -o $@ $^

target/sysapi.o: src/usr/sysapi.asm
	$(nasm) -f elf64 -g -I$(inc) -o $@ $^

target/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(gcc) $@ $(gccBuild) $^

$(img): $(target) 
	bash ./makeiso.sh

all: $(target_elf) $(target) $(img)
clean:
	rm -rf target
	mkdir target
run:$(target_elf) $(target) $(img)
	qemu-system-x86_64 -smp 4 -m 4096 -vga std -device ahci,id=ahci -drive file=$(img),if=none,id=disk0,format=raw -device ide-hd,drive=disk0,bus=ahci.0 \
	-drive file=extended.img,if=none,id=disk1,format=raw -device ide-hd,drive=disk1,bus=ahci.1
test:$(target_elf) $(target) $(img)
	qemu-system-x86_64 -s -S -smp 4 -m 4096 -vga std -device ahci,id=ahci -drive file=$(img),if=none,id=disk0,format=raw -device ide-hd,drive=disk0,bus=ahci.0 \
	-drive file=extended.img,if=none,id=disk1,format=raw -device ide-hd,drive=disk1,bus=ahci.1
.PHONY: run all clean test

