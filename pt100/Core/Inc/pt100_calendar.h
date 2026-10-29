/**
 * @file pt100_calendar.h
 * @brief Преобразование Unix UTC и календаря RTC в пределах 2000..2099 годов.
 */
#ifndef PT100_CALENDAR_H
#define PT100_CALENDAR_H
#include <stdint.h>
/**
 * @brief Календарь UTC: year 2000..2099, month 1..12, day по длине месяца, hour 0..23,
 * minute/second 0..59, weekday 1..7 (понедельник..воскресенье). Часовой пояс устройства не
 * хранится.
 */
typedef struct { uint16_t year; uint8_t month, day, hour, minute, second, weekday; } pt100_calendar_t;
int pt100_epoch_to_calendar(uint32_t, pt100_calendar_t *);
int pt100_calendar_to_epoch(const pt100_calendar_t *, uint32_t *);
#endif
