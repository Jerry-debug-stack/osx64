#include "uconst.h"
#include "sysapi.h"
#include "uprintf.h"
#include "ustring.h"
#include "mem.h"
#include "uext2.h"

#define EXT2_1024_MAX_SECTOR_COUNT (1UL << 32)
#define EXT2_2048_MAX_SECTOR_COUNT (2UL << 32)
#define EXT2_4096_MAX_SECTOR_COUNT (2UL << 32)

struct ext2_params {
    uint32_t block_size;                     // 块大小（字节）
    uint32_t blocks_per_group;                // 每块组块数
    uint32_t inodes_per_group;                 // 每块组 inode 数
    uint32_t group_count;                      // 块组数量
    uint32_t inode_size;                       // inode 大小（固定128）
    uint32_t first_data_block;                  // 第一个数据块（0 或 1）
    uint32_t sb_block;                          // 超级块块号
    uint32_t gdt_block;                         // 块组描述符表起始块号
    uint32_t gdt_blocks;                        // 块组描述符表占用的块数
    uint32_t inode_table_blocks_per_group;      // 每组 inode 表占用的块数
    uint32_t total_blocks;                       // 文件系统总块数
    uint32_t total_inodes;                        // 文件系统总 inode 数
    uint32_t used_blocks;                         // 元数据占用的总块数
    uint32_t free_blocks;                          // 空闲块数
    uint32_t free_inodes;                           // 空闲 inode 数
    uint32_t root_dir_block;
    uint32_t lostfound_block;
} g_params;

static int64_t part_sector_cnt;
static int fd;
static bool is_norm;
static uint64_t rw_one_block;

static uint64_t xorshift64(uint64_t *seed) {
    uint64_t x = *seed;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *seed = x;
    return x * 0x2545F4914F6CDD1DULL;
}

static void uuid_to_string(uint8_t *uuid, char *out) {
    // 十六进制字符表
    const char hex[] = "0123456789abcdef";
    int pos = 0;

    for (int i = 0; i < 16; i++) {
        // 取当前字节的高 4 位和低 4 位
        uint8_t high = uuid[i] >> 4;   // 高半字节
        uint8_t low  = uuid[i] & 0x0F; // 低半字节

        // 转换为字符并存入缓冲区
        out[pos++] = hex[high];
        out[pos++] = hex[low];

        // 在特定位置插入连字符（标准 UUID 格式：8-4-4-4-12）
        if (i == 3 || i == 5 || i == 7 || i == 9) {
            out[pos++] = '-';
        }
    }

    out[36] = '\0';  // 字符串结束符
}

static void generate_and_print_uuid(uint64_t part_sectors,uint8_t uuid[16]) {
    struct utimespec ts;
    clock_gettime(&ts);

    // 构造种子：使用秒、纳秒和分区大小
    uint64_t seed = (uint64_t)ts.tv_sec ^ (uint64_t)ts.tv_nsec ^ part_sectors;

    // 生成16字节（128位）随机数
    uint64_t r1 = xorshift64(&seed);
    uint64_t r2 = xorshift64(&seed);
    // 将两个64位数拆成16字节（小端顺序无关紧要，只要随机即可）
    for (int i = 0; i < 8; i++) {
        uuid[i] = (r1 >> (i * 8)) & 0xFF;
        uuid[8 + i] = (r2 >> (i * 8)) & 0xFF;
    }

    // 设置版本号（第7字节的高4位为0100，即版本4）
    uuid[6] = (uuid[6] & 0x0F) | 0x40;   // 清除高4位，设置为0100

    // 设置变体（第9字节的高2位为10，即变体1）
    uuid[8] = (uuid[8] & 0x3F) | 0x80;   // 清除高2位，设置为10

    char buf[37];
    uuid_to_string(uuid, buf);
    printf("Filesystem UUID: %s\n", buf);
}

