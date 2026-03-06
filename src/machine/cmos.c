#include <stdint.h>
#include "lib/io.h"
#include "lib/atomic.h"
#include "const.h"

static uint8_t cmos_read_register(uint8_t reg) {
    io_outbyte(0x70, 0x80 | reg);
    io_delay();
    return io_inbyte(0x71);
}

UNUSED static void cmos_write_register(uint8_t reg, uint8_t value) {
    io_outbyte(0x70, 0x80 | reg);
    io_delay();
    io_outbyte(0x71, value);
}

static inline int get_update_in_progress_flag(void) {
    return cmos_read_register(0x0A) & 0x80;   // 返回位7（UIP）
}

static uint8_t bcd_to_binary(uint8_t bcd) {
    return (bcd & 0x0F) + ((bcd >> 4) * 10);
}

// 定义时间结构
typedef struct rtc_time {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint16_t year;
    uint8_t weekday;
} rtc_time_t;

static rtc_time_t time;
atomic_64_t unix_time;

static void cmos_read_rtc(rtc_time_t *time) {
    uint8_t intr = io_cli();
    uint8_t second, minute, hour, day, month, year, weekday;
    uint8_t last_second, last_minute, last_hour, last_day, last_month, last_year;
    uint8_t registerB;

    while (get_update_in_progress_flag());
    second   = cmos_read_register(0x00);
    minute   = cmos_read_register(0x02);
    hour     = cmos_read_register(0x04);
    day      = cmos_read_register(0x07);
    month    = cmos_read_register(0x08);
    year     = cmos_read_register(0x09);
    weekday  = cmos_read_register(0x06);

    do {
        last_second   = second;
        last_minute   = minute;
        last_hour     = hour;
        last_day      = day;
        last_month    = month;
        last_year     = year;

        while (get_update_in_progress_flag());
        second   = cmos_read_register(0x00);
        minute   = cmos_read_register(0x02);
        hour     = cmos_read_register(0x04);
        day      = cmos_read_register(0x07);
        month    = cmos_read_register(0x08);
        year     = cmos_read_register(0x09);
        weekday  = cmos_read_register(0x06);
    } while (last_second   != second   ||
             last_minute   != minute   ||
             last_hour     != hour     ||
             last_day      != day      ||
             last_month    != month    ||
             last_year     != year);
    registerB = cmos_read_register(0x0B);

    if (!(registerB & 0x04)) {
        second   = bcd_to_binary(second);
        minute   = bcd_to_binary(minute);
        day      = bcd_to_binary(day);
        month    = bcd_to_binary(month);
        year     = bcd_to_binary(year);
        weekday  = bcd_to_binary(weekday);
        uint8_t pm = hour & 0x80;
        hour = ( (hour & 0x0F) + (((hour & 0x70) >> 4) * 10) ) | pm;
    }
    
    if (!(registerB & 0x02) && (hour & 0x80)) {
        hour = ((hour & 0x7F) + 12) % 24;
    } else {
        hour &= 0x7F;
    }

    year += 2000;
    time->second  = second;
    time->minute  = minute;
    time->hour    = hour;
    time->day     = day;
    time->month   = month;
    time->year    = year;
    time->weekday = weekday;
    io_set_intr(intr);
}

static int is_leap_year(uint16_t year) {
    return (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
}

static const uint8_t month_days[12] = {
    31, 28, 31, 30, 31, 30,
    31, 31, 30, 31, 30, 31
};

static void rtc_time_to_timestamp(const rtc_time_t *tm) {
    uint64_t days = 0;
    uint16_t year;
    uint8_t month;
    for (year = 1970; year < tm->year; year++) {
        days += is_leap_year(year) ? 366 : 365;
    }
    for (month = 1; month < tm->month; month++) {
        days += month_days[month - 1];
        if (month == 2 && is_leap_year(tm->year)) {
            days += 1;
        }
    }
    days += tm->day - 1;
    uint64_t seconds = days * 86400ULL;
    seconds += tm->hour * 3600ULL;
    seconds += tm->minute * 60ULL;
    seconds += tm->second;

    atomic_64_set(&unix_time,seconds);
}

void real_time_init(void){
    cmos_read_rtc(&time);
    rtc_time_to_timestamp(&time);
}
