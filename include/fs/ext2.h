#ifndef OS_EXT2_H
#define OS_EXT2_H

#include <stdint.h>
#include "fs.h"
#include "lib/safelist.h"

/*
 * 超级块魔数
 */
#define EXT2_SUPER_MAGIC    0xEF53
#define EXT2_MAGIC_OFFSET   0x38
#define EXT2_UUID_OFFSET    0x68
/*
 * 扩展2文件系统版本
 */
#define EXT2_GOOD_OLD_REV       0       // 原始版本
#define EXT2_DYNAMIC_REV        1       // 动态版本（支持inode大小等）

/*
 * 特殊inode编号
 */
#define EXT2_BAD_INO            1       // 坏块inode
#define EXT2_ROOT_INO           2       // 根目录inode
#define EXT2_BOOT_LOADER_INO    5       // 引导加载程序inode
#define EXT2_UNDEL_DIR_INO      6       // 恢复删除目录inode
#define EXT2_GOOD_OLD_FIRST_INO 11      // 第一个非保留inode

/*
 * 默认值
 */
#define EXT2_DEFAULT_BLOCK_SIZE     1024
#define EXT2_DEFAULT_FIRST_DATA_BLOCK   1   // 块大小=1024时从块1开始

/*
 * Inode标志
 */
#define EXT2_SECRM_FL       0x00000001  // 安全删除
#define EXT2_UNRM_FL        0x00000002  // 恢复删除
#define EXT2_SYNC_FL        0x00000008  // 同步更新
#define EXT2_IMMUTABLE_FL   0x00000010  // 不可变文件
#define EXT2_APPEND_FL      0x00000020  // 只可追加
#define EXT2_NODUMP_FL      0x00000040  // 不dump
#define EXT2_NOATIME_FL     0x00000080  // 不更新atime
#define EXT2_INDEX_FL       0x00001000  // 哈希索引目录
#define EXT2_DIRSYNC_FL     0x00010000  // 目录同步

/*
 * 特性兼容集
 */
#define EXT2_FEATURE_COMPAT_DIR_PREALLOC    0x0001  // 预分配目录块
#define EXT2_FEATURE_COMPAT_IMAGIC_INODES   0x0002  // 隐含inode
#define EXT2_FEATURE_COMPAT_EXT_ATTR        0x0008  // 扩展属性
#define EXT2_FEATURE_COMPAT_RESIZE_INO      0x0010  // 保留大小inode
#define EXT2_FEATURE_COMPAT_DIR_INDEX       0x0020  // 目录索引

/*
 * 只读兼容特性集
 */
#define EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER 0x0001  // 稀疏超级块
#define EXT2_FEATURE_RO_COMPAT_LARGE_FILE   0x0002  // 大文件（>2GB）
#define EXT2_FEATURE_RO_COMPAT_BTREE_DIR    0x0004  // B树目录

/*
 * 不兼容特性集
 */
#define EXT2_FEATURE_INCOMPAT_COMPRESSION   0x0001  // 压缩
#define EXT2_FEATURE_INCOMPAT_FILETYPE      0x0002  // 目录项记录文件类型
#define EXT2_FEATURE_INCOMPAT_META_BG       0x0010  // 元数据块组

/*
 * 文件系统状态
 */
#define EXT2_VALID_FS       0x0001  // 正常卸载
#define EXT2_ERROR_FS       0x0002  // 出错

/*
 * 错误处理方式
 */
#define EXT2_ERRORS_CONTINUE    1   // 继续
#define EXT2_ERRORS_RO          2   // 只读挂载
#define EXT2_ERRORS_PANIC       3   // 系统崩溃

/*
 * 操作系统ID
 */
#define EXT2_OS_LINUX       0
#define EXT2_OS_HURD        1
#define EXT2_OS_MASIX       2
#define EXT2_OS_FREEBSD     3
#define EXT2_OS_LITES       4

/*
 * 目录项文件类型（需开启FILETYPE特性）
 */
