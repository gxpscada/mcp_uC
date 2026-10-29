/**
 * @file max31865_driver.h
 * @brief Переносимый драйвер одного MAX31865: SPI, управление током RTD и диагностика.
 */
#ifndef MAX31865_DRIVER_H
#define MAX31865_DRIVER_H
#include <stdint.h>
#include <stddef.h>
/* One context per chip. transfer holds CS for the entire transaction,
 * releases CS on every exit and returns zero on success. No HAL dependency. */
typedef int (*max31865_transfer_fn)(void *, const uint8_t *, uint8_t *, size_t);
/**
 * @brief Контекст одной микросхемы. context принадлежит приложению; transfer выполняет полный
 * SPI-обмен. config хранит проверенную конфигурацию, а после неудачного STOP — желаемое состояние
 * OFF.
 */
typedef struct {
    void *context;
    max31865_transfer_fn transfer;
    uint8_t config;
} max31865_t;
/**
 * @brief Результат операции драйвера: OK — успех; IO_ERROR — отказ транспорта; BAD_ARGUMENT —
 * неверный вызов; CONFIG_ERROR — записанная конфигурация не совпала с прочитанной.
 */
typedef enum {
    MAX31865_OK = 0, MAX31865_IO_ERROR, MAX31865_BAD_ARGUMENT,
    MAX31865_CONFIG_ERROR
} max31865_result_t;
enum {
    MAX31865_BIAS = 0x80, MAX31865_AUTO = 0x40,
    MAX31865_THREE_WIRE = 0x10, MAX31865_FILTER_50HZ = 0x01,
    MAX31865_FAULT_HIGH = 0x80, MAX31865_FAULT_LOW = 0x40,
    MAX31865_FAULT_REFIN_HIGH = 0x20, MAX31865_FAULT_REFIN_LOW = 0x10,
    MAX31865_FAULT_RTDIN_LOW = 0x08, MAX31865_FAULT_VOLTAGE = 0x04
};
/**
 * @brief Результат чтения без фильтрации: raw — 15 бит ADC, rtd_fault — младший флаг RTD-регистра,
 * faults — биты 7..2 регистра неисправностей. Нулевой raw сам по себе не доказывает исправность.
 */
typedef struct {
    uint16_t raw;
    uint8_t rtd_fault;
    uint8_t faults;
} max31865_sample_t;
/* Init leaves bias and conversion OFF. Call bias(), wait for analog settling,
 * then start(). stop() turns off BOTH conversion and bias. */
max31865_result_t max31865_init(max31865_t *, void *, max31865_transfer_fn,
                               unsigned wires, unsigned filter_hz);
max31865_result_t max31865_start(max31865_t *);
max31865_result_t max31865_bias(max31865_t *);
max31865_result_t max31865_stop(max31865_t *);
max31865_result_t max31865_read(max31865_t *, max31865_sample_t *);
/* Faults remain latched until explicitly acknowledged. */
max31865_result_t max31865_clear_faults(max31865_t *);
#endif
