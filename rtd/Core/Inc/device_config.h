#ifndef DEVICE_CONFIG_H
#define DEVICE_CONFIG_H

#ifndef DEVICE_DEFAULT_CHANNELS // Позволяет переопределить число активных каналов через ключи компилятора (-D)
#define DEVICE_DEFAULT_CHANNELS 3u // Количество опрашиваемых каналов по умолчанию при первом старте или сбросе настроек
#endif // Конец условного блока определения числа каналов

#define DEVICE_MODBUS_ADDRESS 1u // Сетевой адрес ведомого устройства (Slave ID) в протоколе Modbus (допустимый диапазон 1..247)
#define DEVICE_USB_BAUD 1000000u // Номинальная скорость виртуального COM-порта (Line Coding USB CDC), запрашиваемая драйвером хоста
#ifndef DEVICE_DEFAULT_R0
#define DEVICE_DEFAULT_R0 1000.0f // Номинальное сопротивление платинового термометра при 0 °C (1000 Ом соответствует типу Pt1000)
#endif
#ifndef DEVICE_DEFAULT_RREF
#define DEVICE_DEFAULT_RREF 4300.0f // Сопротивление прецизионного опорного резистора Rref на плате (4.3 кОм — типовой номинал для Pt1000 с MAX31865)
#endif
#define DEVICE_DEFAULT_OFFSET 0 // Калибровочное смещение нуля АЦП (коэффициент b) по умолчанию в младших значащих разрядах
#define DEVICE_DEFAULT_GAIN 1.0f // Калибровочный крутизна/множитель температуры (коэффициент k) по умолчанию (1.0 — без коррекции наклона)
#define DEVICE_DEFAULT_MEDIAN 3u // Размер окна медианного фильтра по умолчанию (3 отсчёта)
#define DEVICE_DEFAULT_AVERAGE 4u // Размер окна фильтра скользящего среднего по умолчанию (4 отсчёта)
#define DEVICE_DEFAULT_USE_MEDIAN 1u // Флаг включения медианного фильтра по умолчанию (1 — включён)
#define DEVICE_DEFAULT_USE_AVERAGE 1u // Флаг включения фильтра скользящего среднего по умолчанию (1 — включён)
#define DEVICE_DEFAULT_USE_CALIBRATION 1u // Флаг разрешения калибровочных поправок b и k по умолчанию (1 — включены)
#ifndef DEVICE_SENSOR_WIRES
#define DEVICE_SENSOR_WIRES 4u // Схема подключения термосопротивлений к MAX31865 (4 — четырёхпроводная схема измерения)
#endif
#define DEVICE_SENSOR_FILTER_HZ 50u // Частота встроенного режекторного фильтра MAX31865 для подавления помех промышленной сети (50 Гц)

// Массив инициализации номинальных сопротивлений R0 при 0 °C для всех 8 логических каналов (T0..T7)
#define DEVICE_R0_DEFAULTS { \
    DEVICE_DEFAULT_R0, DEVICE_DEFAULT_R0, DEVICE_DEFAULT_R0, DEVICE_DEFAULT_R0, \
    DEVICE_DEFAULT_R0, DEVICE_DEFAULT_R0, DEVICE_DEFAULT_R0, DEVICE_DEFAULT_R0 }

// Массив инициализации опорных сопротивлений Rref для всех 8 логических каналов
#define DEVICE_RREF_DEFAULTS { \
    DEVICE_DEFAULT_RREF, DEVICE_DEFAULT_RREF, DEVICE_DEFAULT_RREF, DEVICE_DEFAULT_RREF, \
    DEVICE_DEFAULT_RREF, DEVICE_DEFAULT_RREF, DEVICE_DEFAULT_RREF, DEVICE_DEFAULT_RREF }

// Массив инициализации аддитивных смещений кода АЦП (b) для всех 8 каналов
#define DEVICE_OFFSET_DEFAULTS { \
    DEVICE_DEFAULT_OFFSET, DEVICE_DEFAULT_OFFSET, DEVICE_DEFAULT_OFFSET, DEVICE_DEFAULT_OFFSET, \
    DEVICE_DEFAULT_OFFSET, DEVICE_DEFAULT_OFFSET, DEVICE_DEFAULT_OFFSET, DEVICE_DEFAULT_OFFSET }

// Массив инициализации мультипликативных коэффициентов наклона (k) для всех 8 каналов
#define DEVICE_GAIN_DEFAULTS { \
    DEVICE_DEFAULT_GAIN, DEVICE_DEFAULT_GAIN, DEVICE_DEFAULT_GAIN, DEVICE_DEFAULT_GAIN, \
    DEVICE_DEFAULT_GAIN, DEVICE_DEFAULT_GAIN, DEVICE_DEFAULT_GAIN, DEVICE_DEFAULT_GAIN }

#if DEVICE_MODBUS_ADDRESS < 1 || DEVICE_MODBUS_ADDRESS > 247 // Проверка препроцессором: адрес Modbus Unicast обязан лежать в пределах 1..247
#error Modbus_address_must_be_1_to_247 // Остановка компиляции при задании широковещательного (0) или зарезервированного (>247) адреса
#endif // Конец проверки адреса Modbus

#if DEVICE_SENSOR_WIRES != 2 && DEVICE_SENSOR_WIRES != 3 && DEVICE_SENSOR_WIRES != 4 // Проверка препроцессором корректности выбранной схемы подключения RTD
#error Sensor_wires_must_be_2_3_or_4 // Остановка компиляции, если число проводов отличается от 2, 3 или 4
#endif // Конец проверки числа проводов

#if DEVICE_SENSOR_FILTER_HZ != 50 && DEVICE_SENSOR_FILTER_HZ != 60 // Проверка препроцессором частоты подавления сетевой наводки
#error Sensor_filter_must_be_50_or_60_Hz // Остановка компиляции, если указана частота, отличная от 50 или 60 Гц
#endif // Конец проверки частоты фильтра

#endif
