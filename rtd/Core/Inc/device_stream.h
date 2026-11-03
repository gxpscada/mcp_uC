#ifndef DEVICE_STREAM_H
#define DEVICE_STREAM_H

#include "device_ascii.h"

#define DEVICE_STREAM_DEPTH 16u // Емкость кольцевой очереди в строках (максимум 16 ожидающих отправки кадров)
#define DEVICE_LINE_SIZE 32u // Максимальный размер буфера одной текстовой строки в байтах (включая символы перевода строки и '\0')

// Структура кольцевой очереди диагностических ASCII-строк
typedef struct {
    char lines[DEVICE_STREAM_DEPTH][DEVICE_LINE_SIZE]; // Двумерный массив символов для хранения отформатированных строк в очереди
    uint16_t lengths[DEVICE_STREAM_DEPTH]; // Массив фактических длин каждой строки в байтах (без учёта нуль-терминатора)
    unsigned head; // Индекс головной (самой старой) строки в кольцевом буфере, ожидающей отправки на хост
    unsigned count; // Текущее количество заполненных строк, находящихся в очереди
    uint32_t dropped; // Диагностический счётчик кадров, отброшенных из-за переполнения очереди
    uint32_t accepted; // Диагностический счётчик строк, успешно переданных в драйвер отправки
} device_stream_t; // Имя типа структуры строковой очереди

void device_stream_push(device_stream_t *, const device_frame_t *); // Прототип функции форматирования нового кадра и добавления строки в хвост очереди
void device_stream_poll(device_stream_t *, void *, int (*send)(void *, const uint8_t *, size_t)); // Прототип функции неблокирующей выгрузки головной строки в передатчик

#endif
