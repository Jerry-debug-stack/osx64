#include "uconst.h"
#include "sysapi.h"
#include "ustring.h"
#include "mem.h"
#include "math.h"

#include <stdarg.h> // 可变参数支持

const char numString[] = "0123456789abcdefghijklmnopqrstuvwxyz";
const char numStringUpper[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

static uint32_t vsnprintf(char* buf, uint32_t size, const char* fmt, va_list args);

uint32_t sprintf(char* buf,const char* fmt,uint32_t size,...){
    va_list args;
    va_start(args, size);
    uint32_t written = vsnprintf(buf, size, fmt, args);
    va_end(args);
    return written;
}

static const char int64_min[] = "-9223372036854775808";

static uint32_t vsnprintf(char* buf, uint32_t size, const char* fmt, va_list args)
{
    uint32_t i = 0;
    uint32_t pos = 0;
    uint32_t numpos;
    uint64_t unsigned_val;
    int64_t signed_val;
    uint8_t char_upper;
    uint8_t with_header = 0;
    char numbuf[32];
    while (fmt[i] != '\0' && pos < size - 1) {
        if (fmt[i] == '%') {
            i++;
            with_header = 0;
            if (fmt[i] == 'c') {
                char ch = (char)va_arg(args, int32_t);
                buf[pos++] = ch;
                i++;
                continue;
            } else if (fmt[i] == 's') {
                char* str = va_arg(args, char*);
                uint32_t j = 0;
                if (str == (void*)0) {
                    str = "(null)";
                }
                while (str[j] != '\0' && pos < size - 1) {
                    buf[pos++] = str[j++];
                }
                i++;
                continue;
            } else if (fmt[i] == '%') {
                buf[pos++] = '%';
                i++;
                continue;
            } else if (fmt[i] == 'p') {
                unsigned_val = va_arg(args, uint64_t);
                char_upper = 0;
                with_header = 1;
                goto parse_hex;
            } else if (fmt[i] == 'f') {
                double d = va_arg(args, double);
                int precision = 6;  // 默认精度，以后可从格式中解析
                char buf_int[32], buf_frac[32];
                uint64_t int_part, frac_part;
                int negative = 0;

                // 处理 NaN 和 Inf
                if (isnan(d)) {
                    const char *nan_str = "nan";
                    for (int j = 0; nan_str[j] && pos < size - 1; j++)
                        buf[pos++] = nan_str[j];
                    i++;
                    continue;
                }
                if (isinf(d)) {
                    const char *inf_str = (d > 0) ? "inf" : "-inf";
                    for (int j = 0; inf_str[j] && pos < size - 1; j++)
                        buf[pos++] = inf_str[j];
                    i++;
                    continue;
                }

                if (d < 0) {
                    negative = 1;
                    d = -d;
                }

                // 拆分整数和小数
                double int_d, frac_d;
                frac_d = modf(d, &int_d);

                int_part = (uint64_t)int_d;

                // 计算小数部分并四舍五入
                double multiplier = 1.0;
                for (int j = 0; j < precision; j++) multiplier *= 10.0;
                frac_part = (uint64_t)(frac_d * multiplier + 0.5);  // 简单的四舍五入

                // 处理进位
                if (frac_part >= (uint64_t)multiplier) {
                    int_part++;
                    frac_part -= (uint64_t)multiplier;
                }

                // 转换整数部分
                char *p_int = buf_int + sizeof(buf_int) - 1;
                *p_int = '\0';
                uint64_t tmp = int_part;
                do {
                    *--p_int = (tmp % 10) + '0';
                    tmp /= 10;
                } while (tmp > 0);

                // 转换小数部分（注意补零）
                char *p_frac = buf_frac + sizeof(buf_frac) - 1;
                *p_frac = '\0';
                for (int j = 0; j < precision; j++) {
                    *--p_frac = (frac_part % 10) + '0';
                    frac_part /= 10;
                }
                // 如果小数部分不足精度，前面补零已经在循环中实现（因为循环固定次数）

                // 输出
                if (negative && pos < size-1) buf[pos++] = '-';
                while (*p_int && pos < size-1) buf[pos++] = *p_int++;
                if (precision > 0 && pos < size-1) buf[pos++] = '.';
                while (*p_frac && pos < size-1) buf[pos++] = *p_frac++;

                i++;
                continue;
            } else if (fmt[i] == '#') {
                with_header = 1;
                i++;
                goto parse_next;
            } else {
            parse_next:
                if (fmt[i] == 'd' || fmt[i] == 'i') {
                    signed_val = (int64_t)va_arg(args, int32_t);
                    goto parse_signed10;
                } else if (fmt[i] == 'u') {
                    unsigned_val = (uint64_t)va_arg(args, int32_t);
                    goto parse_unsigned10;
                } else if (fmt[i] == 'x') {
                    unsigned_val = va_arg(args, uint32_t);
                    char_upper = 0;
                    goto parse_hex;
                } else if (fmt[i] == 'X') {
                    unsigned_val = va_arg(args, uint32_t);
                    char_upper = 1;
                    goto parse_hex;
                } else if (fmt[i] == 'o') {
                    unsigned_val = va_arg(args, uint32_t);
                    goto parse_octal;
                } else if (fmt[i] == 'h') {
                    i++;
                    if (fmt[i] == 'u') {
                        unsigned_val = (uint16_t)va_arg(args, uint32_t);
                        goto parse_unsigned10;
                    } else if (fmt[i] == 'd' || fmt[i] == 'i') {
                        signed_val = (int16_t)va_arg(args, int32_t);
                        goto parse_signed10;
                    } else if (fmt[i] == 'x') {
                        unsigned_val = (uint16_t)va_arg(args, uint32_t);
                        char_upper = 0;
                        goto parse_hex;
                    } else if (fmt[i] == 'X') {
                        unsigned_val = (uint16_t)va_arg(args, uint32_t);
                        char_upper = 1;
                        goto parse_hex;
                    } else if (fmt[i] == 'o') {
                        unsigned_val = (uint16_t)va_arg(args, uint32_t);
                        goto parse_octal;
                    } else {
                        continue;
                    }
                } else if (fmt[i] == 'l') {
                    i++;
                    if (fmt[i] == 'd' || fmt[i] == 'i') {
                        signed_val = va_arg(args, int64_t);
                        goto parse_signed10;
                    } else if (fmt[i] == 'u') {
                        unsigned_val = va_arg(args, uint64_t);
                        goto parse_unsigned10;
                    } else if (fmt[i] == 'x') {
                        unsigned_val = va_arg(args, uint64_t);
                        char_upper = 0;
                        goto parse_hex;
                    } else if (fmt[i] == 'X') {
                        unsigned_val = va_arg(args, uint64_t);
                        char_upper = 1;
                        goto parse_hex;
                    } else if (fmt[i] == 'o') {
                        unsigned_val = va_arg(args, uint64_t);
                        goto parse_octal;
                    } else {
                        continue;
                    }
                } else {
                    continue;
                }
            }
        parse_signed10:
            if (signed_val < 0) {
                if (signed_val == INT64_MIN) {
                    int32_t j = 0;
                    while (int64_min[j] != '\0' && pos < size - 1) {
                        buf[pos++] = int64_min[j++];
                    }
                    i++;
                    continue;
                } else {
                    buf[pos++] = '-';
                    unsigned_val = (uint64_t)(-signed_val);
                }
            } else {
                unsigned_val = (uint64_t)signed_val;
            }
        parse_unsigned10:
            numpos = 0;
            do {
                numbuf[numpos++] = (unsigned_val % 10) + '0';
                unsigned_val /= 10;
            } while (unsigned_val > 0);
            for (int32_t k = numpos - 1; k >= 0 && pos < size - 1; k--) {
                buf[pos++] = numbuf[k];
            }
            i++;
            continue;
        parse_hex:
            numpos = 0;
            do {
                if ((unsigned_val & 15) < 10) {
                    numbuf[numpos++] = (unsigned_val & 15) + '0';
                } else if (char_upper) {
                    numbuf[numpos++] = (unsigned_val & 15) - 10 + 'A';
                } else {
                    numbuf[numpos++] = (unsigned_val & 15) - 10 + 'a';
                }
                unsigned_val >>= 4;
            } while (unsigned_val > 0);
            if (with_header) {
                numbuf[numpos++] = 'x';
                numbuf[numpos++] = '0';
            }
            // numbuf大小为32字节,完全足够
            for (int32_t k = numpos - 1; k >= 0 && pos < size - 1; k--) {
                buf[pos++] = numbuf[k];
            }
            i++;
            continue;
        parse_octal:
            numpos = 0;
            do {
                numbuf[numpos++] = (unsigned_val & 7) + '0';
                unsigned_val >>= 3;
            } while (unsigned_val > 0);
            // numbuf大小为32字节,完全足够
            if (with_header) {
                numbuf[numpos++] = '0';
            }
            for (int32_t k = numpos - 1; k >= 0 && pos < size - 1; k--) {
                buf[pos++] = numbuf[k];
            }
            i++;
            continue;
        } else {
            buf[pos++] = fmt[i];
        }
        i++;
    }
    buf[pos] = '\0';
    return pos;
}

uint32_t printf(const char* fmt,...){
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    uint32_t written = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    write(1,buf,written);
    return written;
}
