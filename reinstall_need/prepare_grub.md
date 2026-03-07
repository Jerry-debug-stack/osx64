# 手动准备 GRUB 引导文件指南（适用于自制操作系统）

这是与`deepseek`讨论的构建`boot.img`与`core.img`的方法，只要有了这两个文件，结合make ext2的方法和partition edit的全过程，就可以让我们的操作系统真正的能到处被安装。大家可以看看原文，做参考。

在完成ext2系统的mkfs，完善对UUID的用户态读和写之后，在dd命令的基础上，我们就将会着手开始mkfs

本文档描述如何从源码编译 GRUB，生成 `boot.img` 和定制的 `core.img`，并手动将其安装到磁盘上。该方法不依赖 `grub-install`，适合集成到自制操作系统的安装程序中。核心思想是使用一个自定义的空文件（如 `/boot/my_os_name`）作为分区标识，让 GRUB 自动定位正确的根分区。

---

## 1. 环境准备

你需要一个 Linux 环境（物理机、虚拟机或 WSL），并安装必要的编译工具和依赖。

```bash
# 以 Debian/Ubuntu 为例
sudo apt update
sudo apt install build-essential autoconf automake bison flex git
```

---

## 2. 获取 GRUB 源码

从 GNU 官方仓库克隆 GRUB 2 源码：

```bash
git clone https://git.savannah.gnu.org/git/grub.git
cd grub
```

---

## 3. 配置与编译

为目标平台 i386-pc（传统 BIOS）进行配置并编译。

```bash
# 生成构建系统文件
./bootstrap

# 配置为 PC 平台（BIOS 模式）
./configure --with-platform=pc --target=i386-pc-linux-gnu --prefix=/absolute/tmp/path

# 编译
make
```

编译完成后，所需文件位于：
- `grub-core/boot.img` – 第一阶段引导代码（512 字节）
- `grub-core/` – 模块目录，包含所有 `.mod` 文件和 `grub-mkimage` 工具

---

## 4. 创建自定义分区标识文件

在你的目标系统分区（例如 `/dev/sda1`，挂载点为 `/mnt`）上创建一个独特的空文件，用于 GRUB 搜索定位。

```bash
# 假设目标分区已挂载到 /mnt
touch /mnt/boot/my_os_name
```

**注意**：如果 `/boot` 是独立分区，则将文件放在分区根目录下，例如 `/mnt/my_os_name`，后续搜索路径需相应调整。

---

## 5. 编写 `load.cfg` 嵌入 `core.img`

创建一个名为 `load.cfg` 的文件，内容如下（根据你的实际分区布局调整路径）：

```bash
search --file /boot/my_os_name --set=root
set prefix=($root)/boot/grub
```

- 如果 `/boot` 是独立分区，改为：
  ```
  search --file /my_os_name --set=root
  set prefix=($root)/grub
  ```

将 `load.cfg` 保存在工作目录（例如 GRUB 源码根目录）。

---

## 6. 生成定制的 `core.img`

使用 `grub-mkimage` 工具生成 `core.img`，并嵌入 `load.cfg`。确保包含必要的模块：磁盘访问、分区表、文件系统、正常模式、搜索、多引导等。

```bash
./grub-core/grub-mkimage -O i386-pc -o core.img -d ./grub-core/ \
    -c load.cfg biosdisk part_msdos ext2 configfile normal search multiboot
```

参数说明：
- `-O i386-pc`：输出格式为传统 BIOS 平台
- `-o core.img`：输出文件名
- `-d ./grub-core/`：模块目录
- `-c load.cfg`：嵌入的配置文件（启动时自动执行）
- 模块列表：`biosdisk`（磁盘访问）、`part_msdos`（MBR 分区）、`ext2`（ext2/3/4 文件系统）、`configfile`（读取配置文件）、`normal`（正常模式菜单）、`search`（搜索命令）、`multiboot`（加载 Multiboot 内核）

---

## 7. 准备目标磁盘

