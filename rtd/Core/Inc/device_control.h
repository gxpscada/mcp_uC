#ifndef DEVICE_CONTROL_H
#define DEVICE_CONTROL_H

#include "device_acquisition.h"
#include "device_processing.h"
#include "modbus_ascii.h"
#include "device_registers.h" // Подключаем структуру рабочей конфации и константы адресов регистров

// Главная структура состояния прибора, объединяющая настройки, фильтры и буферы регистров Modbus
typedef struct {
    device_config_t config; // Активная рабочая конфигурация в физических единицах (Омы, коэффициенты, размеры окон)
    device_filter_t filters[DEVICE_MAX_CHANNELS]; // Массив индивидуальных состояний медианного и усредняющего фильтров по каналам
    device_processed_t processed[DEVICE_MAX_CHANNELS]; // Массив рассчитанных значений сопротивления и температуры float32 по каналам
    uint16_t holding[DEVICE_HOLDING_COUNT]; // Сырой 16-битный образ области Holding Registers для быстрого чтения по Modbus
    uint16_t inputs[DEVICE_INPUT_COUNT]; // Сырой 16-битный снимок области Input Registers (температуры int16, коды АЦП, статусы, время)
    uint16_t error_code; // Код последней зафиксированной ошибки протокола или аппаратуры (отдаётся в регистре IR_ERROR)
    uint8_t hardware_error; // Флаг аппаратного отказа периферии (SPI, MAX31865, RTC или Flash)
    uint8_t run_requested; // Запрошенное мастером состояние работы через флаг COIL_RUN (1 — запуск, 0 — останов)
    uint8_t running; // Фактическое состояние автомата: 1 только когда все активные датчики прошли инициализацию и переведены в AUTO
    uint8_t config_changed; // Индикатор того, что настройки фильтрации или калибровки были изменены по Modbus
    uint8_t stop_requested; // Одноразовый триггер принудительного перехода в фазу отключения датчиков STOPPING
    uint16_t commands; // Битовая маска отложенной сервисной команды (Save, Clear, RTC, Reboot, Defaults)
    uint32_t runtime_ms; // Время непрерывной работы текущего сеанса измерений в миллисекундах
    uint32_t frame_seconds; // Штамп времени UTC (секунды Unix) последнего обработанного кадра измерений
    uint16_t frame_milliseconds; // Подсекундная часть штампа времени последнего кадра (0..999 мс)
} device_control_t; // Имя типа структуры контекста управления

void device_config_defaults(device_config_t *); // Прототип функции сброса структуры настроек к заводским значениям из device_config.h
int device_config_valid(const device_config_t *); // Прототип функции проверки допустимости всех полей конфигурации
void device_control_init(device_control_t *); // Прототип функции начальной инициализации контекста управления и карты регистров
void device_control_invalidate(device_control_t *, uint8_t status); // Прототип функции сброса истории фильтров и пометки выходных регистров кодом недостоверности
void device_control_frame(device_control_t *, const device_frame_t *); // Прототип функции обработки нового кадра от АЦП и обновления Input-регистров
int device_control_read(void *, unsigned, uint16_t, uint16_t, uint16_t *); // Прототип callback-функции чтения любого адресного пространства Modbus
int device_control_write(void *, unsigned, uint16_t, uint16_t, const uint16_t *); // Прототип callback-функции записи в пространства Coils и Holding Registers
int device_write_coils(device_control_t *, uint16_t, uint16_t, const uint16_t *); // Прототип функции проверки и применения записи битовых флагов Coils
int device_write_holding(device_control_t *, uint16_t, uint16_t, const uint16_t *); // Прототип функции транзакционной проверки и записи Holding-регистров

#endif
