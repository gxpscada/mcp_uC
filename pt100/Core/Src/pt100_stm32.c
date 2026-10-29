/**
 * @file pt100_stm32.c
 * @brief Адаптеры SPI/GPIO/DWT STM32 HAL и публичные точки входа приложения.
 */
#include "pt100_stm32.h"
/**
 * @brief Выполняет одну синхронную SPI-транзакцию с управлением CS через STM32 HAL.
 * @param[in] context Указатель на pt100_stm32_channel_t конкретного датчика.
 * @param[in] tx Передаваемые байты, включая адрес регистра.
 * @param[out] rx Буфер минимум n байтов для ответа.
 * @param[in] n Длина обмена, не более 65535 байтов.
 * @return 0 при HAL_OK, -1 при ошибке аргументов или обмена.
 * @details CS опускается перед обменом и поднимается после возврата HAL, включая ошибку.
 * Время ожидания ограничивается spi_timeout_ms. SPI-шина должна использоваться
 * последовательно; арбитраж между потоками/прерываниями здесь не реализован.
 */
int pt100_stm32_transfer(void *context, const uint8_t *tx, uint8_t *rx, size_t n)
{
    pt100_stm32_channel_t *p = context;
    HAL_StatusTypeDef status;
    if (!p || !p->spi || n > 65535u) return -1;
    HAL_GPIO_WritePin(p->cs_port, p->cs_pin, GPIO_PIN_RESET);
    status = HAL_SPI_TransmitReceive(p->spi, (uint8_t *)tx, rx, (uint16_t)n,
                                    p->spi_timeout_ms);
    HAL_GPIO_WritePin(p->cs_port, p->cs_pin, GPIO_PIN_SET);
    return status == HAL_OK ? 0 : -1;
}
/**
 * @brief Преобразует активный низкий уровень DRDY в логический признак готовности.
 * @param[in] context Ненулевой указатель на настроенный pt100_stm32_channel_t.
 * @return 1 при низком уровне DRDY, иначе 0.
 * @note Это чтение текущего уровня GPIO, а не захват аппаратной метки фронта.
 */
int pt100_stm32_ready(void *context)
{
    pt100_stm32_channel_t *p = context;
    return HAL_GPIO_ReadPin(p->drdy_port, p->drdy_pin) == GPIO_PIN_RESET;
}
/**
 * @brief Возвращает счётчик процессорных тактов DWT для измерения длительностей.
 * @param[in] unused Не используется; допускается NULL.
 * @return Текущее 32-битное значение DWT->CYCCNT с естественным переполнением.
 * @pre DWT включён при pt100_app_init(). Пересчёт в секунды использует pt100_tick_hz.
 * @note Это монотонный счётчик интервалов, не календарное время RTC.
 */
uint32_t pt100_stm32_ticks(void *unused)
{
    (void)unused;
    return DWT->CYCCNT;
}
