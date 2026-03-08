#include <stdint.h>
#include "sysapi.h"
#include "uconst.h"
#include "ustring.h"
#include "mem.h"
#include "uprintf.h"

/* 从文件描述符 fd 的 offset 处读取 len 字节到 buf。
 * 若 is_blk 为 1，则 fd 对应块设备，所有 I/O 操作以 512 字节扇区为单位。
 * 返回 0 成功，-1 失败。
 */
static int read_bytes(int fd, int is_blk, off_t offset, void *buf, size_t len) {
    char *ptr = (char *)buf;
    size_t remaining = len;
    off_t cur_off = offset;

    if (is_blk) {
        /* 块设备：逐扇区读取 */
        while (remaining > 0) {
            off_t sector = cur_off / 512;          /* 扇区号 */
            size_t sector_off = cur_off % 512;     /* 扇区内偏移 */
            size_t to_read = 512 - sector_off;
            if (to_read > remaining)
                to_read = remaining;

            /* 定位到该扇区（lseek 参数为扇区号） */
            if (lseek(fd, sector, SEEK_SET) == -1) {
                printf("lseek (block device)");
                return -1;
            }

            char tmp[512];
            /* 读取一个扇区（read 返回扇区数，期望为 1） */
            ssize_t n = read(fd, tmp, 1);
            if (n != 1) {
                printf("read (block device)");
                return -1;
            }

            memcpy(ptr, tmp + sector_off, to_read);
            ptr += to_read;
            cur_off += to_read;
            remaining -= to_read;
        }
    } else {
        /* 普通文件：以字节为单位，可能需多次 read 才能读完 */
        if (lseek(fd, offset, SEEK_SET) == -1) {
            printf("lseek");
            return -1;
        }
        while (remaining > 0) {
            ssize_t n = read(fd, ptr, remaining);
            if (n <= 0) {
                if (n == 0)
                    printf("unexpected EOF\n");
                else
                    printf("read error\n");
                return -1;
            }
            ptr += n;
            remaining -= n;
        }
    }
    return 0;
}

/* 将 buf 中 len 字节写入文件描述符 fd 的 offset 处。
 * 若 is_blk 为 1，则 fd 对应块设备，所有 I/O 操作以 512 字节扇区为单位。
 * 返回 0 成功，-1 失败。
 */
static int write_bytes(int fd, int is_blk, off_t offset, void *buf, size_t len) {
    char *ptr = (char *)buf;
    size_t remaining = len;
    off_t cur_off = offset;

    if (is_blk) {
        /* 块设备：逐扇区处理，必要时先读后写（读-修改-写） */
        while (remaining > 0) {
            off_t sector = cur_off / 512;
            size_t sector_off = cur_off % 512;
            size_t to_write = 512 - sector_off;
            if (to_write > remaining)
                to_write = remaining;

            char tmp[512];

            /* 如果不是完整扇区，需要先读出现有内容 */
            if (sector_off != 0 || to_write != 512) {
                if (lseek(fd, sector, SEEK_SET) == -1) {
                    printf("lseek (block device for read) error\n");
                    return -1;
                }
                ssize_t n = read(fd, tmp, 1);
                if (n != 1) {
                    printf("read (block device for modify) error\n");
                    return -1;
                }
            }

            /* 将新数据拷贝到临时缓冲区的正确位置 */
            memcpy(tmp + sector_off, ptr, to_write);

            /* 写回整个扇区 */
            if (lseek(fd, sector, SEEK_SET) == -1) {
                printf("lseek (block device for write) error\n");
                return -1;
            }
            ssize_t n = write(fd, tmp, 1);
            if (n != 1) {
                printf("write (block device) error\n");
                return -1;
            }

            ptr += to_write;
            cur_off += to_write;
            remaining -= to_write;
        }
    } else {
        /* 普通文件：以字节为单位，循环写入 */
        if (lseek(fd, offset, SEEK_SET) == -1) {
            printf("lseek error\n");
            return -1;
        }
        while (remaining > 0) {
            ssize_t n = write(fd, ptr, remaining);
            if (n <= 0) {
                printf("write error\n");
                return -1;
            }
            ptr += n;
            remaining -= n;
        }
    }
    return 0;
}

