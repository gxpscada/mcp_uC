/**
 * @file pt100_stream.h
 * @brief Ограниченная очередь старого ASCII-потока, сохранённая для прежних тестов.
 */
#ifndef PT100_STREAM_H
#define PT100_STREAM_H
#include "pt100_ascii.h"
#define PT100_STREAM_DEPTH 16u
#define PT100_LINE_SIZE 32u
/* send returns zero only after copying/consuming the data. Busy/disconnected
 * must return nonzero. Single main-loop producer/consumer, no ISR access. */
/**
 * @brief Очередь из 16 строк прежнего диагностического режима. head — начало, count — число строк;
 * lengths не включает NUL. dropped считает отброшенные кадры, accepted — принятые передатчиком
 * строки. Обнулить до первого использования.
 */
typedef struct {
    char lines[PT100_STREAM_DEPTH][PT100_LINE_SIZE];
    uint16_t lengths[PT100_STREAM_DEPTH];
    unsigned head, count;
    uint32_t dropped, accepted;
} pt100_stream_t;
void pt100_stream_push(pt100_stream_t *, const pt100_frame_t *);
void pt100_stream_poll(pt100_stream_t *, void *, int (*send)(void *, const uint8_t *, size_t));
#endif
