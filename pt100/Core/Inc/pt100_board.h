/**
 * @file pt100_board.h
 * @brief Распиновка, тактирование и аппаратные параметры переноса на другую плату.
 */
#ifndef PT100_BOARD_H
#define PT100_BOARD_H
#include "stm32f4xx_hal.h"
#include "pt100_config.h"
/* Default pin map is initialized by pt100_board_gpio_init(), not CubeMX.
 * Override groups in PT100_CUSTOM_CONFIG_HEADER for a different board. */
/**
 * @brief Таблица физических подключений MAX31865 в порядке каналов T0..T7.
 * @param spi_handle Указатель на HAL-дескриптор общей SPI-шины.
 * @details Строка содержит SPI, порт CS, порт DRDY, маску CS, маску DRDY и
 * тайм-аут SPI в миллисекундах. CS активен низким уровнем, DRDY также активен
 * низким уровнем. Настраиваются только первые PT100_BOARD_CHANNEL_COUNT строк.
 * При замене таблицы проверьте конфликты с USB, кварцем, SWD и другими периферийными
 * функциями конкретной платы; оставшиеся строки не включаются автоматически.
 */
#ifndef PT100_BOARD_CHANNELS
#define PT100_BOARD_CHANNELS(spi_handle) { \
    {spi_handle, GPIOA, GPIOA, GPIO_PIN_3, GPIO_PIN_2, 2}, \
    {spi_handle, GPIOB, GPIOB, GPIO_PIN_0, GPIO_PIN_12, 2}, \
    {spi_handle, GPIOB, GPIOB, GPIO_PIN_1, GPIO_PIN_13, 2}, \
    {spi_handle, GPIOB, GPIOB, GPIO_PIN_10, GPIO_PIN_14, 2}, \
    {spi_handle, GPIOB, GPIOB, GPIO_PIN_2, GPIO_PIN_15, 2}, \
    {spi_handle, GPIOB, GPIOA, GPIO_PIN_11, GPIO_PIN_8, 2}, \
    {spi_handle, GPIOA, GPIOA, GPIO_PIN_0, GPIO_PIN_9, 2}, \
    {spi_handle, GPIOA, GPIOA, GPIO_PIN_1, GPIO_PIN_10, 2} }
#endif
#ifndef PT100_BOARD_GPIO_CLOCK_ENABLE
#define PT100_BOARD_GPIO_CLOCK_ENABLE() do { __HAL_RCC_GPIOA_CLK_ENABLE(); __HAL_RCC_GPIOB_CLK_ENABLE(); } while (0)
#endif
/**
 * @name Настройка общей SPI-шины
 * Если переопределён PT100_SPI_INSTANCE, нужно определить всю группу SPI-макросов:
 * тактирование периферии, тактирование GPIO, порт, маски выводов и номер AF.
 * Выбранная скорость определяется также PT100_SPI_PRESCALER в профиле частот.
 * @{
 */
#ifndef PT100_SPI_INSTANCE
#define PT100_SPI_INSTANCE SPI1
#define PT100_SPI_CLOCK_ENABLE() __HAL_RCC_SPI1_CLK_ENABLE()
#define PT100_SPI_CLOCK_DISABLE() __HAL_RCC_SPI1_CLK_DISABLE()
#define PT100_SPI_GPIO_CLOCK_ENABLE() __HAL_RCC_GPIOA_CLK_ENABLE()
#define PT100_SPI_GPIO_PORT GPIOA
#define PT100_SPI_GPIO_PINS (GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_7)
#define PT100_SPI_GPIO_AF GPIO_AF5_SPI1
#endif
/** @} */
/**
 * @name Профиль частот платы
 * F411 по умолчанию предполагает HSE 25 МГц и CPU 96 МГц; F407 — HSE 8 МГц
 * и CPU 168 МГц. В обоих профилях PLLQ формирует 48 МГц для USB.
 * Переопределение PT100_PLL_M отключает всю штатную группу: пользователь должен
 * задать также PLL_N, PLL_Q, APB1_DIV и SPI_PRESCALER, согласовав HSE_VALUE.
 * Нельзя выбирать профиль только по названию MCU, не проверив кварц платы.
 * @{
 */
#ifndef PT100_PLL_M
#if defined(STM32F407xx)
#define PT100_PLL_M 8u
#define PT100_PLL_N 336u
#define PT100_PLL_Q 7u
#define PT100_APB1_DIV RCC_HCLK_DIV4
#define PT100_SPI_PRESCALER SPI_BAUDRATEPRESCALER_32
#elif defined(STM32F411xE)
#define PT100_PLL_M 25u
#define PT100_PLL_N 192u
#define PT100_PLL_Q 4u
#define PT100_APB1_DIV RCC_HCLK_DIV2
#define PT100_SPI_PRESCALER SPI_BAUDRATEPRESCALER_16
#else
#error Select_a_supported_STM32_target_and_board_clock_profile
#endif
#endif /* custom clock profile must provide all five clock/SPI macros */
/** @} */
#ifndef PT100_RTC_USE_LSE
#define PT100_RTC_USE_LSE 0 /**< 0: внутренний LSI; 1: внешний LSE 32768 Гц, который должен быть установлен. */
#endif
/**
 * @name Область постоянных настроек
 * Область должна быть зарезервирована linker-скриптом и не пересекаться с кодом.
 * Стандартное размещение — сектор 7, 128 КиБ, начиная с 0x08060000.
 * При переопределении BASE требуется задать также SIZE; автоматического стирания нет.
 * @{
 */
#ifndef PT100_FLASH_BASE
#define PT100_FLASH_BASE 0x08060000u
#define PT100_FLASH_SIZE 0x20000u
#endif
/** @} */
#ifndef PT100_BIAS_SETTLE_MS
#define PT100_BIAS_SETTLE_MS 10u /**< Пауза в мс после включения BIAS всех каналов, перед запуском AUTO. */
#endif
#ifndef PT100_FRAME_TIMEOUT_MS
#define PT100_FRAME_TIMEOUT_MS 100u /**< Максимальное ожидание нового кадра в мс, включая отказавший DRDY. */
#endif
#ifndef PT100_TRIGGER_MODE
#define PT100_TRIGGER_MODE PT100_WAIT_ALL /**< Штатно ждём все каналы; TRIGGER_FIRST предназначен для экспериментов. */
#endif
#endif