static void get_block_size(void){
    int n;
    char input_buf[32];
    while (1)
    {
        printf("Enter block size (1024, 2048, 4096) [default 1024]: ");
        n = read(0, input_buf, sizeof(input_buf) - 1);
        if (n > 0){
            input_buf[n] = '\0';
            if (sscanf(input_buf, "%u", &g_params.block_size) != 1) {
                printf("Invalid input.\n");
            } else {
                if (g_params.block_size == 1024){
                    if (part_sector_cnt > (int64_t)EXT2_1024_MAX_SECTOR_COUNT){
                        printf("block size 1024 is too small for the part\n");
                    }else {
                        rw_one_block = is_norm ? 1024 : 2;
                        break;
                    }
                }else if (g_params.block_size == 2048)
                {
                    if (part_sector_cnt > (int64_t)EXT2_2048_MAX_SECTOR_COUNT){
                        printf("block size 2048 is too small for the part\n");
                    }else{
                        rw_one_block = is_norm ? 2048 : 4;
                        break;
                    }
                }else if (g_params.block_size == 4096)
                {
                    rw_one_block = is_norm ? 4096 : 8;
                    break;
                }else{
                    printf("Unsupported block size.\n");
                }
            }
        }
    }
}

static int compute_layout(void) {
    uint64_t device_bytes = (uint64_t)part_sector_cnt * 512;
    if (g_params.block_size == 1024)
        device_bytes -= 1024;
    uint32_t fs_blocks_total = device_bytes / g_params.block_size;  // 设备可用的总块数（向下取整）
    if (fs_blocks_total == 0) {
        printf("Device too small for block size %u\n", g_params.block_size);
        return -1;
    }

    // 1. 基础参数
    g_params.inode_size = 128;
    uint32_t block_size = g_params.block_size;

    // 每块组块数（按块位图最大容量）
    uint32_t blocks_per_group = block_size * 8;
    g_params.blocks_per_group = blocks_per_group;

    // 每个块可容纳的 inode 数
    uint32_t inodes_per_block = block_size / 128;
    // 目标 inode 密度：每 2KB 一个 inode
    uint32_t target_inodes_per_group = (blocks_per_group * block_size) / 2048;
    if (target_inodes_per_group < inodes_per_block)
        target_inodes_per_group = inodes_per_block;
    // 调整为 inodes_per_block 的整数倍
    uint32_t inodes_per_group = (target_inodes_per_group + inodes_per_block - 1)
                                / inodes_per_block * inodes_per_block;
    g_params.inodes_per_group = inodes_per_group;

    uint32_t inode_table_blocks = inodes_per_group / inodes_per_block;
    g_params.inode_table_blocks_per_group = inode_table_blocks;

    // 2. 初步计算组数（向下取整）
    uint32_t full_groups = fs_blocks_total / blocks_per_group;        // 完整块组数

    if (full_groups == 0) {
        printf("Device too small for even one full block group\n");
        return -1;
    }

    // 3. 根据 group0 的 GDT 大小（依赖组数）需要先估计 GDT 块数
    // 先假设组数为 full_groups，计算 GDT 块数
    uint32_t gdt_bytes = full_groups * sizeof(struct ext2_group_desc);
    uint32_t gdt_blocks = (gdt_bytes + block_size - 1) / block_size;

    uint32_t group0_needed = 1 + gdt_blocks + 1 + 1 + inode_table_blocks + 1 + 1;
    // 超级块 + GDT + 块位图 + inode位图 + inode表 + 根目录块 + lost+found块
    if (group0_needed > blocks_per_group) {
        printf("Group 0 metadata exceeds blocks per group.\n");
        return -1;
    }

    // 4. 决定是否使用尾部块组
    uint32_t group_count;
    uint32_t used_blocks;

    group_count = full_groups;
    used_blocks = full_groups * blocks_per_group;
    // GDT 块数保持不变（已用 full_groups 计算）

    // 5. 确定最终组数和相关参数
    g_params.group_count = group_count;
    g_params.gdt_blocks = gdt_blocks;
    g_params.gdt_block = (block_size == 1024) ? 2 : 1;  // 根据块大小设置 GDT 起始块号（见注）

    // 注：对于块大小 1024，超级块在块1，GDT 从块2开始；其他块大小超级块在块0，GDT 从块1开始。
    // 但 gdt_block 在写入时使用，这里只需记录块号，具体数值依赖于前面计算的 sb_block。
    // 我们可以在 compute_layout 中也设置 sb_block 和 first_data_block。
    if (block_size == 1024) {
        g_params.first_data_block = 1;
        g_params.sb_block = 1;
    } else {
        g_params.first_data_block = 0;
        g_params.sb_block = 0;
    }

    // 6. 计算总块数、总 inode 数、空闲块/空闲 inode
    g_params.total_blocks = used_blocks + g_params.first_data_block;   // 实际使用的块数（可能小于设备总块数）
    g_params.total_inodes = group_count * inodes_per_group;

    // 元数据总占用块数：
    // （超级块(1) + GDT(gdt_blocks) + 块位图(1) + inode位图(1) + inode表(inode_table_blocks)） + root dir
    g_params.used_blocks = group_count * (1 + 1 + 1 + inode_table_blocks + gdt_blocks) + 1 + 1;
    // 确保 used_blocks 不超过 total_blocks（应该如此）
    if (g_params.used_blocks > g_params.total_blocks) {
        printf("Internal error: used_blocks > total_blocks\n");
        return -1;
    }
    g_params.free_blocks = g_params.total_blocks - g_params.first_data_block - g_params.used_blocks;

    // 空闲 inode：除了根目录 inode 2 外全为空
    g_params.free_inodes = (inodes_per_group - 11) + (group_count- 1) * inodes_per_group;;   // inode 2 预留给根目录

    return 0;
}

