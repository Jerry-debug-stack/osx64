#!/bin/bash
set -e

IMG="target/hdd.img"
KERNEL="target/kernel.elf"
MOUNT="mnt"

# 清理旧镜像
rm -f $IMG
rm -rf $MOUNT

# 创建新镜像
dd if=/dev/zero of=$IMG bs=1M count=100
parted $IMG mklabel msdos
parted $IMG mkpart primary ext2 1MiB 20%
parted $IMG mkpart primary ext2 20% 80%
parted $IMG set 1 boot on

# 设置环回设备
LOOP=$(sudo losetup -Pf --show $IMG)
sudo mkfs.ext2 ${LOOP}p1

# 挂载
mkdir -p $MOUNT
sudo mount ${LOOP}p1 $MOUNT

# 安装GRUB
sudo grub-install \
  --target=i386-pc \
  --boot-directory=$MOUNT/boot \
  --modules="part_msdos ext2 multiboot all_video" \
  --recheck \
  $LOOP

# 复制内核
sudo cp $KERNEL $MOUNT/boot/

# 创建GRUB配置
sudo cp ./grub.cfg $MOUNT/boot/grub/grub.cfg 

# 清理
sudo umount $MOUNT
sudo losetup -d $LOOP
rmdir $MOUNT

echo "HDD image created: $IMG"