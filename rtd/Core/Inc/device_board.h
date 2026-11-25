#ifndef DEVICE_BOARD_H
#define DEVICE_BOARD_H

#define DEVICE_MAX_CHANNELS 8u // Предельное число каналов, поддерживаемое архитектурой прошивки и картой регистров Modbus
#define DEVICE_CHANNELS DEVICE_MAX_CHANNELS // Синоним максимальной ёмкости массивов каналов (используется при объявлении таблиц)

#ifndef DEVICE_BOARD_CHANNEL_COUNT // Позволяет переопределить число физически распаянных микросхем через настройки сборки проекта
#define DEVICE_BOARD_CHANNEL_COUNT 3u // Фактическое число микросхем MAX31865, установленных на данной ревизии печатной платы
#endif // Конец блока определения числа физических каналов

#define DEVICE_HW_REVISION 1u // Код аппаратной ревизии печатной платы (доступен для чтения по Modbus в регистре IR_HW)
#define DEVICE_SW_REVISION 0x0031u // Версия встроенного программного обеспечения в формате 0xMMmm (версия 0.3, регистр IR_SW)

#if DEVICE_BOARD_CHANNEL_COUNT < 1 || DEVICE_BOARD_CHANNEL_COUNT > DEVICE_MAX_CHANNELS // Проверка препроцессором корректности числа физических каналов
#error Invalid_physical_channel_count // Остановка компиляции, если число физических каналов меньше 1 или больше 8
#endif // Конец проверки

#endif