static int write_superblock(void) {
    // 分配一个块大小的缓冲区，并清零
    uint8_t *sb_buf = malloc(1024);
    if (!sb_buf) {
        printf("Failed to allocate superblock buffer\n");
        return -1;
    }
    memset(sb_buf, 0, 1024);

    ext2_super_block_t *sb = (ext2_super_block_t *)sb_buf;

    // 填充基本字段
    sb->s_inodes_count = g_params.total_inodes;
    sb->s_blocks_count = g_params.total_blocks;
    sb->s_r_blocks_count = 0;                      // 保留块暂设为0
    sb->s_free_blocks_count = g_params.free_blocks;
    sb->s_free_inodes_count = g_params.free_inodes;
    sb->s_first_data_block = g_params.first_data_block;
    sb->s_log_block_size = (g_params.block_size == 1024) ? 0 : (g_params.block_size == 2048 ? 1 : 2);
    sb->s_log_frag_size = sb->s_log_block_size;    // 片段大小等于块大小
    sb->s_blocks_per_group = g_params.blocks_per_group;
    sb->s_frags_per_group = g_params.blocks_per_group;
    sb->s_inodes_per_group = g_params.inodes_per_group;
    sb->s_wtime = sb->s_mtime = time();
    sb->s_mnt_count = 0;
    sb->s_max_mnt_count = 0xFFFF;   // 通常为 -1，即无限制
    sb->s_magic = 0xEF53;
    sb->s_state = 1;        // EXT2_VALID_FS
    sb->s_errors = 1;       // EXT2_ERRORS_CONTINUE
    sb->s_minor_rev_level = 0;
    sb->s_lastcheck = 0;
    sb->s_checkinterval = 0;
    sb->s_creator_os = 0;   // Linux
    sb->s_rev_level = 1;    // EXT2_DYNAMIC_REV
    sb->s_def_resuid = 0;
    sb->s_def_resgid = 0;
    sb->s_first_ino = 11;    // 第一个非保留 inode
    sb->s_inode_size = 128;
    sb->s_block_group_nr = 0;
    // 高级特性全设为0
    sb->s_feature_compat = 0;
    sb->s_feature_incompat = EXT2_FEATURE_INCOMPAT_FILETYPE;
    sb->s_feature_ro_compat = 0;
    // 生成UUID
    generate_and_print_uuid(g_params.total_blocks,sb->s_uuid);
    // 卷名（可选）
    strcpy((char *)sb->s_volume_name, "mkfs.ext2");
    // 最后挂载点
    memset(sb->s_last_mounted, 0, 64);
    // 其他字段默认为0

    // 计算设备偏移量（字节）
    off_t offset = rw_one_block;
    if (lseek(fd, offset, SEEK_SET) != offset) {
        printf("Failed to seek to superblock\n");
        free(sb_buf);
        return -1;
    }

    ssize_t written = write(fd, (void *)sb_buf, rw_one_block);
    if (written != (ssize_t)rw_one_block) {
        printf("Failed to write superblock (wrote %ld)\n", written);
        free(sb_buf);
        return -1;
    }

    free(sb_buf);
    return 0;
}