假设目标磁盘为 `/dev/sdb`，且已创建好分区（如 `/dev/sdb1`），格式化为 ext2/ext4，并将内核、`grub.cfg` 以及自定义标识文件复制到相应位置。`grub.cfg` 示例：

```
set timeout=1
set default=0

menuentry "My OS" {
    insmod all_video
    multiboot /boot/kernel.elf root=你的内核参数
    boot
}
```

**注意**：菜单中的路径使用相对路径，因为 `root` 已在 `load.cfg` 中正确设置。

---

## 8. 将引导文件写入磁盘

### 8.1 计算 `core.img` 占用的扇区数

```bash
size=$(stat -c %s core.img)
sectors=$(( (size + 511) / 512 ))
echo "core.img 占用 $sectors 个扇区"
```

### 8.2 确定第一个分区的起始扇区

使用 `fdisk` 查看分区信息：

```bash
fdisk -l /dev/sdb
```

记下第一个分区的 **Start** 值（例如 2048）。确保 `core.img` 的扇区数 ≤ Start - 1（即 MBR 后的间隙足够）。

### 8.3 将 `core.img` 写入保留扇区

从扇区 1 开始写入（假设间隙足够）：

```bash
sudo dd if=core.img of=/dev/sdb bs=512 seek=1 count=$sectors conv=notrunc
```

### 8.4 修改 `boot.img` 中的扇区指针

`boot.img` 中偏移 `0x5C`（92 字节）处的 4 字节小端整数保存了 `core.img` 的起始扇区号。默认生成的 `core.img` 起始扇区为 1，但需要确认。

如果 `core.img` 确实从扇区 1 开始，则修改 `boot.img` 将该值设为 1：

```bash
cp grub-core/boot.img boot.mod.img
printf '\x01\x00\x00\x00' | dd of=boot.mod.img bs=1 seek=92 count=4 conv=notrunc
```

如果起始扇区不是 1，则将 `\x01\x00\x00\x00` 替换为对应的 4 字节小端值（例如扇区 2 则为 `\x02\x00\x00\x00`）。

### 8.5 备份原 MBR 的分区表

```bash
sudo dd if=/dev/sdb of=orig_partition_table.bin bs=1 skip=446 count=64
```

### 8.6 写入修改后的 `boot.img` 到 MBR

```bash
# 写入前 440 字节
sudo dd if=boot.mod.img of=/dev/sdb bs=1 count=440 conv=notrunc

# 恢复分区表
sudo dd if=orig_partition_table.bin of=/dev/sdb bs=1 seek=446 count=64 conv=notrunc
```

**注意**：MBR 最后两字节（`55 AA`）通常保持不变，无需额外操作。

---

## 9. 验证与测试

- 重启并从目标磁盘启动，应直接进入 GRUB 菜单。
- 如果进入命令行，手动执行 `set` 查看 `root` 和 `prefix` 是否正确；执行 `ls /boot/kernel.elf` 检查文件是否可读。
- 若仍无法启动，检查 `load.cfg` 中的搜索路径与实际文件位置是否一致。

---

## 10. 集成到自制系统

将生成的 `boot.img`、`core.img` 以及 `grub-mkimage` 工具（可选）复制到你的自制系统环境中。在安装程序中，重复上述写入步骤即可实现 GRUB 的自动化安装。

---

## 注意事项

- 整个过程中，**切勿覆盖目标磁盘的分区表**（偏移 446～509）。写入 MBR 时只写前 440 字节，并恢复原分区表。
- 确保 `core.img` 嵌入的模块足够你的需求。如果需要其他功能（如 `gpt` 支持、其他文件系统），在生成时添加相应模块。
- 自定义标识文件的路径必须与 `load.cfg` 中的搜索路径一致。如果 `/boot` 是独立分区，注意路径变化。

---

按照以上步骤，你可以为自制操作系统准备一套完全自主可控的 GRUB 引导文件，并实现灵活的安装。
