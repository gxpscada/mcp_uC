/**
 * @file pt100_store.h
 * @brief Переносимый журнал конфигурации с CRC и последним маркером commit.
 */
#ifndef PT100_STORE_H
#define PT100_STORE_H
#include "pt100_config.h"
#include <stddef.h>
#include <stdint.h>
#define PT100_STORE_WORDS 20u
/**
 * @brief Состояние журнала. memory — отображённая Flash, word_count и next_slot измеряются в
 * 32-битных словах. record — снимок новой записи. state: 0 — нет операции, 1 — запуск слова, 2 —
 * ожидание; error сообщает отказ. begin_word возвращает 0 при принятии, poll_word: 0 busy, 1
 * готово, -1 ошибка.
 */
typedef struct {
    const volatile uint32_t *memory;
    size_t word_count, next_slot;
    uint32_t record[PT100_STORE_WORDS];
    uint8_t state, index, error;
    void *context;
    int (*begin_word)(void *,size_t,uint32_t);
    int (*poll_word)(void *); /* 0 busy, 1 complete, -1 failed */
} pt100_store_t;
/* Append-only journal. Commit written last. Never erases previous settings. */
int pt100_store_load(pt100_store_t *, pt100_config_t *);
int pt100_store_save(pt100_store_t *, const pt100_config_t *);
void pt100_store_poll(pt100_store_t *);
#endif
