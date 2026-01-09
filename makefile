# tools are here.
nasm 			= nasm
ld   			= ld
gcc  			= gcc -o2 -o
objcopy			= objcopy

grub_dir		= target/iso/boot/grub
inc				= include
gccBuild 		= -m64 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector -mcmodel=large -mno-red-zone -Wall -Wextra -I$(inc) -c -g

c_srcs			= $(shell find src -name "*.c")

asm_objs		= target/boot.o
c_objs			= $(patsubst src/%.c,target/%.o,$(c_srcs))

objs			= $(asm_objs) $(c_objs)
# $(patsubst %.asm,target/%.o,$(asm_srcs))
target_elf		= target/kernel.elf
target			= target/kernel.bin

iso				= target/kernel.iso

$(target_elf): $(objs)
	$(ld) -m elf_x86_64 -Map target/kernel.map -g -T linker.ld -o $@ $^
$(target): $(target_elf)
	$(objcopy) -O binary -S $^ $@

target/boot.o: src/boot.asm
	$(nasm) -f elf64 -g -I$(inc) -o $@ $^

target/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(gcc) $@ $(gccBuild) $^

$(iso): $(target) 
	mkdir -p $(grub_dir)
	cp $(target_elf) target/iso/boot/
	cp grub.cfg $(grub_dir)/grub.cfg
	grub-mkrescue -o $(iso) target/iso

all: $(target_elf) $(target) $(iso)
clean:
	rm -rf target
	mkdir target
run:$(target_elf) $(target) $(iso)
	qemu-system-x86_64 -cdrom $(iso) -smp 4 -m 4096 -vga std
test:$(target_elf) $(target) $(iso)
	qemu-system-x86_64 -cdrom $(iso) -s -S -smp 4 -m 4096 -vga std

.PHONY: run all clean test

