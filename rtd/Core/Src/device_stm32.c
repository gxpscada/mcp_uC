#include "device_stm32.h"


int device_stm32_transfer(void *context, const uint8_t *tx, uint8_t *rx, size_t n)
{
    // Приводим нетипизированный указатель контекста к структуре настроек конкретного канала
    device_stm32_channel_t *p = context;
    HAL_StatusTypeDef status; // Переменная для сохранения кода возврата драйвера STM32 HAL
    // Защита от пустых указателей и переполнения uint16_t (ограничение длины пакета в HAL_SPI)
    if (!p || !p->spi || n > 65535u) return -1;
    // Притягиваем линию CS выбранного датчика к нулю (активный уровень для начала транзакции)
    HAL_GPIO_WritePin(p->cs_port, p->cs_pin, GPIO_PIN_RESET);
    // Синхронно отправляем буфер tx и принимаем rx длины n с защитным таймаутом
    status = HAL_SPI_TransmitReceive(p->spi, (uint8_t *)tx, rx, (uint16_t)n, p->spi_timeout_ms);
    // Поднимаем линию CS обратно в 1 (завершаем транзакцию независимо от успешности обмена)
    HAL_GPIO_WritePin(p->cs_port, p->cs_pin, GPIO_PIN_SET);
    // Преобразуем платформенный статус HAL_OK в 0 (успех), а любую ошибку/таймаут — в -1
    return status == HAL_OK ? 0 : -1;
}

// Опрос линии готовности данных DRDY датчика MAX31865
int device_stm32_ready(void *context)
{
    device_stm32_channel_t *p = context; // Получаем доступ к структуре с портом и пином DRDY данного канала
    // Возвращаем 1, если на пине DRDY низкий уровень (преобразование АЦП завершено)
    return HAL_GPIO_ReadPin(p->drdy_port, p->drdy_pin) == GPIO_PIN_RESET;
}

// Чтение аппаратного 32-битного счётчика тактов ядра Cortex-M4 (блок DWT)
uint32_t device_stm32_ticks(void *unused)
{
    (void)unused; // Явно подавляем предупреждение компилятора о неиспользуемом аргументе контекста
    // Возвращаем текущее значение регистра счётчика тактов CPU для точного замера интервалов
    // времени
    return DWT->CYCCNT;
}