static int write_group_descriptors(void) {
    uint32_t block_size = g_params.block_size;
    uint32_t groups = g_params.group_count;
    uint32_t gdt_blocks = g_params.gdt_blocks;
    uint32_t gdt_start_block = g_params.gdt_block;           // ext2 块号
    uint32_t inode_table_blocks = g_params.inode_table_blocks_per_group;
    uint32_t blocks_per_group = g_params.blocks_per_group;
    uint32_t inodes_per_group = g_params.inodes_per_group;
    uint32_t sb_block = g_params.sb_block;
    uint32_t first_data_block = g_params.first_data_block;
    size_t buf_size = gdt_blocks * block_size;

    uint8_t *buf = malloc(buf_size);
    if (!buf) {
        printf("Failed to allocate GDT buffer\n");
        return -1;
    }
    memset(buf, 0, buf_size);

    for (uint32_t i = 0; i < groups; i++) {
        ext2_group_desc_t *desc = (ext2_group_desc_t *)(buf + i * sizeof(ext2_group_desc_t));
        uint32_t group_start_block = i * blocks_per_group + first_data_block;   // 该组的起始块号（绝对块号，即超级块备份所在）

        // 块位图块号
        if (i == 0) {
            // 组0：超级块 + GDT 之后
            desc->bg_block_bitmap = sb_block + 1 + gdt_blocks;
        } else {
            // 其他组：超级块备份 + GDT 备份之后
            desc->bg_block_bitmap = group_start_block + 1 + gdt_blocks;
        }

        // inode 位图块号
        desc->bg_inode_bitmap = desc->bg_block_bitmap + 1;

        // inode 表起始块号
        desc->bg_inode_table = desc->bg_inode_bitmap + 1;

        // 计算本组已用块数（包括所有元数据）
        uint32_t used_blocks_in_group;
        if (i == 0) {
            // 组0：超级块 + GDT + 块位图 + inode位图 + inode表 + 根目录数据块 + lost+found数据块
            used_blocks_in_group = 1 + gdt_blocks + 1 + 1 + inode_table_blocks + 1 + 1;
            g_params.root_dir_block = sb_block + 1 + gdt_blocks + 1 + 1 + inode_table_blocks;
            g_params.lostfound_block = sb_block + 1 + gdt_blocks + 1 + 1 + inode_table_blocks + 1;
        } else {
            // 其他组：超级块备份 + GDT备份 + 块位图 + inode位图 + inode表
            used_blocks_in_group = 1 + gdt_blocks + 1 + 1 + inode_table_blocks;
        }
        uint32_t free_blocks = blocks_per_group - used_blocks_in_group;
        if (free_blocks > 0xFFFF) {
            printf("Warning: free blocks in group %u exceed 16-bit limit\n", i);
            desc->bg_free_blocks_count = 0xFFFF;
        } else {
            desc->bg_free_blocks_count = (uint16_t)free_blocks;
        }

        uint32_t free_inodes;
        if (i == 0) {
            free_inodes = inodes_per_group - 11;
        } else {
            free_inodes = inodes_per_group;
        }
        if (free_inodes > 0xFFFF) {
            desc->bg_free_inodes_count = 0xFFFF;
        } else {
            desc->bg_free_inodes_count = (uint16_t)free_inodes;
        }

        // 目录数：根目录和lost+found在组0
        desc->bg_used_dirs_count = (i == 0) ? 2 : 0;

        desc->bg_flags = 0;
        // bg_reserved[3] 已在 memset 中清零
    }

    // 将 GDT 写入设备（注意：偏移量为字节）
    off_t offset = (off_t)gdt_start_block * rw_one_block;
    if (lseek(fd, offset, SEEK_SET) != offset) {
        printf("Failed to seek to GDT start\n");
        free(buf);
        return -1;
    }

    ssize_t written = write(fd, (void *)buf, rw_one_block);
    if (written != (ssize_t)rw_one_block) {
        printf("Failed to write GDT (wrote %ld)\n", written);
        free(buf);
        return -1;
    }

    free(buf);
    return 0;
}

