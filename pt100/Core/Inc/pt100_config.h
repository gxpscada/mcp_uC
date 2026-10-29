/**
 * @file pt100_config.h
 * @brief Параметры сборки и представление сохраняемой конфигурации.
 */
#ifndef PT100_CONFIG_H
#define PT100_CONFIG_H
#include <stdint.h>
/**
 * @name Переопределение параметров при сборке
 * PT100_CUSTOM_CONFIG_HEADER задаётся как имя заголовка в кавычках через -D.
 * Он подключается до значений по умолчанию и может определить сразу согласованный
 * набор настроек платы. R0 задаётся в Омах, Rref — в десятых долях Ома.
 * Массивы R0/Rref должны содержать восемь элементов, даже при трёх активных каналах.
 * Физическое число каналов ограничивает допустимое значение Holding 0x00.
 * При изменении платы нужно также обновить startup, MCU define и linker-скрипт.
 * @{
 */
#ifdef PT100_CUSTOM_CONFIG_HEADER
#include PT100_CUSTOM_CONFIG_HEADER
#endif
/* Portable build settings. Override via compiler -D or a custom board header. */
#define PT100_MAX_CHANNELS 8u /**< Ёмкость массивов и карты протокола; не число подключённых датчиков. */
#ifndef PT100_DEFAULT_CHANNELS
#define PT100_DEFAULT_CHANNELS 3u /**< Число активных каналов при отсутствии сохранённых настроек. */
#endif
#ifndef PT100_BOARD_CHANNEL_COUNT
#define PT100_BOARD_CHANNEL_COUNT 3u /**< Число реально разведённых каналов, GPIO которых разрешено настраивать. */
#endif
#if PT100_BOARD_CHANNEL_COUNT < 1 || PT100_BOARD_CHANNEL_COUNT > 8
#error PT100_BOARD_CHANNEL_COUNT_must_be_1_to_8
#endif
#if PT100_DEFAULT_CHANNELS < 1 || PT100_DEFAULT_CHANNELS > PT100_BOARD_CHANNEL_COUNT
#error Default_count_exceeds_wired_channels
#endif
#ifndef PT100_MODBUS_ADDRESS
#define PT100_MODBUS_ADDRESS 1u /**< Собственный unicast-адрес; выбрать 1..247, адрес 0 оставлен для broadcast. */
#endif
#ifndef PT100_R0_VALUES
#define PT100_R0_VALUES {1000,1000,1000,1000,1000,1000,1000,1000} /**< Номиналы при 0 °C, Ом; 100 для PT100, 1000 для PT1000. */
#endif
#ifndef PT100_RREF_X10_VALUES
#define PT100_RREF_X10_VALUES {43000,43000,43000,43000,43000,43000,43000,43000} /**< Rref*10: 43000 соответствует 4300 Ом, а не 430 Ом. */
#endif
#ifndef PT100_HW_REVISION
#define PT100_HW_REVISION 1u
#endif
#define PT100_SW_REVISION 0x0200u
#define PT100_HOLDING_COUNT 29u
#define PT100_COIL_MEDIAN 6u
#define PT100_COIL_AVERAGE 7u
#define PT100_COIL_CALIBRATION 8u
#define PT100_DEFAULT_FLAGS ((1u<<6)|(1u<<7)|(1u<<8)) /**< При старте включены медиана, среднее и калибровка. */
/** @} */
/**
 * @brief Сохраняемый образ настроек: holding соответствует адресам 0x00..0x1C, flags хранит только
 * биты Coils 6/7/8. Rref хранится как Ом*10, b — дополнительный код int16, k — коэффициент*100.
 * Run и импульсные команды не сохраняются.
 */
typedef struct {
    uint16_t holding[PT100_HOLDING_COUNT];
    uint16_t flags;
} pt100_config_t;
#endif
