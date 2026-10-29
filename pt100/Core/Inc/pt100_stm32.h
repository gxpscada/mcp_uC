/**
 * @file pt100_stm32.h
 * @brief Адаптеры SPI/GPIO/DWT STM32 HAL и публичные точки входа приложения.
 */
#ifndef PT100_STM32_H
#define PT100_STM32_H
#include "stm32f4xx_hal.h"
#include "pt100_acquisition.h"
#include "pt100_control.h"
/**
 * @brief Распиновка и HAL-контекст отдельного MAX31865. SPI может быть общим; CS и DRDY задаются
 * для каждого канала. cs_pin/drdy_pin — маски GPIO_PIN_*, не номера. spi_timeout_ms ограничивает
 * синхронный обмен в миллисекундах.
 */
typedef struct {
    SPI_HandleTypeDef *spi;
    GPIO_TypeDef *cs_port, *drdy_port;
    uint16_t cs_pin, drdy_pin;
    uint32_t spi_timeout_ms;
} pt100_stm32_channel_t;
int pt100_stm32_transfer(void *, const uint8_t *, uint8_t *, size_t);
int pt100_stm32_ready(void *);
uint32_t pt100_stm32_ticks(void *);
void pt100_app_init(SPI_HandleTypeDef *);
void pt100_app_poll(void);
void pt100_board_gpio_init(void);
void pt100_app_usb_receive(const uint8_t *, size_t);
void pt100_app_usb_reset(void);
/* Debugger-visible diagnostics. Times are CPU cycles; divide by
 * pt100_tick_hz / 1e6 for microseconds. They are NOT RTC timestamps. */
extern pt100_acquisition_t pt100_acquisition;
extern pt100_frame_t pt100_last_frame;
extern pt100_control_t pt100_control;
extern modbus_rtu_t pt100_modbus;
extern uint32_t pt100_tick_hz;
extern max31865_result_t pt100_init_status[PT100_CHANNELS];
#endif
