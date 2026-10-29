/**
 * @file pt100_processing.h
 * @brief Фильтрация ADC, знаковая поправка и преобразование сопротивления в температуру.
 */
#ifndef PT100_PROCESSING_H
#define PT100_PROCESSING_H
#include <stdint.h>
/**
 * @brief История одного канала: кольца медианы и среднего, сумма среднего и индексы заполнения.
 * Перед использованием обнулить; между каналами не разделять. Размеры массивов рассчитаны на
 * максимальные окна 7 и 16.
 */
typedef struct {
    uint16_t median[7], average[16];
    uint32_t sum;
    uint8_t median_pos, median_count, average_pos, average_count;
} pt100_filter_t;
/**
 * @brief Параметры обработки одного канала. Окна задаются в отсчётах, use_* трактуются как
 * логические флаги. adc_offset — знаковая b в исходных отсчётах ADC; r0/rref — Ом; gain —
 * безразмерный коэффициент k для температуры.
 */
typedef struct {
    uint8_t median_window, average_window;
    uint8_t use_median, use_average, use_calibration;
    int16_t adc_offset; /* units of original 15-bit ADC, signed */
    float r0, rref, gain;
} pt100_processing_settings_t;
/**
 * @brief Результат обработки. adc_q1 — удвоенный отфильтрованный ADC до калибровки; corrected_q1 —
 * после b. resistance выражен в Омах, temperature — в °C. Поля могут быть частично заполнены при
 * ошибке; использовать температуру только при valid!=0.
 */
typedef struct {
    uint16_t adc_q1; /* filtered uncalibrated ADC * 2, range 0..65534 */
    uint16_t corrected_q1;
    float resistance, temperature;
    uint8_t valid;
} pt100_processed_t;
void pt100_filter_reset(pt100_filter_t *);
int pt100_filter_valid(const pt100_processing_settings_t *);
/* 0 = warming up, 1 = valid, -1 = invalid parameters/range. */
int pt100_process(pt100_filter_t *, const pt100_processing_settings_t *, uint16_t,
                  pt100_processed_t *);
int pt100_cvd_temperature(float resistance, float r0, float *temperature);
#endif
