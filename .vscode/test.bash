#!/bin/bash
gnome-terminal --title="QEMU VM" -- qemu-system-x86_64 -cdrom ./target/kernel.iso -s -S -smp 2 -m 4096 -vga std
