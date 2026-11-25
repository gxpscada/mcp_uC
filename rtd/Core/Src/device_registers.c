#include "device_registers.h"
#include "modbus_ascii.h"
#include <math.h>
#define REGISTER_MAX 65535u // Максимальное беззнаковое число, представимое в одном 16-битном регистре Modbus
#define SIGNED_LIMIT 32768u // Порог знакового бита для 16-битного числа в дополнительном коде (0x8000)
#define WORD_RANGE 65536u // Полный модуль 16-битного слова (2^16) для ручного перевода из дополнительного кода
#include <string.h>

// Заполнение структуры конфигурации параметрами по умолчанию из заголовочного файла
void device_config_defaults(device_config_t *c)
{
    // Массив номиналов датчиков R0 по умолчанию для всех каналов
    static const float r0[DEVICE_MAX_CHANNELS] = DEVICE_R0_DEFAULTS;
    // Массив номиналов опорных резисторов Rref по умолчанию
    static const float rref[DEVICE_MAX_CHANNELS] = DEVICE_RREF_DEFAULTS;
    // Массив коэффициентов наклона k по умолчанию (обычно 1.0)
    static const float gain[DEVICE_MAX_CHANNELS] = DEVICE_GAIN_DEFAULTS;
    // Массив смещений АЦП b по умолчанию (обычно 0)
    static const int32_t offset[DEVICE_MAX_CHANNELS] = DEVICE_OFFSET_DEFAULTS;
    unsigned i; // Счётчик цикла по всем каналам

    // Начинаем с нулевых настроек, затем заполняем значения по умолчанию. CRC считается по
    // отдельной записи журнала, не по памяти этой структуры.
    memset(c, 0, sizeof(*c));
    // Устанавливаем число активных опрашиваемых каналов по умолчанию
    c->active_channels = DEVICE_DEFAULT_CHANNELS;
    c->channel_mask = (1u << c->active_channels) - 1u;
    c->average_window = DEVICE_DEFAULT_AVERAGE; // Устанавливаем длину окна скользящего среднего по умолчанию
    c->median_window = DEVICE_DEFAULT_MEDIAN; // Устанавливаем длину окна медианного фильтра по умолчанию
    c->use_median = DEVICE_DEFAULT_USE_MEDIAN; // Флаг включения медианы по умолчанию
    c->use_average = DEVICE_DEFAULT_USE_AVERAGE; // Флаг включения скользящего среднего по умолчанию
    c->use_calibration = DEVICE_DEFAULT_USE_CALIBRATION; // Флаг использования калибровки по умолчанию

    // Копируем поканальные параметры для всех каналов (включая резервные)
    for (i = 0; i < DEVICE_MAX_CHANNELS; ++i) {
        c->r0[i] = r0[i]; // Записываем номинальное сопротивление R0 канала i в Омах
        c->rref[i] = rref[i]; // Записываем опорное сопротивление Rref канала i в Омах
        c->adc_offset[i] = offset[i]; // Записываем смещение кода АЦП b канала i
        c->gain[i] = gain[i]; // Записываем калибровочный коэффициент k канала i
    }
}

// Проверка допустимости значения одиночного Holding-регистра и запись его в массив образа регистров
int register_validateAndSet(uint16_t *map, uint16_t a, uint16_t v)
{
    // Если индекс регистра выходит за размер таблицы Holding, возвращаем ошибку адреса
    if (a >= DEVICE_HOLDING_COUNT) return MB_ILLEGAL_ADDRESS;
    // Число активных каналов не может быть нулевым или превышать число распаянных микросхем
    if (a == HR_CHANNELS && (!v || v > DEVICE_BOARD_CHANNEL_COUNT)) return MB_ILLEGAL_VALUE;
    // Сопротивление Rref и коэффициент наклона k не могут быть равны нулю
    if (((a >= HR_RREF && a < HR_OFFSET) || (a >= HR_GAIN && a < HR_AVERAGE)) && !v) return MB_ILLEGAL_VALUE;
    // Окно усреднения принимает только значения 2, 4, 8 или 16
    if (a == HR_AVERAGE && v != 2 && v != 4 && v != 8 && v != 16) return MB_ILLEGAL_VALUE;
    // Окно медианы принимает только нечётные значения 3, 5 или 7
    if (a == HR_MEDIAN && v != 3 && v != 5 && v != 7) return MB_ILLEGAL_VALUE;
    if (a == HR_CHANNEL_MASK && (!v || v >= (1u << DEVICE_BOARD_CHANNEL_COUNT))) return MB_ILLEGAL_VALUE;
    map[a] = v; // Все проверки пройдены: сохраняем 16-битное слово в массив Holding-регистров
    return 0; // Возвращаем 0 (успех)
}

