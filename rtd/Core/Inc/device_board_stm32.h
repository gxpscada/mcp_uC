#ifndef DEVICE_BOARD_STM32_H
#define DEVICE_BOARD_STM32_H

#include "stm32f4xx_hal.h"
#include "device_config.h" // Пользовательские настройки по умолчанию
#include "device_board.h"

#ifndef DEVICE_BOARD_CHANNELS // Если таблица распиновки каналов не переопределена извне
// Макрос инициализации массива из 8 структур привязки каналов T0..T7: {дескриптор SPI, порт CS, порт DRDY, пин CS, пин DRDY, таймаут SPI в мс}
#define DEVICE_BOARD_CHANNELS(spi_handle) { \
    {spi_handle, GPIOA, GPIOB, GPIO_PIN_3, GPIO_PIN_10, 2}, /* T0: CS=PA3, DRDY=PB10 */ \
    {spi_handle, GPIOB, GPIOB, GPIO_PIN_0, GPIO_PIN_12, 2}, /* T1: CS=PB0, DRDY=PB12 */ \
    {spi_handle, GPIOB, GPIOB, GPIO_PIN_1, GPIO_PIN_15, 2}, /* T2: CS=PB1, DRDY=PB15 */ \
    {spi_handle, GPIOA, GPIOA, GPIO_PIN_2, GPIO_PIN_8, 2}, /* T3: CS=PA2, DRDY=PA8 */ \
    {spi_handle, GPIOB, GPIOB, GPIO_PIN_2, GPIO_PIN_4, 2}, /* T4: CS=PB2, DRDY=PB4 */ \
    {spi_handle, GPIOB, GPIOB, GPIO_PIN_11, GPIO_PIN_5, 2}, /* T5: CS=PB11, DRDY=PB5 */ \
    {spi_handle, GPIOA, GPIOA, GPIO_PIN_0, GPIO_PIN_9, 2}, /* T6: CS=PA0, DRDY=PA9 */ \
    {spi_handle, GPIOA, GPIOB, GPIO_PIN_1, GPIO_PIN_6, 2} } /* T7: CS=PA1, DRDY=PB6 */
#endif // Конец определения таблицы каналов

#ifndef DEVICE_BOARD_GPIO_CLOCK_ENABLE // Если макрос включения тактирования портов датчиков не задан извне
// Макрос подачи тактирования на порты GPIOA и GPIOB в шине AHB1 (конструкция do-while(0) гарантирует безопасную подстановку в условия if/else)
#define DEVICE_BOARD_GPIO_CLOCK_ENABLE() do { __HAL_RCC_GPIOA_CLK_ENABLE(); __HAL_RCC_GPIOB_CLK_ENABLE(); } while (0)
#endif // Конец макроса тактирования GPIO

#ifndef DEVICE_SPI_INSTANCE // Блок определения параметров общей шины SPI для опроса всех MAX31865
#define DEVICE_SPI_INSTANCE SPI1 // Используем аппаратный блок приёмопередатчика SPI1
#define DEVICE_SPI_CLOCK_ENABLE() __HAL_RCC_SPI1_CLK_ENABLE() // Макрос подачи тактовой частоты на периферию SPI1 (шина APB2)
#define DEVICE_SPI_CLOCK_DISABLE() __HAL_RCC_SPI1_CLK_DISABLE() // Макрос отключения тактирования блока SPI1 при деинициализации
#define DEVICE_SPI_GPIO_CLOCK_ENABLE() __HAL_RCC_GPIOA_CLK_ENABLE() // Макрос включения тактирования порта GPIOA, на котором находятся линии SPI1
#define DEVICE_SPI_GPIO_PORT GPIOA // Порт выводов интерфейса SPI1 (GPIOA)
#define DEVICE_SPI_GPIO_PINS (GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7) // Линии шины SPI1: PA5 — SCK, PA6 — MISO, PA7 — MOSI
#define DEVICE_SPI_GPIO_AF GPIO_AF5_SPI1 // Номер альтернативной функции мультиплексора выводов (AF5 соответствует SPI1)
#endif // Конец блока конфигурации SPI

