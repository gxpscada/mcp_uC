/**
 * @file max31865_driver.c
 * @brief Переносимый драйвер одного MAX31865: SPI, управление током RTD и диагностика.
 */
#include "max31865_driver.h"
#include <string.h>
static max31865_result_t exchange(max31865_t *d, const uint8_t *tx,
                                 uint8_t *rx, size_t n)
{
    if (!d || !d->transfer) return MAX31865_BAD_ARGUMENT;
    return d->transfer(d->context, tx, rx, n) ? MAX31865_IO_ERROR : MAX31865_OK;
}
static max31865_result_t configure(max31865_t *d, uint8_t config)
{
    uint8_t tx[2] = {0x80, config}, rx[2] = {0};
    max31865_result_t result = exchange(d, tx, rx, 2);
    if (result != MAX31865_OK) return result;
    tx[0] = 0; tx[1] = 0;
    result = exchange(d, tx, rx, 2);
    if (result != MAX31865_OK) return result;
    if (rx[1] != config) return MAX31865_CONFIG_ERROR;
    d->config = config;
    return MAX31865_OK;
}
/**
 * @brief Настраивает один MAX31865, оставляя измерения и ток смещения выключенными.
 * @param[out] d Контекст конкретной микросхемы; должен существовать всё время её использования.
 * @param[in] context Пользовательские данные SPI-адаптера, передаваемые в transfer без изменений.
 * @param[in] transfer Синхронный обмен: удерживает CS на время всей транзакции и снимает его при
 * любом выходе.
 * @param[in] wires Число проводов: 2, 3 или 4. Отдельный бит MAX31865 нужен только для схемы с 3 проводами.
 * @param[in] filter_hz Частота подавления сетевой помехи: 50 или 60 Гц.
 * @return MAX31865_OK при успешной записи и проверке регистра; иначе ошибка аргументов, SPI или readback.
 * @details На каждый чип нужен отдельный контекст, даже при общей SPI-шине.
 * Драйвер не знает о HAL, GPIO и времени: эти обязанности остаются у приложения.
 * После успешной инициализации вызвать max31865_bias(), выдержать время установления
 * аналоговой цепи и только затем вызвать max31865_start().
 */
max31865_result_t max31865_init(max31865_t *d, void *context,
                               max31865_transfer_fn transfer,
                               unsigned wires, unsigned filter_hz)
{
    if (!d || !transfer || (wires != 2 && wires != 3 && wires != 4) ||
        (filter_hz != 50 && filter_hz != 60)) return MAX31865_BAD_ARGUMENT;
    d->context = context; d->transfer = transfer; d->config = 0;
    return configure(d, (uint8_t)(
        (wires == 3 ? MAX31865_THREE_WIRE : 0) |
        (filter_hz == 50 ? MAX31865_FILTER_50HZ : 0)));
}
/**
 * @brief Включает BIAS и непрерывный режим преобразования AUTO.
 * @param[in,out] d Инициализированный контекст микросхемы.
 * @return MAX31865_OK либо ошибка обмена/проверки конфигурации/аргументов.
 * @pre После включения BIAS уже выдержано время установления аналоговой цепи.
 * @note Успех означает подтверждённую конфигурацию, а не готовность первого отсчёта: нужно дождаться DRDY.
 */
max31865_result_t max31865_start(max31865_t *d)
{
    if (!d || !d->transfer) return MAX31865_BAD_ARGUMENT;
    return configure(d, (uint8_t)(d->config | MAX31865_AUTO | MAX31865_BIAS));
}
/**
 * @brief Включает ток смещения RTD без запуска непрерывных преобразований.
 * @param[in,out] d Инициализированный контекст микросхемы.
 * @return MAX31865_OK либо ошибка обмена/проверки конфигурации/аргументов.
 * @details Функция сама не выдерживает задержку установления напряжения.
 * Время ожидания организует вызывающая сторона, не блокируя главный цикл.
 */
max31865_result_t max31865_bias(max31865_t *d)
{
    if (!d || !d->transfer) return MAX31865_BAD_ARGUMENT;
    return configure(d, (uint8_t)((d->config & 0x11u) | MAX31865_BIAS));
}
/**
 * @brief Пытается выключить и AUTO, и BIAS, чтобы прекратить нагрев датчика измерительным током.
 * @param[in,out] d Контекст микросхемы; желаемое состояние OFF сохраняется даже при ошибке SPI.
 * @return MAX31865_OK, если запись и контрольное чтение прошли успешно; иначе код ошибки.
 * @details Запись выполняется при каждом вызове, даже если программный контекст уже хранит OFF.
 * Это позволяет повторить STOP после сбоя шины. При неудаче физическое отключение тока
 * не подтверждено; ошибка не должна трактоваться как успешная остановка.
 */
max31865_result_t max31865_stop(max31865_t *d)
{
    if (!d || !d->transfer) return MAX31865_BAD_ARGUMENT;
    /* Write even when readback previously failed: STOP must still be tried. */
    d->config &= 0x11u; /* remember requested OFF even if the bus fails */
    return configure(d, d->config);
}
/**
 * @brief Одной SPI-транзакцией читает исходный отсчёт и признаки неисправности.
 * @param[in,out] d Контекст устройства с настроенным SPI-адаптером.
 * @param[out] sample Результат; перед обменом обнуляется. Не может быть NULL.
 * @return MAX31865_OK при успешном обмене, даже если сам датчик сообщает неисправность.
 * @details Читаются регистры 1..7 вместе с промежуточными порогами. Поле raw содержит
 * 15-битный код 0..32767 без младшего флага ошибки. Отдельно возвращаются rtd_fault
 * и faults. Проверить их обязан вызывающий код: успешный SPI не означает исправный RTD.
 * Функция не ожидает DRDY, не фильтрует данные и не сбрасывает защёлкнутые ошибки.
 */
max31865_result_t max31865_read(max31865_t *d, max31865_sample_t *sample)
{
    /* Burst: RTD + thresholds + fault status, one chip-select transaction. */
    uint8_t tx[8] = {1, 0, 0, 0, 0, 0, 0, 0}, rx[8] = {0};
    max31865_result_t result;
    uint16_t word;
    if (!sample) return MAX31865_BAD_ARGUMENT;
    memset(sample, 0, sizeof(*sample));
    result = exchange(d, tx, rx, sizeof(tx));
    if (result != MAX31865_OK) return result;
    word = (uint16_t)(((uint16_t)rx[1] << 8) | rx[2]);
    sample->raw = word >> 1;
    sample->rtd_fault = (uint8_t)(word & 1u);
    sample->faults = rx[7] & 0xfcu;
    return MAX31865_OK;
}
/**
 * @brief Записывает команду сброса защёлкнутых ошибок MAX31865.
 * @param[in,out] d Контекст устройства; остальные биты берутся из сохранённой конфигурации.
 * @return MAX31865_OK при успешной передаче команды, иначе ошибка аргументов или SPI.
 * @note Обратное чтение здесь не выполняется. Причина неисправности может сохраняться,
 * поэтому после сброса следует снова проверить состояние датчика.
 */
max31865_result_t max31865_clear_faults(max31865_t *d)
{
    uint8_t tx[2], rx[2] = {0};
    if (!d || !d->transfer) return MAX31865_BAD_ARGUMENT;
    tx[0] = 0x80; tx[1] = (uint8_t)(d->config | 0x02);
    return exchange(d, tx, rx, 2);
}