// Декодирование значения из проверенного Holding-регистра в физическое поле структуры настроек
void register_updateFromModbus(device_config_t *c, const uint16_t *map, uint16_t a)
{
    uint16_t v = map[a]; // Считываем сырое 16-битное слово из массива регистров по адресу a
    // Если адрес принадлежит диапазону опорных сопротивлений Rref каналов
    if (a >= HR_RREF && a < HR_OFFSET) {
        // Переводим значение в Омы делением на масштабный коэффициент (например, делим на 10.0f)
        c->rref[a - HR_RREF] = v / REGISTER_RREF_SCALE;
    // Если адрес принадлежит диапазону калибровочных смещений АЦП (b)
    } else if (a >= HR_OFFSET && a < HR_GAIN) {
        // Восстанавливаем знак: например, 65534 - 65536 = -2. Рабочее поле имеет тип int32_t.
        c->adc_offset[a - HR_OFFSET] = v < SIGNED_LIMIT ? (int32_t)v : (int32_t)v - (int32_t)WORD_RANGE;
    } else if (a >= HR_GAIN && a < HR_AVERAGE) { // Если адрес относится к диапазону коэффициентов наклона (k)
        // Делим на 100: значение регистра 260 превращается в коэффициент 2.6.
        c->gain[a - HR_GAIN] = v / REGISTER_GAIN_SCALE;
    } else { // Обработка одиночных общих регистров конфигурации
        switch (a) {
        case HR_CHANNELS: c->active_channels = v; c->channel_mask = (1u << v) - 1u; break;
        case HR_CHANNEL_MASK: c->channel_mask = v; break; // Обновляем количество активных опрашиваемых каналов
        case HR_AVERAGE: c->average_window = v; break; // Обновляем размер окна скользящего среднего
        case HR_MEDIAN: c->median_window = v; break; // Обновляем размер окна медианного фильтра
        case HR_RTC_LO: // Если изменилось младшее слово буфера установки времени RTC
        case HR_RTC_HI: { // Или старшее слово буфера установки времени RTC
            device_value32_t value; // Union для представления 32-битного значения.
            // Склеиваем 32-битный штамп Unix-времени из младшего и старшего 16-битных регистров
            value.u32 = (uint32_t)map[HR_RTC_LO] | ((uint32_t)map[HR_RTC_HI] << 16);
            // Сохраняем подготовленное время в поле ожидания команды установки RTC
            c->rtc_pending = value.u32;
            break;
        }
        default: break; // Прочие адреса уже обработаны условиями выше
        }
    }
}

// Кодирование одного параметра из рабочей структуры настроек обратно в 16-битный формат
// Holding-регистра
int register_set2modbus(const device_config_t *c, uint16_t *map, uint16_t a)
{
    // Переменная повышенной точности для масштабирования float-параметров перед округлением
    double scaled = 0;
    // Проверка выхода индекса за пределы массива Holding-регистров
    if (a >= DEVICE_HOLDING_COUNT) return MB_ILLEGAL_ADDRESS;

    if (a >= HR_RREF && a < HR_OFFSET) { // Кодирование опорного сопротивления Rref канала
        // Умножаем сопротивление в Омах на масштабный множитель протокола
        scaled = (double)c->rref[a - HR_RREF] * REGISTER_RREF_SCALE;
    } else if (a >= HR_GAIN && a < HR_AVERAGE) { // Кодирование коэффициента наклона k канала
        // Умножаем коэффициент k на масштабный множитель протокола
        scaled = (double)c->gain[a - HR_GAIN] * REGISTER_GAIN_SCALE;
    } else if (a >= HR_OFFSET && a < HR_GAIN) { // Кодирование знакового смещения АЦП b
        int32_t offset = c->adc_offset[a - HR_OFFSET]; // Считываем 32-битное знаковое смещение канала
        // Проверяем, что смещение помещается в стандартный 16-битный знаковый диапазон int16
        if (offset < -32768 || offset > 32767) return MB_ILLEGAL_VALUE;
        // Приводим к uint16_t (дополнительный код) и сохраняем через функцию валидации
        return register_validateAndSet(map, a, (uint16_t)offset);
    } else { // Кодирование общих регистров настроек
        switch (a) {
        // Число активных каналов передаётся один к одному
        case HR_CHANNELS: scaled = c->active_channels; break;
        case HR_CHANNEL_MASK: scaled = c->channel_mask; break;
        // Размер окна усреднения передаётся как целое число
        case HR_AVERAGE: scaled = c->average_window; break;
        case HR_MEDIAN: scaled = c->median_window; break; // Размер окна медианы передаётся как целое число
        case HR_RTC_LO: // Кодирование младшего слова времени установки RTC
        case HR_RTC_HI: { // Кодирование старшего слова времени установки RTC
            device_value32_t value; // Вспомогательная переменная для разбора 32-битного штампа времени
            value.u32 = c->rtc_pending; // Берём 32-битное значение ожидающего времени
            // Выделяем младшие или старшие 16 бит сдвигом и записываем в массив map
            return register_validateAndSet(map, a, (uint16_t)(value.u32 >> (a == HR_RTC_LO ? 0 : 16)));
        }
        default: return MB_ILLEGAL_ADDRESS; // Защита от неизвестного адреса
        }
    }

    // Проверяем, что масштабированное число конечно и лежит в диапазоне 0..65535
    if (!isfinite(scaled) || scaled < 0 || scaled > REGISTER_MAX) return MB_ILLEGAL_VALUE;
    // Округляем до ближайшего целого и записываем в массив регистров с проверкой
    return register_validateAndSet(map, a, (uint16_t)lround(scaled));
}

