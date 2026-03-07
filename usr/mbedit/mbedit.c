#include "uprintf.h"
#include "ustring.h"
#include "uconst.h"
#include "sysapi.h"
#include "mem.h"

#define NULL ((void *)0)

// 常量定义
#define SECTOR_SIZE         512
#define MBR_PART_OFFSET     446
#define PART_ENTRY_SIZE     16
#define MBR_SIGN_OFFSET     510
#define MBR_SIGNATURE       0xAA55

// 分区表项结构（紧凑对齐）
typedef struct {
    unsigned char boot_flag;      // 0x80 表示活动
    unsigned char start_chs[3];   // 起始CHS地址（通常忽略）
    unsigned char type;           // 分区类型
    unsigned char end_chs[3];     // 结束CHS地址
    unsigned int  start_lba;      // 起始LBA（小端）
    unsigned int  sectors;        // 扇区数（小端）
} __attribute__((packed)) partition_t;

static int read_mbr(int fd, unsigned char *mbr, partition_t *parts) {
    if (lseek(fd, 0, 0) != 0) return -1;
    if (read(fd, (void *)mbr, 1) != 1) return -1;

    unsigned short sig = *(unsigned short*)(mbr + MBR_SIGN_OFFSET);
    if (sig != MBR_SIGNATURE) return -2;

    for (int i = 0; i < 4; i++) {
        unsigned char *p = mbr + MBR_PART_OFFSET + i * PART_ENTRY_SIZE;
        parts[i].boot_flag = p[0];
        parts[i].type = p[4];
        parts[i].start_lba = *(unsigned int*)(p + 8);
        parts[i].sectors   = *(unsigned int*)(p + 12);
    }
    return 0;
}

static int write_mbr(int fd, unsigned char *mbr, partition_t *parts) {
    for (int i = 0; i < 4; i++) {
        unsigned char *p = mbr + MBR_PART_OFFSET + i * PART_ENTRY_SIZE;
        p[0] = parts[i].boot_flag;
        p[4] = parts[i].type;
        *(unsigned int*)(p + 8)  = parts[i].start_lba;
        *(unsigned int*)(p + 12) = parts[i].sectors;
    }
    if (lseek(fd, 0, 0) != 0) return -1;
    if (write(fd, (void *)mbr, 1) != 1) return -1;
    return 0;
}

// 打印分区表
static void print_parts(partition_t *parts) {
    printf("Num  Boot  Type    Start LBA    Sectors\n");
    for (int i = 0; i < 4; i++) {
        printf("%d    %s    0x%x    %u    %u\n",
               i+1,
               (parts[i].boot_flag & 0x80) ? "*" : " ",
               parts[i].type,
               parts[i].start_lba,
               parts[i].sectors);
    }
}

static char *read_line(void) {
    char buf[256];
    int n = read(0, buf, sizeof(buf) - 1);
    if (n <= 0) return NULL;

    if (n > 0 && buf[n-1] == '\n') {
        buf[n-1] = '\0';
    } else {
        buf[n] = '\0';
    }

    char *line = (char*)malloc(strlen(buf) + 1);
    if (!line) return NULL;
    strcpy(line, buf);
    return line;
}

// ---------- 主程序 ----------
int main(char *argv[]) {
    const char *dev;
    if (argv && argv[0]) {
        dev = argv[0];
    } else {
        printf("Usage: mbedit <device>   (e.g. /dev/sda)\n");
        exit(1);
    }

    int fd = open(dev, O_RDWR, 0);
    if (fd < 0) {
        printf("Cannot open %s\n", dev);
        exit(1);
    }

    uint8_t *mbr = (uint8_t*)malloc(SECTOR_SIZE);
    if (!mbr) {
        printf("Out of memory\n");
        exit(1);
    }

    partition_t parts[4];
    int ret = read_mbr(fd, mbr, parts);
    if (ret < 0) {
        if (ret == -2) printf("Invalid MBR signature (not a valid MBR?)\n");
        else printf("Failed to read MBR\n");
        free(mbr);
        exit(1);
    }

    printf("Current MBR partition table:\n");
    print_parts(parts);

    int modified = 0;
    char *line;

    while (1) {
        printf("mbr> ");
        line = read_line();
        if (!line) continue;   // EOF or error

        if (strcmp(line, "p") == 0) {
            print_parts(parts);
        }
        else if (strcmp(line, "q") == 0) {
            free(line);
            break;
        }
        else if (strcmp(line, "w") == 0) {
            if (modified) {
                if (write_mbr(fd, mbr, parts) == 0) {
                    printf("MBR written. Reloading partitions...\n");
                    close(fd);
                    reload_partition((void *)dev);
                    printf("Done.\n");
                    exit(0);
                } else {
                    printf("Write failed!\n");
                }
            } else {
                printf("No changes to write.\n");
            }
            free(line);
            break;
        }
        else if (line[0] == 't' || line[0] == 'b' || line[0] == 's' || line[0] == 'c') {
            int part;
            unsigned int val;
            int n;

            if (line[0] == 't') {
                n = sscanf(line, "t %d %i", &part, &val);
                if (n == 2 && part >= 1 && part <= 4) {
                    parts[part-1].type = (uint8_t)val;
                    modified = 1;
                    printf("Partition %d type set to 0x%x\n", part, val);
                } else {
                    printf("Usage: t <part(1-4)> <hex_type>\n");
                }
            }
            else if (line[0] == 'b') {
                n = sscanf(line, "b %d %i", &part, &val);
                if (n == 2 && part >= 1 && part <= 4 && (val == 0 || val == 1)) {
                    parts[part-1].boot_flag = val ? 0x80 : 0x00;
                    modified = 1;
                    printf("Partition %d boot flag set to %s\n", part, val ? "active" : "inactive");
                } else {
                    printf("Usage: b <part(1-4)> <0|1>\n");
                }
            }
            else if (line[0] == 's') {
                unsigned int lba;
                n = sscanf(line, "s %d %i", &part, &lba);
                if (n == 2 && part >= 1 && part <= 4) {
                    parts[part-1].start_lba = lba;
                    modified = 1;
                    printf("Partition %d start LBA set to %u\n", part, lba);
                } else {
                    printf("Usage: s <part(1-4)> <start_lba>\n");
                }
            }
            else if (line[0] == 'c') {
                unsigned int sectors;
                n = sscanf(line, "c %d %i", &part, &sectors);
                if (n == 2 && part >= 1 && part <= 4) {
                    parts[part-1].sectors = sectors;
                    modified = 1;
                    printf("Partition %d sector count set to %u\n", part, sectors);
                } else {
                    printf("Usage: c <part(1-4)> <sectors>\n");
                }
            }
        }
        else {
            printf("Commands:\n");
            printf("  p                    - print partition table\n");
            printf("  t <part> <hex_type>  - change partition type\n");
            printf("  b <part> <0|1>       - change boot flag (1 = active)\n");
            printf("  s <part> <start_lba> - change starting LBA (decimal)\n");
            printf("  c <part> <sectors>   - change number of sectors (decimal)\n");
            printf("  w                    - write changes and quit\n");
            printf("  q                    - quit without saving\n");
        }

        free(line);
    }

    free(mbr);
    close(fd);
    exit(0);
}
