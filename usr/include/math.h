#ifndef OS_USR_MATH_H
#define OS_USR_MATH_H

#include <stdint.h>
#include "ustring.h"

static inline int isnan(double x) {
    uint64_t u;
    memcpy(&u, &x, sizeof(u));
    return (u & 0x7fffffffffffffffULL) > 0x7ff0000000000000ULL;
}

static inline int isinf(double x) {
    uint64_t u;
    memcpy(&u, &x, sizeof(u));
    return (u & 0x7fffffffffffffffULL) == 0x7ff0000000000000ULL;
}

static double modf(double x, double *iptr) {
    // 处理负数：转化为正数处理，最后恢复符号
    if (x < 0) {
        double pos_int;
        double frac = modf(-x, &pos_int);
        *iptr = -pos_int;
        return -frac;
    }
    // 对于足够大的数（>= 2^52），没有小数部分
    if (x >= 4503599627370496.0) { // 2^52
        *iptr = x;
        return 0.0;
    }
    long long int_part = (long long)x;   // 向零取整
    *iptr = (double)int_part;
    return x - *iptr;
}

#endif