#define EXT2_FT_UNKNOWN     0
#define EXT2_FT_REG_FILE    1   // 普通文件
#define EXT2_FT_DIR         2   // 目录
#define EXT2_FT_CHRDEV      3   // 字符设备
#define EXT2_FT_BLKDEV      4   // 块设备
#define EXT2_FT_FIFO        5   // FIFO
#define EXT2_FT_SOCK        6   // 套接字
#define EXT2_FT_SYMLINK     7   // 符号链接
#define EXT2_FT_MAX         8

/*
 * 间接块索引数量
 */
#define EXT2_NDIR_BLOCKS        12  // 直接块
#define EXT2_IND_BLOCK          12  // 一级间接块索引
#define EXT2_DIND_BLOCK         13  // 二级间接块索引
#define EXT2_TIND_BLOCK         14  // 三级间接块索引
#define EXT2_N_BLOCKS           15  // 总块索引数

/*
 * 磁盘超级块结构 - ext2_super_block
 * 位于每个块组的起始位置（但只有部分块组有备份）
 * 对于1K块大小，位于块1（偏移1024字节）
 */
typedef struct ext2_super_block {
    uint32_t  s_inodes_count;           // inode总数
    uint32_t  s_blocks_count;           // 块总数
    uint32_t  s_r_blocks_count;         // 保留块数
    uint32_t  s_free_blocks_count;      // 空闲块数
    uint32_t  s_free_inodes_count;      // 空闲inode数
    uint32_t  s_first_data_block;       // 第一个数据块
    uint32_t  s_log_block_size;         // 块大小 = 1024 << s_log_block_size
    uint32_t  s_log_frag_size;          // 片大小（通常等于块大小）
    uint32_t  s_blocks_per_group;       // 每组的块数
    uint32_t  s_frags_per_group;        // 每组的片数
    uint32_t  s_inodes_per_group;       // 每组的inode数
    uint32_t  s_mtime;                  // 最后挂载时间
    uint32_t  s_wtime;                  // 最后写入时间
    uint16_t  s_mnt_count;              // 挂载计数
    uint16_t  s_max_mnt_count;          // 最大挂载计数
    uint16_t  s_magic;                  // 魔数 0xEF53
    uint16_t  s_state;                  // 文件系统状态
    uint16_t  s_errors;                  // 错误处理方式
    uint16_t  s_minor_rev_level;        // 次版本号
    uint32_t  s_lastcheck;              // 最后检查时间
    uint32_t  s_checkinterval;          // 检查间隔
    uint32_t  s_creator_os;             // 创建操作系统
    uint32_t  s_rev_level;              // 修订版本
    uint16_t  s_def_resuid;              // 默认保留块用户ID
    uint16_t  s_def_resgid;              // 默认保留块组ID
    uint32_t  s_first_ino;              // 第一个非保留inode
    uint16_t  s_inode_size;              // inode大小
    uint16_t  s_block_group_nr;          // 当前块组号（用于备份）
    uint32_t  s_feature_compat;          // 兼容特性集
    uint32_t  s_feature_incompat;        // 不兼容特性集
    uint32_t  s_feature_ro_compat;       // 只读兼容特性集
    uint8_t   s_uuid[16];                // 卷UUID
    char      s_volume_name[16];         // 卷名
    char      s_last_mounted[64];        // 最后挂载点
    uint32_t  s_algorithm_usage_bitmap;  // 压缩算法位图
    uint8_t   s_prealloc_blocks;         // 文件预分配块数
    uint8_t   s_prealloc_dir_blocks;     // 目录预分配块数
    uint16_t  s_padding1;
    uint8_t   s_journal_uuid[16];        // 日志UUID
    uint32_t  s_journal_inum;            // 日志inode号
    uint32_t  s_journal_dev;             // 日志设备号
    uint32_t  s_last_orphan;             // 孤儿inode链表头
    uint32_t  s_hash_seed[4];             // 哈希种子
    uint8_t   s_def_hash_version;        // 默认哈希版本
    uint8_t   s_jnl_backup_type;
    uint16_t  s_reserved_word_pad;
    uint32_t  s_default_mount_opts;
    uint32_t  s_first_meta_bg;           // 第一个元数据块组
    uint32_t  s_mkfs_time;                // 创建时间
    uint32_t  s_jnl_blocks[17];           // 日志inode备份
    uint32_t  s_reserved[172];            // 填充到块大小
} __attribute__((packed)) ext2_super_block_t;

