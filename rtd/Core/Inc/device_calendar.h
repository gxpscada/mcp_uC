#ifndef DEVICE_CALENDAR_H
#define DEVICE_CALENDAR_H

#include <stdint.h>

// Структура разобранного календарного времени UTC для обмена с регистрами аппаратного RTC
typedef struct {
    uint16_t year; // Полный четырехзначный номер года (поддерживается диапазон 2000..2099)
    uint8_t month; // Номер месяца в году (1 — январь .. 12 — декабрь)
    uint8_t day; // Число месяца (от 1 до 28/29/30/31 в зависимости от месяца и високосности года)
    uint8_t hour; // Часы в 24-часовом формате (0..23)
    uint8_t minute; // Минуты (0..59)
    uint8_t second; // Секунды (0..59)
    uint8_t weekday; // День недели по стандарту ISO-8601 и аппаратуре STM32 RTC (1 — понедельник .. 7 — воскресенье)
} device_calendar_t; // Имя типа календарной структуры

int device_epoch_to_calendar(uint32_t, device_calendar_t *); // Прототип функции перевода секунд Unix Epoch (с 01.01.1970) в календарную дату/время
int device_calendar_to_epoch(const device_calendar_t *, uint32_t *); // Прототип функции проверки календарной даты и её перевода обратно в секунды Unix Epoch

#endif