#define CHUNK_SIZE (1024 * 1024)  /* 1MB 缓冲区 */

int main(char *argv[]) {
    if (argv == NULL || argv[0] == NULL || argv[1] == NULL || argv[2] == NULL ||
        argv[3] == NULL || argv[4] == NULL || argv[5] != NULL) {
        printf("Usage: input output write_offset read_offset length\n");
        exit(1);
    }

    char *input_file  = argv[0];
    char *output_file = argv[1];
    char *write_off_str = argv[2];
    char *read_off_str  = argv[3];
    char *len_str       = argv[4];

    off_t write_offset;
    off_t read_offset;
    off_t length;

    if (sscanf(write_off_str, "%ld", &write_offset) != 1) {
        printf("write_offset format error\n");
        exit(1);
    }
    if (sscanf(read_off_str, "%ld", &read_offset) != 1) {
        printf("read_offset format error\n");
        exit(1);
    }
    if (sscanf(len_str, "%ld", &length) != 1) {
        printf("len format error\n");
        exit(1);
    }

    /* 打开输入文件 */
    int infd = open(input_file, O_RDONLY, 0);
    if (infd == -1) {
        printf("open input failed\n");
        exit(1);
    }
    /* 打开输出文件（需要读写权限，因为可能读-修改-写） */
    int outfd = open(output_file, O_RDWR, 0);
    if (outfd == -1) {
        printf("open output failed\n");
        close(infd);
        exit(1);
    }

    struct stat st_in, st_out;
    if (fstat(infd, &st_in) == -1) {
        printf("fstat input error\n");
        close(infd); close(outfd);
        exit(1);
    }
    if (fstat(outfd, &st_out) == -1) {
        printf("fstat output error\n");
        close(infd); close(outfd);
        exit(1);
    }

    int in_is_blk  = (st_in.block_size == 512);  /* 根据你的 stat 结构判断块设备 */
    int out_is_blk = (st_out.block_size == 512);

    if (length < 0) {
        length = (off_t)st_in.file_size * st_in.block_size;
        if (length < 0) {  /* 防止溢出导致的负数 */
            printf("Output file size too large\n");
            close(infd); close(outfd);
            exit(1);
        }
    }
    printf("length:%ld\n",length);

    if (length == 0) {
        /* 没有数据要复制，直接成功退出 */
        close(infd); close(outfd);
        exit(0);
    }

    /* 分配固定大小的缓冲区（只分配一次，循环复用） */
    char *buffer = (char *)malloc(CHUNK_SIZE);
    if (buffer == NULL) {
        printf("malloc error\n");
        close(infd); close(outfd);
        exit(1);
    }

    off_t remaining = length;
    off_t read_pos  = read_offset;
    off_t write_pos = write_offset;

    while (remaining > 0) {
        size_t chunk = (remaining > CHUNK_SIZE) ? CHUNK_SIZE : (size_t)remaining;

        /* 从输入文件读取一个块 */
        if (read_bytes(infd, in_is_blk, read_pos, buffer, chunk) < 0) {
            printf("Failed to read from input at offset %ld\n", (long long)read_pos);
            free(buffer); close(infd); close(outfd);
            exit(1);
        }

        /* 写入输出文件 */
        if (write_bytes(outfd, out_is_blk, write_pos, buffer, chunk) < 0) {
            printf("Failed to write to output at offset %ld\n", (long long)write_pos);
            free(buffer); close(infd); close(outfd);
            exit(1);
        }

        read_pos  += chunk;
        write_pos += chunk;
        remaining -= chunk;
    }

    free(buffer);
    close(infd);
    close(outfd);
    exit(0);
}