typedef struct ext2_group_desc_cache {
    uint32_t bg_block_bitmap;       // 块位图块号
    uint32_t bg_inode_bitmap;       // inode位图块号
    uint32_t bg_inode_table;        // inode表起始块号
    uint16_t bg_free_blocks_count;  // 组内空闲块数
    uint16_t bg_free_inodes_count;  // 组内空闲inode数
    uint16_t bg_used_dirs_count;     // 组内目录数
    uint8_t *block_bitmap;           // 块位图缓存（可选）
    uint8_t *inode_bitmap;           // inode位图缓存（可选）
    mutex_t block_lock;    // 保护块位图及块分配
    mutex_t inode_lock;    // 保护 inode 位图及 inode 分配
} ext2_group_desc_cache_t;

typedef struct ext2_fs_info {
    struct ext2_super_block es;
    uint32_t block_size;
    uint32_t inode_size;
    uint32_t group_count;
    ext2_group_desc_cache_t *group_descs;
} ext2_fs_info_t;

/*
 * 块组描述符 - ext2_group_desc
 * 位于超级块之后，描述每个块组的信息
 */
typedef struct ext2_group_desc {
    uint32_t  bg_block_bitmap;            // 块位图所在块号
    uint32_t  bg_inode_bitmap;            // inode位图所在块号
    uint32_t  bg_inode_table;             // inode表起始块号
    uint16_t  bg_free_blocks_count;       // 组内空闲块数
    uint16_t  bg_free_inodes_count;       // 组内空闲inode数
    uint16_t  bg_used_dirs_count;         // 组内目录数
    uint16_t  bg_flags;
    uint32_t  bg_reserved[3];              // 保留
} __attribute__((packed)) ext2_group_desc_t;

/*
 * 磁盘inode结构 - ext2_inode
 * 存储在inode表中，每个inode大小通常为128字节
 */
typedef struct ext2_inode {
    uint16_t  i_mode;                      // 文件类型和权限
    uint16_t  i_uid;                       // 用户ID（低16位）
    uint32_t  i_size;                      // 文件大小（字节）
    uint32_t  i_atime;                     // 最后访问时间
    uint32_t  i_ctime;                     // 创建时间
    uint32_t  i_mtime;                     // 最后修改时间
    uint32_t  i_dtime;                     // 删除时间
    uint16_t  i_gid;                       // 组ID（低16位）
    uint16_t  i_links_count;               // 硬链接计数
    uint32_t  i_blocks;                    // 文件占用块数（512字节块为单位）
    uint32_t  i_flags;                     // 文件标志
    uint32_t  i_osd1;                      // 操作系统相关1
    uint32_t  i_block[EXT2_N_BLOCKS];      // 块指针数组（直接+间接）
    uint32_t  i_generation;                 // 文件版本（用于NFS）
    uint32_t  i_file_acl;                   // 文件访问控制链表块号
    uint32_t  i_dir_acl;                    // 目录访问控制链表块号
    uint32_t  i_faddr;                      // 碎片地址
    uint8_t   i_frag;                       // 碎片数
    uint8_t   i_fsize;                      // 碎片大小
    uint16_t  i_pad1;
    uint16_t  i_uid_high;                   // 用户ID高16位
    uint16_t  i_gid_high;                   // 组ID高16位
    uint32_t  i_reserved2;                   // 保留
} __attribute__((packed)) ext2_inode_t;

/*
 * 目录项结构 - ext2_dir_entry_2
 * 存储在目录文件的数据块中
 * 注意：该结构是变长的，因为name字段长度可变
 */
