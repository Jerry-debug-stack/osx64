#!/bin/bash
gnome-terminal --title="QEMU VM" -- qemu-system-x86_64 -hda target/hdd.img -s -S -smp 4 -m 4096 -vga std