static int init_block_bitmap(void) {
    uint32_t block_size = g_params.block_size;
    uint32_t groups = g_params.group_count;
    uint32_t blocks_per_group = g_params.blocks_per_group;
    uint32_t inode_table_blocks = g_params.inode_table_blocks_per_group;
    uint32_t gdt_blocks = g_params.gdt_blocks;
    uint32_t first_data_block = g_params.first_data_block;

    uint8_t *bitmap_buf = malloc(block_size);
    if (!bitmap_buf) {
        printf("Failed to allocate block bitmap buffer\n");
        return -1;
    }

    for (uint32_t group = 0; group < groups; group++) {
        memset(bitmap_buf, 0, block_size);

        uint32_t group_start_abs = group * blocks_per_group + first_data_block; // 组内第一个块（超级块）的绝对块号

        // 组内相对块号
        uint32_t rel_super = 0;
        uint32_t rel_gdt_start = 1;                           // GDT 起始块
        uint32_t rel_bitmap = rel_gdt_start + gdt_blocks;     // 块位图
        uint32_t rel_inode_bitmap = rel_bitmap + 1;           // inode 位图
        uint32_t rel_inode_table = rel_inode_bitmap + 1;      // inode 表起始
        uint32_t rel_root_dir = rel_inode_table + inode_table_blocks; // 根目录数据块（仅组0）

        #define SET_BIT(rel_block) \
            do { \
                if ((rel_block) < blocks_per_group) { \
                    bitmap_buf[(rel_block) / 8] |= (1 << ((rel_block) % 8)); \
                } \
            } while(0)

        // 标记所有元数据块
        SET_BIT(rel_super);                                   // 超级块
        for (uint32_t i = 0; i < gdt_blocks; i++) {           // GDT 块
            SET_BIT(rel_gdt_start + i);
        }
        SET_BIT(rel_bitmap);                                   // 块位图自身
        SET_BIT(rel_inode_bitmap);                             // inode 位图
        for (uint32_t i = 0; i < inode_table_blocks; i++) {    // inode 表
            SET_BIT(rel_inode_table + i);
        }
        if (group == 0) {
            SET_BIT(rel_root_dir);                             // 根目录数据块
            uint32_t rel_lostfound = rel_inode_table + inode_table_blocks + 1;  // 根目录块在 inode 表后 +1，lost+found 再 +1
            SET_BIT(rel_lostfound);
        }

        #undef SET_BIT

        // 块位图所在的绝对块号
        off_t offset = (off_t)(group_start_abs + rel_bitmap) * rw_one_block;
        if (lseek(fd, offset, SEEK_SET) != offset) {
            printf("Failed to seek to block bitmap of group %u\n", group);
            free(bitmap_buf);
            return -1;
        }
        ssize_t written = write(fd, (void *)bitmap_buf, rw_one_block);
        if (written != (ssize_t)rw_one_block) {
            printf("Failed to write block bitmap of group %u\n", group);
            free(bitmap_buf);
            return -1;
        }
    }

    free(bitmap_buf);
    return 0;
}

static int init_inode_bitmap(void) {
    uint32_t block_size = g_params.block_size;
    uint32_t groups = g_params.group_count;
    uint32_t blocks_per_group = g_params.blocks_per_group;
    uint32_t gdt_blocks = g_params.gdt_blocks;
    uint32_t sb_block = g_params.sb_block;

    uint8_t *bitmap_buf = malloc(block_size);
    if (!bitmap_buf) {
        printf("Failed to allocate inode bitmap buffer\n");
        return -1;
    }

    for (uint32_t group = 0; group < groups; group++) {
        // 计算本组 inode 位图的绝对块号
        uint32_t bitmap_block;
        if (group == 0) {
            bitmap_block = sb_block + 1 + gdt_blocks + 1;   // 块位图之后
        } else {
            bitmap_block = group * blocks_per_group + g_params.first_data_block + 2 + gdt_blocks;    // 每组第三块
        }

        memset(bitmap_buf, 0, block_size);

        // inode 位图中每个比特对应一个 inode，组内 inode 编号从 1 到 inodes_per_group
        // 需要将已分配的 inode 对应位置 1
        // 对于组0：前 10 个 inode 保留（inode 1-10），其中 inode 2 是根目录，其余保留未用但应标记为已用以避免分配
        // 为简化，这里将组0的前 10 个 inode 全部标记为已用（包括 inode 2）
        // 其他组没有保留 inode，全空闲

        if (group == 0) {
            // 标记前 11 个 inode 为已用（inode 编号 1-11）
            // 注意：inode 编号从 1 开始，在位图中偏移 0 对应 inode 1
            for (uint32_t i = 1; i <= 11; i++) {
                uint32_t bit_index = i - 1;  // 位图中的比特位置
                bitmap_buf[bit_index / 8] |= (1 << (bit_index % 8));
            }
        }
        // 其他组不标记任何 inode，保持全0（全部空闲）
        uint32_t valid_bytes = (g_params.inodes_per_group + 7) / 8;
        for (uint32_t j = valid_bytes; j < block_size; j++) {
            bitmap_buf[j] = 0xFF;
        }

        // 写入设备
        off_t offset = (off_t)bitmap_block * rw_one_block;
        if (lseek(fd, offset, SEEK_SET) != offset) {
            printf("Failed to seek to inode bitmap of group %u\n", group);
            free(bitmap_buf);
            return -1;
        }
        ssize_t written = write(fd, (void *)bitmap_buf, rw_one_block);
        if (written != (ssize_t)rw_one_block) {
            printf("Failed to write inode bitmap of group %u\n", group);
            free(bitmap_buf);
            return -1;
        }
    }

    free(bitmap_buf);
    return 0;
}

