#include "uconst.h"
#include "sysapi.h"
#include "ustring.h"
#include "mem.h"
#include "math.h"
#include "uctype.h"

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


// 跳过空白字符
static void skip_whitespace(const char **s) {
    while (isspace(**s)) (*s)++;
}

// 将字符串转换为整数
static uint64_t str2uint(const char **s, int base, int *chars_used) {
    const char *p = *s;
    uint64_t val = 0;
    int count = 0;
    while (1) {
        int digit;
        if (isdigit(*p))
            digit = *p - '0';
        else if (isalpha(*p)) {
            char c = tolower(*p);
            if (c >= 'a' && c <= 'f')
                digit = c - 'a' + 10;
            else
                break;
        } else
            break;
        if (digit >= base) break;
        val = val * base + digit;
        p++;
        count++;
    }
    *chars_used = count;
    *s = p;
    return val;
}

static int vsscanf(const char *str, const char *fmt, va_list ap) {
    int matched = 0;
    const char *s = str;
    const char *f = fmt;

    while (*f && *s) {
        if (isspace(*f)) {
            skip_whitespace(&f);
            skip_whitespace(&s);
            continue;
        }
        if (*f == '%') {
            f++;
            if (*f == '%') {
                if (*s != '%') break;
                s++;
                f++;
                continue;
            }
            int longmod = 0;
            if (*f == 'h') {
                f++;
            } else if (*f == 'l') {
                f++;
                if (*f == 'l') {
                    f++;
                    longmod = 2;
                } else {
                    longmod = 1;
                }
            }
            int skip_ws = 1;
            if (*f == 'c') skip_ws = 0;

            if (skip_ws) skip_whitespace(&s);
            if (*s == '\0') break;

            if (*f == 'd') {
                int negative = 0;
                if (*s == '+') {
                    s++;
                } else if (*s == '-') {
                    negative = 1;
                    s++;
                }
                int used;
                uint64_t uval = str2uint(&s, 10, &used);
                if (used == 0) break;
                int64_t val = negative ? -(int64_t)uval : (int64_t)uval;
                if (longmod == 2) {
                    long long *p = va_arg(ap, long long*);
                    *p = (long long)val;
                } else if (longmod == 1) {
                    long *p = va_arg(ap, long*);
                    *p = (long)val;
                } else {
                    int *p = va_arg(ap, int*);
                    *p = (int)val;
                }
                matched++;
                f++;
                continue;
            } else if (*f == 'i') {
                int negative = 0;
                if (*s == '+') {
                    s++;
                } else if (*s == '-') {
                    negative = 1;
                    s++;
                }
                int base = 10;
                const char *p = s;
                if (*p == '0') {
                    p++;
                    if (*p == 'x' || *p == 'X') {
                        base = 16;
                        s = p + 1; // 跳过 '0x'
                    } else {
                        base = 8;
                        // s 保持指向第一个 '0'，由 str2uint 处理
                    }
                }
                int used;
                uint64_t uval = str2uint(&s, base, &used);
                if (used == 0) break;
                int64_t val = negative ? -(int64_t)uval : (int64_t)uval;
                if (longmod == 2) {
                    long long *p = va_arg(ap, long long*);
                    *p = (long long)val;
                } else if (longmod == 1) {
                    long *p = va_arg(ap, long*);
                    *p = (long)val;
                } else {
                    int *p = va_arg(ap, int*);
                    *p = (int)val;
                }
                matched++;
                f++;
                continue;
            } else if (*f == 'u') {
                // 无符号十进制
                int used;
                uint64_t val = str2uint(&s, 10, &used);
                if (used == 0) break;
                if (longmod == 2) {
                    unsigned long long *p = va_arg(ap, unsigned long long*);
                    *p = (unsigned long long)val;
                } else if (longmod == 1) {
                    unsigned long *p = va_arg(ap, unsigned long*);
                    *p = (unsigned long)val;
                } else {
                    unsigned int *p = va_arg(ap, unsigned int*);
                    *p = (unsigned int)val;
                }
                matched++;
                f++;
                continue;
            } else if (*f == 'x' || *f == 'X') {
                // 十六进制
                int used;
                uint64_t val = str2uint(&s, 16, &used);
                if (used == 0) break;
                if (longmod == 2) {
                    unsigned long long *p = va_arg(ap, unsigned long long*);
                    *p = val;
                } else if (longmod == 1) {
                    unsigned long *p = va_arg(ap, unsigned long*);
                    *p = (unsigned long)val;
                } else {
                    unsigned int *p = va_arg(ap, unsigned int*);
                    *p = (unsigned int)val;
                }
                matched++;
                f++;
                continue;
            } else if (*f == 'o') {
                // 八进制
                int used;
                uint64_t val = str2uint(&s, 8, &used);
                if (used == 0) break;
                if (longmod == 2) {
                    unsigned long long *p = va_arg(ap, unsigned long long*);
                    *p = val;
                } else if (longmod == 1) {
                    unsigned long *p = va_arg(ap, unsigned long*);
                    *p = (unsigned long)val;
                } else {
                    unsigned int *p = va_arg(ap, unsigned int*);
                    *p = (unsigned int)val;
                }
                matched++;
                f++;
                continue;
            } else if (*f == 's') {
                // 字符串（跳过前导空白，然后读取非空白字符）
                skip_whitespace(&s);
                if (*s == '\0') break;
                char *out = va_arg(ap, char*);
                while (*s && !isspace(*s)) {
                    *out++ = *s++;
                }
                *out = '\0';
                matched++;
                f++;
                continue;
            } else if (*f == 'c') {
                // 字符（不跳过空白）
                char *out = va_arg(ap, char*);
                *out = *s++;
                matched++;
                f++;
                continue;
            } else if (*f == 'p') {
                // 指针（十六进制地址）
                int used;
                uint64_t val = str2uint(&s, 16, &used);
                if (used == 0) break;
                void **p = va_arg(ap, void**);
                *p = (void*)(uintptr_t)val;
                matched++;
                f++;
                continue;
            } else {
                // 不支持的格式，跳过
                f++;
            }
        } else {
            if (*f != *s) break;
            f++;
            s++;
        }
    }
    return matched;
}

int sscanf(const char *str, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = vsscanf(str, fmt, ap);
    va_end(ap);
    return ret;
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
