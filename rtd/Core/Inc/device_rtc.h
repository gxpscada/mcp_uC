#ifndef DEVICE_RTC_H
#define DEVICE_RTC_H

#include <stdint.h>

void device_rtc_init(void); // Прототип функции неблокирующего запуска тактирования Backup-домена и часового генератора LSE/LSI
void device_rtc_poll(void); // Прототип функции шага конечного автомата инициализации и записи регистров календаря RTC
int device_rtc_set(uint32_t epoch); // Прототип функции запроса установки нового времени в секундах Unix UTC (диапазон 2000..2099 гг.)
int device_rtc_get(uint32_t *epoch, uint16_t *milliseconds); // Прототип функции когерентного чтения текущего времени UTC и миллисекунд из регистров TR/DR/SSR
int device_rtc_busy(void); // Прототип функции проверки готовности RTC (возвращает 0 только когда часы находятся в рабочем состоянии READY)
extern volatile uint8_t device_rtc_error; // Глобальный флаг аппаратного отказа RTC (таймаут запуска кварца или входа в режим INIT)

#endif
