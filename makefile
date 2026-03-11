# tools are here.
nasm 			= nasm
ld   			= ld
gcc  			= gcc -O0 -o
objcopy			= objcopy

kinc			= include
uinc			= usr/include

k_cflags 		= -m64 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector -mcmodel=large -mno-red-zone -Wall -Wextra -I$(kinc) -c -g \
	-mgeneral-regs-only -mno-mmx -mno-sse -mno-sse2  -mno-avx -mno-80387
u_cflags		= -m64 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector -mcmodel=large -Wall -Wextra -mno-avx -mmmx -msse -mstackrealign -I$(uinc) -c -g

# 自动收集用户公共库源文件
PUB_C_SRCS		= $(wildcard usr/pub/*.c)
PUB_ASM_SRCS	= $(wildcard usr/pub/*.asm)
PUB_OBJS		= $(patsubst usr/pub/%.c, target/usr/pub/%.o, $(PUB_C_SRCS)) \
				  $(patsubst usr/pub/%.asm, target/usr/pub/%.o, $(PUB_ASM_SRCS))

# 自动收集用户程序目录（排除 pub）
EXCLUDE_SUBDIRS = usr/pub/ usr/include/
USER_SUBDIRS    = $(filter-out $(EXCLUDE_SUBDIRS), $(wildcard usr/*/))
USER_PROGS		= $(notdir $(patsubst %/, %, $(USER_SUBDIRS)))
USER_ELFS		= $(addprefix target/, $(USER_PROGS))

# 内核源文件
c_srcs			= $(shell find src -name "*.c")
asm_objs		= target/boot.o
c_objs			= $(patsubst src/%.c, target/%.o, $(c_srcs))
objs			= $(asm_objs) $(c_objs)
target_elf		= target/kernel.elf
target			= target/kernel.bin
img				= target/hdd.img

# 内核编译规则
$(target_elf): $(objs)
	$(ld) -m elf_x86_64 -Map target/kernel.map -g -T linker.ld -o $@ $^

$(target): $(target_elf)
	$(objcopy) -O binary -S $^ $@

target/boot.o: src/boot.asm
	$(nasm) -f elf64 -g -I$(kinc) -o $@ $^

# 通用编译规则：处理 usr/ 下所有 .c 和 .asm 文件（包括 pub 和各个程序目录）
target/usr/%.o: usr/%.c
	@mkdir -p $(dir $@)
	$(gcc) $@ $(u_cflags) $^

target/usr/%.o: usr/%.asm
	@mkdir -p $(dir $@)
	$(nasm) -f elf64 -g -o $@ $^

# 内核 C 文件编译规则
target/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(gcc) $@ $(k_cflags) $^

# 为每个用户程序生成链接规则
define PROGRAM_template
$(1)_SRCS_C   = $$(wildcard usr/$(1)/*.c)
$(1)_SRCS_ASM = $$(wildcard usr/$(1)/*.asm)
$(1)_OBJS     = $$(patsubst usr/$(1)/%.c, target/usr/$(1)/%.o, $$($(1)_SRCS_C)) \
                $$(patsubst usr/$(1)/%.asm, target/usr/$(1)/%.o, $$($(1)_SRCS_ASM))
target/$(1): $$($(1)_OBJS) $(PUB_OBJS)
	@mkdir -p $$(dir $$@)
	$$(ld) -m elf_x86_64 --entry=main -o $$@ $$^
endef

$(foreach prog, $(USER_PROGS), $(eval $(call PROGRAM_template,$(prog))))

# 镜像生成（假设 makeiso.sh 已处理）
$(img): $(target)
	bash ./makeiso.sh

# 最终目标：编译内核和所有用户程序，并复制到挂载点
all: $(target_elf) $(USER_ELFS) $(target) $(img)
	sudo mount /dev/loop1p1 /mnt/osdev
	sudo mkdir -p /mnt/osdev/os
	for elf in $(USER_ELFS); do sudo cp $$elf /mnt/osdev/os/; done
	sudo umount /mnt/osdev

clean:
	rm -rf target
	mkdir target

run:
	make clean
	make all
	qemu-system-x86_64 -smp 4 -m 4096 -vga std -device ahci,id=ahci -drive file=$(img),if=none,id=disk0,format=raw -device ide-hd,drive=disk0,bus=ahci.0 \
	-drive file=extended.img,if=none,id=disk1,format=raw -device ide-hd,drive=disk1,bus=ahci.1 \
	-device usb-ehci,id=ehci -drive if=none,id=myusb,file=usb_extended.img,format=raw -device usb-storage,drive=myusb,bus=ehci.0
#	-device piix3-usb-uhci,id=usb-uhci -drive if=none,id=myusb,file=usb_extended.img,format=raw -device usb-storage,drive=myusb,bus=usb-uhci.0

rerun:
	qemu-system-x86_64 -smp 4 -m 4096 -vga std -device ahci,id=ahci -drive file=$(img),if=none,id=disk0,format=raw -device ide-hd,drive=disk0,bus=ahci.0 \
	-drive file=extended.img,if=none,id=disk1,format=raw -device ide-hd,drive=disk1,bus=ahci.1 \
	-device usb-ehci,id=ehci -drive if=none,id=myusb,file=usb_extended.img,format=raw -device usb-storage,drive=myusb,bus=ehci.0
#	-device piix3-usb-uhci,id=usb-uhci -drive if=none,id=myusb,file=usb_extended.img,format=raw -device usb-storage,drive=myusb,bus=usb-uhci.0 \

usb_rerun:
	qemu-system-x86_64 -smp 4 -m 4096 -vga std -device ahci,id=ahci -drive file=$(img),if=none,id=disk0,format=raw -device ide-hd,drive=disk0,bus=ahci.0 \
	-drive file=usb_extended.img,if=none,id=disk1,format=raw -device ide-hd,drive=disk1,bus=ahci.1 \
	-device usb-ehci,id=ehci -drive if=none,id=myusb,file=extended.img,format=raw -device usb-storage,drive=myusb,bus=ehci.0
#	-device piix3-usb-uhci,id=usb-uhci -drive if=none,id=myusb,file=extended.img,format=raw -device usb-storage,drive=myusb,bus=usb-uhci.0 \

test:
	make clean
	make all
	qemu-system-x86_64 -s -S -smp 4 -m 4096 -vga std -device ahci,id=ahci -drive file=$(img),if=none,id=disk0,format=raw -device ide-hd,drive=disk0,bus=ahci.0 \
	-drive file=extended.img,if=none,id=disk1,format=raw -device ide-hd,drive=disk1,bus=ahci.1 \
	-device usb-ehci,id=ehci -drive if=none,id=myusb,file=usb_extended.img,format=raw -device usb-storage,drive=myusb,bus=ehci.0
#	-device piix3-usb-uhci,id=usb-uhci -drive if=none,id=myusb,file=usb_extended.img,format=raw -device usb-storage,drive=myusb,bus=usb-uhci.0

.PHONY: run all clean test rerun

