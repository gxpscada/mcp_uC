#ifndef DEVICE_ASCII_H
#define DEVICE_ASCII_H

#include "device_acquisition.h" // Подключаем определение структуры кадра измерений device_frame_t и тип size_t

size_t device_ascii_format(const device_frame_t *, char *, size_t); // Прототип функции вывода сырых кодов АЦП первых 4 каналов в текстовую строку с переводом строки CRLF

#endif