static int init_inode_tables(void) {
    uint32_t block_size = g_params.block_size;
    uint32_t groups = g_params.group_count;
    uint32_t inode_table_blocks = g_params.inode_table_blocks_per_group;
    uint32_t blocks_per_group = g_params.blocks_per_group;
    uint32_t gdt_blocks = g_params.gdt_blocks;
    uint32_t sb_block = g_params.sb_block;

    // 分配一个块的缓冲区，用于清零写入
    uint8_t *zero_buf = malloc(block_size);
    if (!zero_buf) {
        printf("Failed to allocate zero buffer\n");
        return -1;
    }
    memset(zero_buf, 0, block_size);

    for (uint32_t group = 0; group < groups; group++) {
        // 计算本组 inode 表的起始块号（绝对块号）
        uint32_t inode_table_start;
        if (group == 0) {
            inode_table_start = sb_block + 1 + gdt_blocks + 2;  // 超级块 + GDT + 块位图 + inode位图
        } else {
            inode_table_start = group * blocks_per_group + g_params.first_data_block + 3 + gdt_blocks;   // 每组2、3块是位图
        }

        // 写入 inode_table_blocks 个块，全部填零
        for (uint32_t i = 0; i < inode_table_blocks; i++) {
            uint32_t block_num = inode_table_start + i;
            off_t offset = (off_t)block_num * rw_one_block;
            if (lseek(fd, offset, SEEK_SET) != offset) {
                printf("Failed to seek to inode table block %u in group %u\n", block_num, group);
                free(zero_buf);
                return -1;
            }
            ssize_t written = write(fd, (void *)zero_buf, rw_one_block);
            if (written != (ssize_t)rw_one_block) {
                printf("Failed to write zero to inode table block %u\n", block_num);
                free(zero_buf);
                return -1;
            }
        }
    }

    free(zero_buf);
    return 0;
}

