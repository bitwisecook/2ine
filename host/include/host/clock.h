/* Public domain. No rights reserved. Wholly AI-authored. */
/* Host monotonic and wall clocks, sleeping and calendar conversion. */
#ifndef TWOINE_HOST_CLOCK_H
#define TWOINE_HOST_CLOCK_H
#include <stdint.h>

uint64_t lx_host_monotonic_us(void);
void lx_host_sleep_ms(uint32_t milliseconds);

typedef struct LxHostCalendar
{
    int year, month, day, weekday, hour, minute, second, utc_offset_minutes;
} LxHostCalendar;

uint64_t lx_host_realtime_us(void);
int lx_host_calendar_seconds(const LxHostCalendar *calendar, int utc, int64_t *seconds);
int lx_host_calendar(int64_t seconds, int utc, LxHostCalendar *result);
/* Abbreviated names of the host zone's standard and daylight time. */
void lx_host_zone_names(char standard[32], char daylight[32]);
#endif
