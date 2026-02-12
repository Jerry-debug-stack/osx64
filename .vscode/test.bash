#!/bin/bash
gnome-terminal --title="QEMU VM" -- qemu-system-x86_64 -s -S -smp 4 -m 4096 -vga std -device ahci,id=ahci -drive file=target/hdd.img,if=none,id=disk0,format=raw -device ide-hd,drive=disk0,bus=ahci.0 -drive file=extended.img,if=none,id=disk1,format=raw -device ide-hd,drive=disk1,bus=ahci.1
