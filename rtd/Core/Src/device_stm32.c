#include "device_stm32.h"

static volatile uint8_t drdy_pending[16];

void HAL_GPIO_EXTI_Callback(uint16_t pin)
{
    unsigned i;
    for (i = 0; i < 16; ++i)
        if (pin & (1u << i)) drdy_pending[i] = 1;
}

static unsigned drdy_index(uint16_t pin)
{
    unsigned i = 0;
    while (i < 15 && !(pin & (1u << i))) ++i;
    return i;
}

void device_stm32_drdy_reset(void)
{
    unsigned i;
    uint32_t state = __get_PRIMASK();
    __disable_irq();
    for (i = 0; i < 16; ++i) drdy_pending[i] = 0;
    __set_PRIMASK(state);
}


int device_stm32_transfer(void *context, const uint8_t *tx, uint8_t *rx, size_t n)
{
    // Приводим нетипизированный указатель контекста к структуре настроек конкретного канала
    device_stm32_channel_t *p = context;
    HAL_StatusTypeDef status; // Переменная для сохранения кода возврата драйвера STM32 HAL
    // Защита от пустых указателей и переполнения uint16_t (ограничение длины пакета в HAL_SPI)
    if (!p || !p->spi || n > 65535u) return -1;
    // Забираем событие только перед чтением RTD. Новое событие во время SPI сохраняется.
    if (n == 8 && tx && tx[0] == 1) {
        uint32_t state = __get_PRIMASK();
        __disable_irq();
        drdy_pending[drdy_index(p->drdy_pin)] = 0;
        __set_PRIMASK(state);
    }
    // Притягиваем линию CS выбранного датчика к нулю (активный уровень для начала транзакции)
    HAL_GPIO_WritePin(p->cs_port, p->cs_pin, GPIO_PIN_RESET);
    // Синхронно отправляем буфер tx и принимаем rx длины n с защитным таймаутом
    status = HAL_SPI_TransmitReceive(p->spi, (uint8_t *)tx, rx, (uint16_t)n, p->spi_timeout_ms);
    // Поднимаем линию CS обратно в 1 (завершаем транзакцию независимо от успешности обмена)
    HAL_GPIO_WritePin(p->cs_port, p->cs_pin, GPIO_PIN_SET);
    // Преобразуем платформенный статус HAL_OK в 0 (успех), а любую ошибку/таймаут — в -1
    return status == HAL_OK ? 0 : -1;
}

// Проверка события, защёлкнутого прерыванием DRDY.
int device_stm32_ready(void *context)
{
    device_stm32_channel_t *p = context; // Получаем доступ к структуре с портом и пином DRDY данного канала
    // GPIO здесь не читаем: готовность приходит только из EXTI.
    return drdy_pending[drdy_index(p->drdy_pin)] != 0;
}

// Чтение аппаратного 32-битного счётчика тактов ядра Cortex-M4 (блок DWT)
uint32_t device_stm32_ticks(void *unused)
{
    (void)unused; // Явно подавляем предупреждение компилятора о неиспользуемом аргументе контекста
    // Возвращаем текущее значение регистра счётчика тактов CPU для точного замера интервалов
    // времени
    return DWT->CYCCNT;
}
