/**
 * @file pt100_acquisition.h
 * @brief Сбор кадров от нескольких MAX31865 по DRDY с ограниченным ожиданием.
 */
#ifndef PT100_ACQUISITION_H
#define PT100_ACQUISITION_H
#include "max31865_driver.h"
#include "pt100_config.h"
#define PT100_CHANNELS PT100_MAX_CHANNELS
/**
 * @brief Условие выдачи кадра: WAIT_ALL ждёт готовности всех выбранных каналов; TRIGGER_FIRST ждёт
 * канал 0 и предназначен для эксперимента с возможным чтением устаревших данных остальных.
 */
typedef enum { PT100_WAIT_ALL, PT100_TRIGGER_FIRST } pt100_trigger_t;
enum { PT100_VALID = 0, PT100_NOT_READY = 1, PT100_BUS_ERROR = 2,
       PT100_SENSOR_FAULT = 4, PT100_INIT_ERROR = 8,
       PT100_FILTER_WARMUP = 16, PT100_RANGE_ERROR = 32, PT100_STOPPED = 64 };
/**
 * @brief Привязка логического канала к драйверу и callback готовности. ready возвращает ненулевое
 * значение при активном DRDY. enabled означает, что аппаратная инициализация и запуск этого канала
 * разрешили его чтение.
 */
typedef struct {
    max31865_t *device;
    void *ready_context;
    int (*ready)(void *);
    uint8_t enabled;
} pt100_channel_t;
/**
 * @brief Политика сбора кадра: trigger определяет условие готовности, timeout_ms ограничивает
 * ожидание в миллисекундах, channel_count выбирает первые 1..8 каналов таблицы.
 */
typedef struct {
    pt100_trigger_t trigger;
    uint32_t timeout_ms;
    uint8_t channel_count;
} pt100_settings_t;
/**
 * @brief Данные одного канала кадра. status — битовая маска PT100_*, ноль означает исправный
 * отсчёт. read_offset_ticks отсчитывается от начала чтения кадра; ready_offset_ticks — от первого
 * замеченного DRDY. Это наблюдения программного опроса, не аппаратный захват фронтов.
 */
typedef struct {
    max31865_sample_t sample;
    uint8_t status;
    uint32_t read_offset_ticks;
    uint32_t ready_offset_ticks; /* first observed DRDY, not hardware edge */
} pt100_channel_data_t;
/**
 * @brief Снимок до восьми каналов. sequence — номер кадра;
 * read_ticks/period_ticks/ready_span_ticks — длительности в тиках callback ticks. ready_mask
 * показывает готовность при формировании кадра. utc_seconds — Unix UTC, milliseconds — доля
 * секунды; валидность RTC задаёт rtc_valid.
 */
typedef struct {
    pt100_channel_data_t channel[PT100_CHANNELS];
    uint32_t sequence, read_ticks, period_ticks, ready_span_ticks;
    uint8_t ready_mask;
    uint32_t utc_seconds;
    uint16_t milliseconds;
    uint8_t rtc_valid;
} pt100_frame_t;
/**
 * @brief Долгоживущее состояние сборщика: каналы, callbacks, таймеры и статистика. ticks задаёт
 * монотонный 32-битный счётчик, timestamp возвращает 0 при успешном UTC-снимке. Счётчики
 * bus_errors/sensor_errors ведутся отдельно по каналам; timeout_frames считает кадры по тайм-ауту.
 */
typedef struct {
    pt100_channel_t channel[PT100_CHANNELS];
    pt100_settings_t settings;
    void *clock_context;
    uint32_t (*ticks)(void *);
    int (*timestamp)(uint32_t *, uint16_t *);
    uint32_t last_frame_ms, last_frame_ticks, sequence;
    uint32_t max_read_ticks, timeout_frames;
    uint32_t bus_errors[PT100_CHANNELS], sensor_errors[PT100_CHANNELS];
    uint32_t ready_ticks[PT100_CHANNELS], first_ready_ticks;
    uint8_t observed_mask;
} pt100_acquisition_t;
/* Zero-init, configure callbacks/settings, set last_frame_ms after startup.
 * Call in main loop, not ISR. Tick durations must be less than one wrap. */
int pt100_acquisition_poll(pt100_acquisition_t *, uint32_t now_ms, pt100_frame_t *);
#endif