typedef struct ext2_dir_entry_2 {
    uint32_t  inode;                         // inode号
    uint16_t  rec_len;                       // 目录项长度（包含所有字段和名称）
    uint8_t   name_len;                      // 名称长度
    uint8_t   file_type;                     // 文件类型
    char      name[];                         // 文件名（变长）
} __attribute__((packed)) ext2_dir_entry_2_t;

/*
 * 旧版目录项（无file_type）
 */
typedef struct ext2_dir_entry {
    uint32_t  inode;
    uint16_t  rec_len;
    uint16_t  name_len;
    char      name[];
} __attribute__((packed)) ext2_dir_entry_t ;

/*
 * 辅助宏
 */
#define EXT2_BLOCK_SIZE(sb)     (1024 << (sb)->s_log_block_size)
#define EXT2_BLOCK_SIZE_BITS(sb) ((sb)->s_log_block_size + 10)
#define EXT2_ADDR_PER_BLOCK(sb) (EXT2_BLOCK_SIZE(sb) / sizeof(uint32_t))
#define EXT2_DESC_PER_BLOCK(sb) (EXT2_BLOCK_SIZE(sb) / sizeof(struct ext2_group_desc))
#define EXT2_INODE_SIZE(sb)     ((sb)->s_rev_level == EXT2_GOOD_OLD_REV ? 128 : (sb)->s_inode_size)
#define EXT2_FIRST_INO(sb)      ((sb)->s_rev_level == EXT2_GOOD_OLD_REV ? EXT2_GOOD_OLD_FIRST_INO : (sb)->s_first_ino)
#define EXT2_FRAGS_PER_BLOCK(sb) (EXT2_BLOCK_SIZE(sb) / EXT2_FRAG_SIZE(sb))

/*
 * 块组相关宏
 */
#define EXT2_BLOCKS_PER_GROUP(sb)   ((sb)->s_blocks_per_group)
#define EXT2_INODES_PER_GROUP(sb)    ((sb)->s_inodes_per_group)
#define EXT2_GROUP_COUNT(sb)         ((sb)->s_blocks_count / (sb)->s_blocks_per_group + \
                                     ((sb)->s_blocks_count % (sb)->s_blocks_per_group ? 1 : 0))

/*
 * 文件模式宏（与VFS保持一致）
 */
#define EXT2_S_IFMT   0xF000  // 文件类型掩码
#define EXT2_S_IFSOCK 0xC000  // 套接字
#define EXT2_S_IFLNK  0xA000  // 符号链接
#define EXT2_S_IFREG  0x8000  // 普通文件
#define EXT2_S_IFBLK  0x6000  // 块设备
#define EXT2_S_IFDIR  0x4000  // 目录
#define EXT2_S_IFCHR  0x2000  // 字符设备
#define EXT2_S_IFIFO  0x1000  // FIFO
#define EXT2_S_ISUID  0x0800  // SUID
#define EXT2_S_ISGID  0x0400  // SGID
#define EXT2_S_ISVTX  0x0200  // 粘滞位
#define EXT2_S_IRWXU  0x01C0  // 所有者权限掩码
#define EXT2_S_IRUSR  0x0100  // 所有者读
#define EXT2_S_IWUSR  0x0080  // 所有者写
#define EXT2_S_IXUSR  0x0040  // 所有者执行
#define EXT2_S_IRWXG  0x0038  // 组权限掩码
#define EXT2_S_IRGRP  0x0020  // 组读
#define EXT2_S_IWGRP  0x0010  // 组写
#define EXT2_S_IXGRP  0x0008  // 组执行
#define EXT2_S_IRWXO  0x0007  // 其他人权限掩码
#define EXT2_S_IROTH  0x0004  // 其他人读
#define EXT2_S_IWOTH  0x0002  // 其他人写
#define EXT2_S_IXOTH  0x0001  // 其他人执行

extern super_operations_t ext2_super_ops;

#endif