#include <drivers/rtc.h>
#include <arch/x86_64/io.h>

#define CMOS_ADDRESS 0x70
#define CMOS_DATA    0x71

static int get_update_in_progress_flag(void) {
    outb(CMOS_ADDRESS, 0x0A);
    return (inb(CMOS_DATA) & 0x80);
}

static uint8_t get_rtc_register(int reg) {
    outb(CMOS_ADDRESS, reg);
    return inb(CMOS_DATA);
}

static uint8_t bcd_to_bin(uint8_t val) {
    return ((val >> 4) * 10) + (val & 0x0F);
}

void rtc_get_time(rtc_time_t* t) {
    while (get_update_in_progress_flag());

    uint8_t second = get_rtc_register(0x00);
    uint8_t minute = get_rtc_register(0x02);
    uint8_t hour   = get_rtc_register(0x04);
    uint8_t day    = get_rtc_register(0x07);
    uint8_t month  = get_rtc_register(0x08);
    uint8_t year   = get_rtc_register(0x09);
    uint8_t registerB = get_rtc_register(0x0B);

    // Converte da BCD se necessario (se il bit 2 del registro B è 0)
    if (!(registerB & 0x04)) {
        second = bcd_to_bin(second);
        minute = bcd_to_bin(minute);
        hour = bcd_to_bin(hour & 0x7F) | (hour & 0x80);
        day = bcd_to_bin(day);
        month = bcd_to_bin(month);
        year = bcd_to_bin(year);
    }

    // Modalità 12 ore / 24 ore
    if (!(registerB & 0x02) && (hour & 0x80)) {
        hour = ((hour & 0x7F) + 12) % 24;
    }

    t->second = second;
    t->minute = minute;
    t->hour = hour;
    t->day = day;
    t->month = month;
    t->year = 2000 + year;
}