// Полная выгрузка рабочей структуры конфигурации в массив Holding-регистров Modbus
int registers_encode(const device_config_t *c, uint16_t *map)
{
    // Временный буфер для сохранения атомарности (при ошибке на середине выходной массив map не
    // испортится)
    uint16_t temporary[DEVICE_HOLDING_COUNT];
    unsigned i; // Счётчик по всем адресам Holding-регистров
    for (i = 0; i < DEVICE_HOLDING_COUNT; ++i) { // Проходим по каждому регистру таблицы
        // Переводим соответствующее поле настроек в формат Modbus
        int result = register_set2modbus(c, temporary, (uint16_t)i);
        // При ошибке в любом регистре немедленно прерываем операцию и возвращаем код ошибки
        if (result) return result;
    }
    // Когда все регистры успешно сформированы, копируем временный буфер в целевой массив map
    memcpy(map, temporary, sizeof(temporary));
    return 0; // Возвращаем 0 (успех)
}

// Полное восстановление рабочей структуры конфигурации из массива Holding-регистров
int registers_decode(device_config_t *c, const uint16_t *map)
{
    uint16_t temporary[DEVICE_HOLDING_COUNT] = {0}; // Временный проверочный массив регистров
    // Копируем текущую конфигурацию (чтобы сохранить поля, не представленные в Modbus, такие как
    // R0)
    device_config_t next = *c;
    unsigned i; // Индекс цикла по регистрам
    for (i = 0; i < DEVICE_HOLDING_COUNT; ++i) { // Проверяем и переносим каждый регистр по порядку
        // Валидируем значение i-го регистра
        int result = register_validateAndSet(temporary, (uint16_t)i, map[i]);
        // Если найдено недопустимое значение, прерываем декодирование без изменения *c
        if (result) return result;
        // Обновляем соответствующее физическое поле во временной структуре next
        register_updateFromModbus(&next, temporary, (uint16_t)i);
    }
    *c = next; // Все регистры корректны: копируем собранную конфигурацию в выходную структуру
    return 0; // Возвращаем 0 (успех)
}

// Комплексная проверка валидности структуры конфигурации (используется при загрузке из Flash и
// записи по Modbus)
int device_config_valid(const device_config_t *c)
{
    uint16_t map[DEVICE_HOLDING_COUNT]; // Временный буфер для тестового кодирования в Holding-регистры
    unsigned i; // Счётчик каналов
    // Булевы флаги режимов должны содержать строго 0 или 1
    if (!c->active_channels || c->active_channels > DEVICE_BOARD_CHANNEL_COUNT ||
        !c->channel_mask || c->channel_mask >= (1u << c->active_channels)) return 0;
    if (c->use_median > 1 || c->use_average > 1 || c->use_calibration > 1) return 0;
    for (i = 0; i < DEVICE_MAX_CHANNELS; ++i) // Проверяем номинальные сопротивления R0 всех каналов
        // Если R0 содержит NaN/Inf или меньше/равно 0, конфигурация повреждена
        if (!isfinite(c->r0[i]) || c->r0[i] <= 0) return 0;
    // Проверяем, что все остальные поля без ошибок укладываются в допустимые диапазоны регистров
    // Modbus
    return registers_encode(c, map) == 0;
}