int create_initial_dirs(void) {
    uint32_t block_size = g_params.block_size;
    uint32_t root_inode_num = 2;
    uint32_t lostfound_inode_num = 11;
    uint32_t root_dir_block = g_params.root_dir_block;
    uint32_t lostfound_block = g_params.lostfound_block;
    uint32_t gdt_blocks = g_params.gdt_blocks;
    uint32_t sb_block = g_params.sb_block;

    // ---------- 1. 填充 lost+found 数据块 ----------
    uint8_t *data_buf = malloc(block_size);
    if (!data_buf) {
        printf("Failed to allocate data buffer for initial directory\n");
        return -1;
    }
    memset(data_buf, 0, block_size);

    // 目录项 "." 指向自己
    struct ext2_dir_entry_2 *entry = (struct ext2_dir_entry_2 *)data_buf;
    entry->inode = lostfound_inode_num;
    entry->name_len = 1;
    entry->file_type = DT_DIR;
    entry->rec_len = 12;     // 8 + 1 + 3 填充到4字节边界
    entry->name[0] = '.';

    // 目录项 ".." 指向根目录
    entry = (struct ext2_dir_entry_2 *)(data_buf + 12);
    entry->inode = root_inode_num;
    entry->name_len = 2;
    entry->file_type = DT_DIR;
    entry->rec_len = block_size - 12;   // 占据剩余空间
    entry->name[0] = '.';
    entry->name[1] = '.';

    // 写入数据块
    off_t offset = (off_t)lostfound_block * rw_one_block;
    if (lseek(fd, offset, SEEK_SET) != offset) {
        printf("Failed to seek to lost+found data block\n");
        free(data_buf);
        return -1;
    }
    ssize_t written = write(fd, (void *)data_buf, rw_one_block);
    if (written != (ssize_t)rw_one_block) {
        printf("Failed to write lost+found directory data block\n");
        free(data_buf);
        return -1;
    }

    // ---------- 2. 填充根目录数据块（包含三个目录项） ----------
    memset(data_buf, 0, block_size);

    // 目录项 "." (inode 2)
    entry = (struct ext2_dir_entry_2 *)data_buf;
    entry->inode = root_inode_num;
    entry->name_len = 1;
    entry->file_type = DT_DIR;
    entry->rec_len = 12;
    entry->name[0] = '.';

    // 目录项 ".." (inode 2)
    entry = (struct ext2_dir_entry_2 *)(data_buf + 12);
    entry->inode = root_inode_num;
    entry->name_len = 2;
    entry->rec_len = 12;   // 暂时设为12，后面会调整第三个目录项
    entry->name[0] = '.';
    entry->name[1] = '.';

    // 目录项 "lost+found" (inode 11)
    entry = (struct ext2_dir_entry_2 *)(data_buf + 24);
    entry->inode = lostfound_inode_num;
    entry->file_type = DT_DIR;
    entry->name_len = 10;
    // 计算 rec_len：从当前偏移到块尾的距离
    entry->rec_len = block_size - 24;
    memcpy(entry->name, "lost+found", 10);

    // 写入根目录数据块
    offset = (off_t)root_dir_block * rw_one_block;
    if (lseek(fd, offset, SEEK_SET) != offset) {
        printf("Failed to seek to root dir data block\n");
        free(data_buf);
        return -1;
    }
    written = write(fd, (void *)data_buf, rw_one_block);
    if (written != (ssize_t)rw_one_block) {
        printf("Failed to write root dir directory data block\n");
        free(data_buf);
        return -1;
    }
    free(data_buf);

    // ---------- 3. 初始化根目录 inode (inode 2) ----------
    // 计算 inode 2 的位置（与之前相同）
    uint32_t inode_size = g_params.inode_size;
    uint32_t inode_table_start = sb_block + 1 + gdt_blocks + 2;  // 组0 inode 表起始块
    uint32_t inode_index = 1;   // inode 2 的组内偏移为1
    uint32_t inode_offset = inode_index * inode_size;
    uint32_t target_block = inode_table_start + (inode_offset / block_size);
    uint32_t offset_in_block = inode_offset % block_size;

    uint8_t *inode_buf = malloc(block_size);
    if (!inode_buf) {
        printf("Failed to allocate block buffer for root inode\n");
        return -1;
    }

    // 读取 inode 表块
    offset = (off_t)target_block * rw_one_block;
    if (lseek(fd, offset, SEEK_SET) != offset) {
        printf("Failed to seek to inode table block\n");
        free(inode_buf);
        return -1;
    }
    if (read(fd, (void *)inode_buf, rw_one_block) != (ssize_t)rw_one_block) {
        printf("Failed to read inode table block\n");
        free(inode_buf);
        return -1;
    }

    // 填充根 inode
    struct ext2_inode *inode = (struct ext2_inode *)(inode_buf + offset_in_block);
    memset(inode, 0, inode_size);
    inode->i_mode = 0x41ED;          // 目录 0755
    inode->i_uid = 0;
    inode->i_size = block_size;
    inode->i_links_count = 3;
    inode->i_blocks = rw_one_block;
    inode->i_block[0] = root_dir_block;
    inode->i_atime = inode->i_ctime = inode->i_mtime = time();
    // 其他字段保持0

    // 写回 inode 表块
    if (lseek(fd, offset, SEEK_SET) != offset) {
        printf("Failed to seek back for writing inode table\n");
        free(inode_buf);
        return -1;
    }
    if (write(fd, (void *)inode_buf, rw_one_block) != (ssize_t)rw_one_block) {
        printf("Failed to write inode table block\n");
        free(inode_buf);
        return -1;
    }

    // ---------- 4. 初始化 lost+found inode (inode 11) ----------
    inode_index = 10;   // inode 11 的组内偏移为10
    inode_offset = inode_index * inode_size;
    target_block = inode_table_start + (inode_offset / block_size);
    offset_in_block = inode_offset % block_size;

    offset = (off_t)target_block * rw_one_block;
    if (lseek(fd, offset, SEEK_SET) != offset) {
        printf("Failed to seek to inode table block\n");
        free(inode_buf);
        return -1;
    }
    if (read(fd, (void *)inode_buf, rw_one_block) != (ssize_t)rw_one_block) {
        printf("Failed to read inode table block\n");
        free(inode_buf);
        return -1;
    }

    inode = (struct ext2_inode *)(inode_buf + offset_in_block);
    memset(inode, 0, inode_size);
    inode->i_mode = 0x41ED;          // 目录 0755
    inode->i_uid = 0;
    inode->i_size = block_size;
    inode->i_links_count = 2;         // "." 和 ".."
    inode->i_blocks = rw_one_block;
    inode->i_block[0] = lostfound_block;
    inode->i_ctime = inode->i_mtime = inode->i_atime = time();

    // 写回
    if (lseek(fd, offset, SEEK_SET) != offset) {
        printf("Failed to seek back for writing inode table\n");
        free(inode_buf);
        return -1;
    }
    if (write(fd, (void *)inode_buf, rw_one_block) != (ssize_t)rw_one_block) {
        printf("Failed to write inode table block\n");
        free(inode_buf);
        return -1;
    }

    free(inode_buf);
    return 0;
}

