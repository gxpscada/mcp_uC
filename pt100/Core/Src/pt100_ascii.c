/**
 * @file pt100_ascii.c
 * @brief Совместимость со старым четырёхканальным ASCII-форматом, вне текущего Modbus-тракта.
 */
#include "pt100_ascii.h"
#include <stdio.h>
/**
 * @brief Формирует диагностическую строку прежнего четырёхканального режима.
 * @param[in] f Кадр; используются только каналы 0..3.
 * @param[out] buffer Буфер текстовой строки с завершающим NUL.
 * @param[in] capacity Ёмкость буфера в байтах, включая место под NUL.
 * @return Длина строки без NUL либо 0 при неверных аргументах/нехватке места.
 * @details Четыре десятичных raw ADC разделены пробелами, строка заканчивается CRLF.
 * Недостоверный канал выводится как -1. При нехватке места буфер может содержать
 * обрезанную строку: передавать её нельзя. Модуль сохранён для тестов старого этапа,
 * в основном приложении с Modbus не используется.
 */
size_t pt100_ascii_format(const pt100_frame_t *f, char *buffer, size_t capacity)
{
    int v[4], length;
    unsigned i;
    if (!f || !buffer || !capacity) return 0;
    for (i = 0; i < 4; ++i)
        v[i] = f->channel[i].status == PT100_VALID ? (int)f->channel[i].sample.raw : -1;
    length = snprintf(buffer, capacity, "%d %d %d %d\r\n", v[0], v[1], v[2], v[3]);
    return length > 0 && (size_t)length < capacity ? (size_t)length : 0;
}
