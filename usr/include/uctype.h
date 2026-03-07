#ifndef OS_UCTYPE_H
#define OS_UCTYPE_H

// 判断字符是否为空白字符：空格、制表符、换行、回车、换页、垂直制表符
static inline int isspace(int c) {
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v');
}

// 判断字符是否为十进制数字
static inline int isdigit(int c) {
    return (c >= '0' && c <= '9');
}

// 判断字符是否为英文字母（大小写）
static inline int isalpha(int c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

// 判断字符是否为十六进制数字（0-9, a-f, A-F）
static inline int isxdigit(int c) {
    return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// 判断字符是否为大写字母
static inline int isupper(int c) {
    return (c >= 'A' && c <= 'Z');
}

// 判断字符是否为小写字母
static inline int islower(int c) {
    return (c >= 'a' && c <= 'z');
}

// 将大写字母转换为小写（非字母原样返回）
static inline int tolower(int c) {
    if (c >= 'A' && c <= 'Z')
        return c + ('a' - 'A');
    return c;
}

// 将小写字母转换为大写
static inline int toupper(int c) {
    if (c >= 'a' && c <= 'z')
        return c - ('a' - 'A');
    return c;
}

// 判断字符是否为字母或数字
static inline int isalnum(int c) {
    return isalpha(c) || isdigit(c);
}

// 判断字符是否为可打印字符（包括空格）
static inline int isprint(int c) {
    return c >= ' ' && c <= '~';
}

// 判断字符是否为控制字符
static inline int iscntrl(int c) {
    return (c >= 0 && c <= 31) || c == 127;
}

// 判断字符是否为图形字符（可打印且非空格）
static inline int isgraph(int c) {
    return c > ' ' && c <= '~';
}

// 判断字符是否为标点符号（可打印且非字母数字）
static inline int ispunct(int c) {
    return isgraph(c) && !isalnum(c);
}

#endif