static int format_ext2(void){
    if (write_superblock())
        return -1;
    if (write_group_descriptors())
        return -1;
    if (init_block_bitmap())
        return -1;
    if (init_inode_bitmap())
        return -1;
    if (init_inode_tables())
        return -1;
    if (create_initial_dirs())
        return -1;
    return 0;
}

int main(char *argv[]) {
    char device[256];

    // 检查是否有设备参数
    if (argv == NULL || argv[0] == NULL) {
        printf("Usage: <program> <device>\n");
        exit(1);
    }
    strcpy(device, argv[0]);

    // 打开设备
    fd = open(device, O_RDWR, 0);
    if (fd < 0) {
        printf("Failed to open device %s\n", device);
        exit(1);
    }

    // 获取设备大小
    lseek(fd, 0, SEEK_SET);
    part_sector_cnt = lseek(fd, 0, SEEK_END);
    int64_t start = lseek(fd,0,SEEK_SET);
    if (part_sector_cnt < 0 || start < 0) {
        printf("Failed to get device size\n");
        close(fd);
        exit(1);
    }

    stat_t stat;
    if (fstat(fd,&stat)){
        printf("fstat failed\n");
        close(fd);
        exit(1);
    }
    if (stat.block_size == 1){
        printf("open a normal file\n");
        part_sector_cnt /= 512;
        is_norm = true;
    }else{
        printf("open a partition\n");
        is_norm = false;
    }

    if (part_sector_cnt > (int64_t)EXT2_4096_MAX_SECTOR_COUNT){
        printf("too big partition,ext2 is not able to handle this\n");
        close(fd);
        exit(1);
    }

    get_block_size();

    if (compute_layout() < 0) {
        printf("Layout computation failed\n");
        close(fd);
        exit(1);
    }

    printf("Layout parameters:\n");
    printf("  block_size = %u\n", g_params.block_size);
    printf("  total_blocks = %u\n", g_params.total_blocks);
    printf("  blocks_per_group = %u\n", g_params.blocks_per_group);
    printf("  group_count = %u\n", g_params.group_count);
    printf("  inodes_per_group = %u\n", g_params.inodes_per_group);
    printf("  inode_table_blocks = %u\n", g_params.inode_table_blocks_per_group);
    printf("  gdt_blocks = %u\n", g_params.gdt_blocks);
    printf("  sb_block = %u\n", g_params.sb_block);
    printf("  gdt_block = %u\n", g_params.gdt_block);
    printf("  first_data_block = %u\n", g_params.first_data_block);
    printf("  used_blocks = %u\n", g_params.used_blocks);
    printf("  free_blocks = %u\n", g_params.free_blocks);
    printf("  total_inodes = %u\n", g_params.total_inodes);
    printf("  free_inodes = %u\n", g_params.free_inodes);

    if (format_ext2() < 0) {
        printf("Formatting failed\n");
        close(fd);
        exit(1);
    }

    close(fd);
    printf("ext2 filesystem created successfully on %s\n", device);
    exit(0);
    __builtin_unreachable();
}