#ifndef DEVICE_PLL_M // Выбор профиля тактирования PLL и делителей шин в зависимости от модели микроконтроллера
#if defined(STM32F407xx) // Профиль для микроконтроллера STM32F407 (кварц HSE = 8 МГц, ядро SYSCLK = 168 МГц)
#define DEVICE_PLL_M 8u // Делитель входа PLL M = 8 (8 МГц / 8 = 1 МГц опорной частоты на входе ФАПЧ)
#define DEVICE_PLL_N 336u // Множитель частоты ГУН N = 336 (1 МГц * 336 = 336 МГц внутри VCO; при PLLP = 2 даёт 168 МГц на ядро)
#define DEVICE_PLL_Q 7u // Делитель тактирования USB OTG FS Q = 7 (336 МГц / 7 = 48.0 МГц — точная частота для USB)
#define DEVICE_APB1_DIV RCC_HCLK_DIV4 // Делитель шины APB1 = 4 (168 МГц / 4 = 42 МГц, в пределах максимума 42 МГц для F407)
#define DEVICE_SPI_PRESCALER SPI_BAUDRATEPRESCALER_32 // Делитель частоты SPI1 от шины APB2 (84 МГц / 32 = 2.625 МГц на линии SCK)
#elif defined(STM32F411xE) // Профиль для микроконтроллера STM32F411CEUx (кварц HSE = 25 МГц, ядро SYSCLK = 96 МГц)
#define DEVICE_PLL_M 25u // Делитель входа PLL M = 25 (25 МГц / 25 = 1 МГц опорной частоты на входе ФАПЧ)
#define DEVICE_PLL_N 192u // Множитель частоты ГУН N = 192 (1 МГц * 192 = 192 МГц внутри VCO; при PLLP = 2 даёт 96 МГц на ядро)
#define DEVICE_PLL_Q 4u // Делитель тактирования USB OTG FS Q = 4 (192 МГц / 4 = 48.0 МГц для работы USB CDC)
#define DEVICE_APB1_DIV RCC_HCLK_DIV2 // Делитель шины APB1 = 2 (96 МГц / 2 = 48 МГц, укладывается в лимит 50 МГц для F411)
#define DEVICE_SPI_PRESCALER SPI_BAUDRATEPRESCALER_16 // Делитель частоты SPI1 от шины APB2 (48 МГц / 16 = 3.0 МГц на линии SCK, запас до лимита 5 МГц у MAX31865)
#else // Если целевой микроконтроллер не относится ни к F407, ни к F411
#error Select_a_supported_STM32_target_and_board_clock_profile // Остановка компиляции с требованием явно задать профиль тактирования
#endif // Конец выбора МК
#endif // Конец блока настройки тактирования PLL

#ifndef DEVICE_RTC_USE_LSE // Выбор источника тактирования часов реального времени RTC
#define DEVICE_RTC_USE_LSE 0 // 0 — использовать внутренний RC-генератор LSI (~32 кГц); 1 — использовать внешний часовой кварц LSE 32768 Гц
#endif // Конец выбора генератора RTC

#ifndef DEVICE_FLASH_BASE // Параметры выделенного сектора встроенной Flash-памяти для ведения журнала настроек
#define DEVICE_FLASH_BASE 0x08060000u // Физический базовый адрес Сектора 7 во Flash STM32F411CE / F407 (смещение 384 КБ от 0x08000000)
#define DEVICE_FLASH_SIZE 0x20000u // Размер выделенного сектора Flash в байтах (0x20000 = 131072 байта = 128 КБ)
#endif // Конец определения параметров сектора Flash

#ifndef DEVICE_BIAS_SETTLE_MS // Время ожидания заряда аналоговых входных цепей датчиков
#define DEVICE_BIAS_SETTLE_MS 10u // Выдержка в миллисекундах (10 мс) после включения тока смещения Vbias перед запуском режима AUTO
#endif // Конец определения задержки BIAS

#ifndef DEVICE_FRAME_TIMEOUT_MS // Защитный интервал ожидания готовности всех каналов
#define DEVICE_FRAME_TIMEOUT_MS 100u // Таймаут сборки кадра (100 мс), предотвращающий зависание опроса при отказе линии DRDY одного из датчиков
#endif // Конец определения таймаута кадра

#ifndef DEVICE_TRIGGER_MODE // Режим синхронизации сбора кадра по умолчанию
#define DEVICE_TRIGGER_MODE DEVICE_WAIT_ALL // Штатный режим: дожидаемся активного уровня DRDY от всех включённых каналов
#endif // Конец определения режима синхронизации

#endif
