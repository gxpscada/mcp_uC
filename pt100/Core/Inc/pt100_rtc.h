/**
 * @file pt100_rtc.h
 * @brief Пошаговая работа аппаратного RTC STM32F4 и получение метки UTC.
 */
#ifndef PT100_RTC_H
#define PT100_RTC_H
#include <stdint.h>
void pt100_rtc_init(void);
void pt100_rtc_poll(void);
int pt100_rtc_set(uint32_t epoch);
int pt100_rtc_get(uint32_t *epoch, uint16_t *milliseconds);
int pt100_rtc_busy(void);
extern volatile uint8_t pt100_rtc_error;
#endif
