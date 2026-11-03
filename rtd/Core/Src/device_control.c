#include "device_control.h"
#include "device_calendar.h"
#include <math.h>
#include <string.h>

// Сброс буферов фильтрации и перевод выходных регистров всех каналов в невалидное состояние
void device_control_invalidate(device_control_t *c, uint8_t status)
{
    unsigned i; // Счётчик цикла по каналам
    // Полностью очищаем накопленные окна медианного и усредняющего фильтров
    memset(c->filters, 0, sizeof(c->filters));
    // Обнуляем структуру рассчитанных float-значений температуры и сопротивления
    memset(c->processed, 0, sizeof(c->processed));
    for (i = 0; i < DEVICE_MAX_CHANNELS; ++i) { // Проходим по всем возможным каналам карты регистров
        // Записываем программный код статуса в старший байт регистра состояния канала (младший байт
        // ошибок чипа = 0)
        c->inputs[IR_STATUS + i] = (uint16_t)((unsigned)status << 8);
        // Записываем в целочисленный регистр температуры спецкод недостоверного значения (-32768 /
        // 0x8000)
        c->inputs[IR_TEMPERATURE + i] = REGISTER_INVALID_T;
        // Записываем в регистр АЦП маркер невалидного отсчёта (0xFFFF)
        c->inputs[IR_ADC + i] = REGISTER_INVALID_ADC;
    }
    c->frame_seconds = 0; // Сбрасываем штамп времени последнего кадра (секунды)
    c->frame_milliseconds = 0; // Сбрасываем миллисекунды последнего кадра
    c->inputs[IR_RTC_LO] = 0; // Обнуляем младший регистр времени кадра
    c->inputs[IR_RTC_HI] = 0; // Обнуляем старший регистр времени кадра
}

// Первичная инициализация структуры управления и регистров Modbus при старте МК
void device_control_init(device_control_t *c)
{
    memset(c, 0, sizeof(*c)); // Обнуляем всю структуру управления (флаги команд, ошибок, счётчики)
    device_config_defaults(&c->config); // Загружаем заводские настройки в рабочую структуру конфигурации
    // Формируем начальное содержимое массива Holding-регистров из настроек по умолчанию
    (void)registers_encode(&c->config, c->holding);
    // Выставляем во всех измерительных регистрах статус STOPPED и маркеры невалидных данных
    device_control_invalidate(c, DEVICE_STOPPED);
    // Записываем константу аппаратной ревизии платы в соответствующий Input-регистр
    c->inputs[IR_HW] = DEVICE_HW_REVISION;
    c->inputs[IR_SW] = DEVICE_SW_REVISION; // Записываем версию прошивки в Input-регистр версии ПО
}

// Математическая обработка нового кадра данных и обновление снимка Input-регистров Modbus
void device_control_frame(device_control_t *c, const device_frame_t *f)
{
    unsigned i; // Индекс канала
    c->frame_seconds = f->utc_seconds; // Сохраняем секунды UTC текущего кадра в контексте управления
    c->frame_milliseconds = f->milliseconds; // Сохраняем миллисекунды текущего кадра
    // Упаковываем младшие 16 бит времени кадра в Input-регистр Modbus
    c->inputs[IR_RTC_LO] = (uint16_t)f->utc_seconds;
    // Упаковываем старшие 16 бит времени кадра в соседний Input-регистр
    c->inputs[IR_RTC_HI] = (uint16_t)(f->utc_seconds >> 16);

    for (i = 0; i < DEVICE_MAX_CHANNELS; ++i) { // Обрабатываем каждый канал из пришедшего кадра
        // Указатель на сырые данные и ошибки i-го канала из кадра
        const device_channel_data_t *d = &f->channel[i];
        uint8_t status = d->status; // Берём статус канала, присвоенный на этапе опроса SPI
        // Указатель на структуру результатов математической обработки канала
        device_processed_t *p = &c->processed[i];
        // Локальная структура параметров фильтрации и калибровки для передачи в вычислитель
        device_processing_settings_t s;
        int result; // Код возврата функции математической обработки

        // Все каналы за пределами выбранного числа активных принудительно помечаем как
        // остановленные
        if (i >= c->config.active_channels) status = DEVICE_STOPPED;
        // По умолчанию до успешного расчёта выставляем в регистре температуры маркер ошибки
        c->inputs[IR_TEMPERATURE + i] = REGISTER_INVALID_T;
        // Если ошибок сбора нет, публикуем чистый 15-битный код АЦП, иначе 0xFFFF
        c->inputs[IR_ADC + i] = status ? REGISTER_INVALID_ADC : d->sample.raw;
        memset(p, 0, sizeof(*p)); // Сбрасываем прежние вычисленные float-значения канала

        if (status) { // Если на этапе опроса датчика зафиксирована любая ошибка (обрыв, SPI, не готов)
            // Сбрасываем историю медианного и скользящего среднего фильтров этого канала, чтобы не
            // смешивать сбой с хорошими отсчётами
            device_filter_reset(&c->filters[i]);
        } else { // Если сырой отсчёт АЦП валиден, готовим параметры для расчёта температуры
            // Задаём размер окна медианного фильтра (3, 5 или 7)
            s.median_window = (uint8_t)c->config.median_window;
            // Задаём размер окна скользящего среднего (2, 4, 8 или 16)
            s.average_window = (uint8_t)c->config.average_window;
            s.use_median = c->config.use_median; // Флаг включения медианной фильтрации
            s.use_average = c->config.use_average; // Флаг включения усреднения
            // Флаг применения калибровочных коэффициентов (смещения АЦП b и наклона k)
            s.use_calibration = c->config.use_calibration;
            // Индивидуальное знаковое смещение кода АЦП (коэффициент b) для канала i
            s.adc_offset = c->config.adc_offset[i];
            s.r0 = c->config.r0[i]; // Номинальное сопротивление датчика при 0 °C (обычно 100 Ом или 1000 Ом)
            s.rref = c->config.rref[i]; // Точное значение опорного резистора Rref на плате для канала i
            s.gain = c->config.gain[i]; // Коэффициент k: умножаем на него рассчитанную температуру.
            // Запускаем конвейер: медиана -> среднее -> смещение -> расчёт Ом -> формула Каллендара
            // — Ван Дюзена -> наклон
            result = device_process(&c->filters[i], &s, d->sample.raw, p);
            // Если функция вернула 0, значит выборка в окнах фильтров ещё не накопилась целиком
            if (!result) {
                status = DEVICE_FILTER_WARMUP; // Выставляем статус прогрева цифровых фильтров
            // Если расчёт вернул ошибку диапазона или температура в сотых долях градуса не
            // помещается в int16
            } else if (result < 0 || p->temperature * REGISTER_T_SCALE < -32767.0f || p->temperature * REGISTER_T_SCALE > 32767.0f) {
                // Ошибка расчёта либо температура не помещается в int16*100. Во втором случае
                // результат float может оставаться исправным.
                status = DEVICE_RANGE_ERROR;
            } else { // Расчёт полностью успешен и укладывается в формат регистра
                // Переводим температуру в сотые доли градуса (умножаем на 100) и округляем до
                // ближайшего целого
                int32_t t = (int32_t)lroundf(p->temperature * REGISTER_T_SCALE);
                // Записываем готовое значение в 16-битный Input-регистр температуры канала
                c->inputs[IR_TEMPERATURE + i] = (uint16_t)t;
            }
        }
        // Формируем слово статуса канала: старший байт — программный статус, младший — аппаратный
        // байт ошибок MAX31865
        c->inputs[IR_STATUS + i] = (uint16_t)(((unsigned)status << 8) | d->sample.faults | d->sample.sensor_fault);
    }
}

// Запись кода исключения Modbus во внутренний диагностический регистр ошибки прибора
static int error(device_control_t *c, int e)
{
    // Отображаем стандартные коды исключений (неверный адрес -> 1, неверное значение -> 2, прочие
    // -> 4)
    c->error_code = (uint16_t)(e == MB_ILLEGAL_ADDRESS ? 1 : e == MB_ILLEGAL_VALUE ? 2 : 4);
    return e; // Возвращаем исходный код исключения для формирования ответного пакета об ошибке в стеке Modbus
}

// Вычисление сводного флага аварии прибора для чтения через область Discrete Inputs
static int flag_error(const device_control_t *c)
{
    unsigned i; // Счётчик активных каналов
    // Если зафиксирована ошибка команды/запроса или аппаратный отказ периферии, сразу возвращаем 1
    if (c->error_code || c->hardware_error) return 1;
    // Проверяем статусы всех задействованных в измерении каналов
    for (i = 0; i < c->config.active_channels; ++i)
        // Если в любом активном канале присутствует бит фатальной ошибки, поднимаем общий флаг
        // аварии
        if ((c->inputs[IR_STATUS + i] >> 8) & (DEVICE_NOT_READY | DEVICE_BUS_ERROR | DEVICE_SENSOR_FAULT | DEVICE_INIT_ERROR | DEVICE_RANGE_ERROR)) return 1;
    return 0; // Все активные каналы и подсистемы работают штатно
}

// Обработчик чтения всех адресных пространств Modbus (Coils, Discrete Inputs, Holding Registers,
// Input Registers)
int device_control_read(void *ctx, unsigned space, uint16_t addr, uint16_t count, uint16_t *out)
{
    device_control_t *c = ctx; // Приводим контекст к типу структуры управления прибором
    unsigned i; // Индекс элемента внутри запрашиваемого диапазона регистров

    for (i = 0; i < count; ++i) { // Предварительно проверяем допустимость каждого адреса в запрошенном блоке
        unsigned a = (unsigned)addr + i; // Вычисляем абсолютный адрес текущего регистра/бита
        int ok = 0;
        switch (space) {
        case MB_COILS:
            ok = a < COIL_COUNT;
            break;
        case MB_DISCRETE:
            ok = a == 0; // Единственный сводный флаг ошибки.
            break;
        case MB_HOLDING:
            ok = a < DEVICE_HOLDING_COUNT;
            break;
        case MB_INPUT:
            // У Input есть пропуски: недостаточно проверить только последний адрес.
            if (a == IR_RUNTIME_LO || a == IR_RUNTIME_HI) {
                ok = 1;
            } else if (a >= IR_STATUS && a < IR_STATUS + DEVICE_MAX_CHANNELS) {
                ok = 1;
            } else if (a >= IR_TEMPERATURE && a < IR_ADC + DEVICE_MAX_CHANNELS) {
                // Температуры, ошибка, RTC и исходные коды ADC образуют сплошной блок.
                ok = 1;
            } else if (a == IR_HW || a == IR_SW) {
                ok = 1;
            } else if (a >= IR_TEMPERATURE_FLOAT && a < IR_EXT_END) {
                ok = 1;
            }
            break;
        default:
            break; // Неизвестное пространство: ok остаётся нулём.
        }
        // Если хотя бы один адрес попадает в «дыру» карты памяти, отклоняем весь запрос с кодом
        // Illegal Data Address
        if (!ok) return error(c, MB_ILLEGAL_ADDRESS);
    }

    // Обновляем младшее слово времени работы перед отдачей данных
    c->inputs[IR_RUNTIME_LO] = (uint16_t)c->runtime_ms;
    c->inputs[IR_RUNTIME_HI] = (uint16_t)(c->runtime_ms >> 16); // Обновляем старшее слово времени работы
    c->inputs[IR_ERROR] = c->error_code; // Копируем актуальный код ошибки в соответствующий Input-регистр

    for (i = 0; i < count; ++i) { // Заполняем выходной буфер ответа значениями проверенных регистров
        unsigned a = (unsigned)addr + i; // Текущий читаемый адрес
        if (space == MB_COILS) { // Если читается пространство битовых флагов Coils
            // Для флага RUN возвращаем текущее состояние запроса работы
            out[i] = a == COIL_RUN ? c->run_requested :
                     // Для командных битов возвращаем 1, если команда ожидает выполнения
                     a <= COIL_DEFAULTS ? (uint16_t)((c->commands >> a) & 1) :
                     // Для флага медианы возвращаем настройку медианного фильтра
                     (a == COIL_MEDIAN ? c->config.use_median :
                      // Для остальных возвращаем флаги усреднения или калибровки
                      a == COIL_AVERAGE ? c->config.use_average : c->config.use_calibration);
        } else if (space == MB_DISCRETE) { // Если читается пространство Discrete Inputs
            out[i] = (uint16_t)flag_error(c); // Возвращаем сводный признак наличия ошибок в системе
        } else if (space == MB_HOLDING) { // Если читаются Holding-регистры настроек
            // Отдаём значение напрямую из синхронизированного массива Holding-регистров
            out[i] = c->holding[a];
        // Если читается расширенная область 32-битных чисел с плавающей точкой (IEEE 754)
        } else if (a >= IR_TEMPERATURE_FLOAT) {
            // Определяем базовый адрес блока (температура или сопротивление)
            unsigned base = a < IR_RESISTANCE_FLOAT ? IR_TEMPERATURE_FLOAT : IR_RESISTANCE_FLOAT;
            // Находим структуру канала (каждое float-значение занимает 2 смежных 16-битных
            // регистра)
            const device_processed_t *p = &c->processed[(a - base) / 2u];
            // Например, 850.0f имеет код 0x44548000: регистры содержат 0x8000, затем 0x4454.
            // Это порядок двух слов; байты внутри каждого слова передаются старшим вперёд.
            device_value32_t value; // Union для представления float и его 32-битного кода.
            // Если расчёт канала валиден, берём температуру/сопротивление, иначе стандартный IEEE
            // 754 NaN
            value.f32 = p->valid ? (base == IR_TEMPERATURE_FLOAT ? p->temperature : p->resistance) : NAN;
            {
                uint32_t bits; // Временная 32-битная переменная для копирования битового представления float
                // Копируем 4 байта float в uint32_t без нарушения правил strict aliasing
                // компилятора C
                memcpy(&bits, &value.f32, sizeof(bits));
                value.u32 = bits; // Сохраняем битовый образ в поле u32
            }
            // Отдаём младшее 16-битное слово по чётному смещению и старшее слово по нечётному
            // (порядок Little-Endian Word)
            out[i] = (uint16_t)(value.u32 >> (16u * ((a - base) % 2u)));
        } else { // Для всех остальных стандартных Input-регистров
            out[i] = c->inputs[a]; // Читаем готовое 16-битное значение из массива снимка входов
        }
    }
    return 0; // Чтение успешно завершено
}

// Вспомогательная функция фиксации изменения параметров фильтрации/калибровки
static void changed(device_control_t *c)
{
    c->config_changed = 1; // Выставляем диагностический флаг изменения конфигурации
    // Очищаем буферы фильтров и переводим каналы в режим прогрева (если идёт измерение) либо в
    // STOPPED
    device_control_invalidate(c, c->running ? DEVICE_FILTER_WARMUP : DEVICE_STOPPED);
}

// Атомарная проверка и применение записи в Holding-регистры Modbus
int device_write_holding(device_control_t *c, uint16_t addr, uint16_t count, const uint16_t *values)
{
    // Создаём локальную копию рабочей конфигурации для транзакционной проверки
    device_config_t next = c->config;
    uint16_t map[DEVICE_HOLDING_COUNT]; // Временная копия массива Holding-регистров
    unsigned i; // Счётчик записываемых регистров

    // Запрещаем менять настройки, пока выполняется фоновая сервисная команда (например, сохранение
    // во Flash)
    if (c->commands) return MB_DEVICE_BUSY;
    // Проверяем, что диапазон записи не пуст и не выходит за границы массива настроек
    if (!count || (uint32_t)addr + count > DEVICE_HOLDING_COUNT) return error(c, MB_ILLEGAL_ADDRESS);
    memcpy(map, c->holding, sizeof(map)); // Копируем текущие Holding-регистры во временный буфер

    for (i = 0; i < count; ++i) { // Поочерёдно проверяем и декодируем каждое пришедшее слово
        uint16_t a = (uint16_t)(addr + i); // Вычисляем адрес текущего модифицируемого регистра
        // Проверяем допустимость сырого значения и записываем его во временный массив map
        int result = register_validateAndSet(map, a, values[i]);
        // Если значение не прошло проверку, отменяем всю транзакцию и возвращаем код ошибки Modbus
        if (result) return error(c, result);
        // Пересчитываем сырое значение регистра в физическую величину во временной структуре next
        register_updateFromModbus(&next, map, a);
    }

    // Выполняем комплексную проверку целостности всей итоговой конфигурации
    if (!device_config_valid(&next)) return error(c, MB_ILLEGAL_VALUE);
    // Запрещаем менять число активных опрашиваемых каналов на лету без остановки измерений
    if (c->run_requested && next.active_channels != c->config.active_channels) return MB_DEVICE_BUSY;
    // Если изменился любой настроечный регистр кроме регистров подстановки времени RTC, сбрасываем
    // историю фильтров
    if (memcmp(map, c->holding, HR_RTC_LO * sizeof(map[0]))) changed(c);
    c->config = next; // Атомарно применяем проверенную конфигурацию как рабочую
    memcpy(c->holding, map, sizeof(map)); // Обновляем основной массив Holding-регистров
    return 0; // Запись Holding-регистров успешно выполнена
}

// Атомарная проверка и применение записи битовых флагов и команд в пространство Coils
int device_write_coils(device_control_t *c, uint16_t addr, uint16_t count, const uint16_t *values)
{
    device_config_t next = c->config; // Локальная копия настроек для безопасного изменения флагов фильтров
    uint16_t commands = 0; // Накопитель битов сервисных команд, запрошенных в этой транзакции
    uint8_t run = c->run_requested; // Предварительное состояние флага запуска измерений
    uint8_t stop = 0; // Признак того, что в транзакции был явно запрошен останов измерений
    unsigned i; // Индекс цикла по записываемым флагам Coils

    // Проверяем выход адреса за пределы количества определённых Coils
    if (!count || (uint32_t)addr + count > COIL_COUNT) return error(c, MB_ILLEGAL_ADDRESS);
    // Пока идёт выполнение сервисной команды, разрешаем только одиночную команду экстренного
    // останова (RUN=0)
    if (c->commands && !(addr == COIL_RUN && count == 1 && values[0] == 0)) return MB_DEVICE_BUSY;

    for (i = 0; i < count; ++i) { // Обрабатываем каждый записываемый бит
        unsigned a = addr + i; // Текущий адрес Coil
        // Значение в массиве после распаковки должно быть строго булевым (0 или 1)
        if (values[i] > 1) return error(c, MB_ILLEGAL_VALUE);
        switch (a) { // Распределяем действие по назначению конкретного Coil
        case COIL_RUN: // Управление запуском и остановом опроса датчиков
            run = (uint8_t)values[i]; // Обновляем целевое состояние работы
            if (!run) stop = 1; // Если записан 0, выставляем запрос на принудительный останов автомата
            break;
        case COIL_SAVE: // Запрос сохранения настроек во Flash
            if (values[i]) commands |= DEVICE_CMD_SAVE; // При записи единицы взводим бит команды сохранения
            break;
        case COIL_CLEAR: // Запрос сброса ошибок датчиков и карты регистров
            // При записи единицы взводим бит команды очистки ошибок
            if (values[i]) commands |= DEVICE_CMD_CLEAR;
            break;
        case COIL_RTC: // Запрос записи подготовленного времени в часы RTC
            if (values[i]) commands |= DEVICE_CMD_RTC; // При записи единицы взводим бит команды установки RTC
            break;
        case COIL_REBOOT: // Запрос перезагрузки микроконтроллера
            if (values[i]) commands |= DEVICE_CMD_REBOOT; // При записи единицы взводим бит перезагрузки
            break;
        case COIL_DEFAULTS: // Запрос сброса настроек к заводским значениям
            // При записи единицы взводим бит восстановления настроек по умолчанию
            if (values[i]) commands |= DEVICE_CMD_DEFAULTS;
            break;
        case COIL_MEDIAN: // Включение/выключение медианного фильтра
            // Сохраняем новое состояние флага во временной копии конфигурации
            next.use_median = (uint8_t)values[i];
            break;
        case COIL_AVERAGE: // Включение/выключение скользящего среднего
            next.use_average = (uint8_t)values[i]; // Сохраняем новое состояние флага усреднения
            break;
        case COIL_CALIBRATION: // Включение/выключение индивидуальной калибровки (b и k)
            next.use_calibration = (uint8_t)values[i]; // Сохраняем новое состояние флага калибровки
            break;
        default: // На случай неописанного адреса
            return error(c, MB_ILLEGAL_ADDRESS); // Возвращаем ошибку недопустимого адреса
        }
    }

    // Классическая битовая проверка: если взведено больше одного бита команды одновременно,
    // возвращаем ошибку
    if (commands && (commands & (commands - 1u))) return error(c, MB_ILLEGAL_VALUE);
    // Сервисные команды разрешено запускать только при остановленных измерениях (RUN == 0)
    if (commands && run) return MB_DEVICE_BUSY;
    if (commands == DEVICE_CMD_RTC) { // Если запрошена установка часов реального времени
        device_calendar_t calendar; // Временная структура календаря для проверки даты
        // Проверяем, что записанный ранее штамп времени входит в диапазон 2000..2099 годов
        if (device_epoch_to_calendar(next.rtc_pending, &calendar)) return error(c, MB_ILLEGAL_VALUE);
    }
    if (next.use_median != c->config.use_median || next.use_average != c->config.use_average ||
        // Если изменился хотя бы один флаг режима фильтрации или калибровки, сбрасываем накопленные
        // окна фильтров
        next.use_calibration != c->config.use_calibration) changed(c);

    c->config = next; // Публикуем обновлённые флаги обработки в рабочую конфигурацию
    c->run_requested = run; // Сохраняем целевое состояние запуска/останова
    c->commands |= commands; // Передаём сервисную команду на исполнение в главный цикл
    c->stop_requested |= stop; // Выставляем флаг необходимости останова датчиков
    return 0; // Запись Coils успешно завершена
}

// Маршрутизатор запросов записи от драйвера Modbus к обработчикам Coils и Holding Registers
int device_control_write(void *ctx, unsigned space, uint16_t addr, uint16_t count, const uint16_t *values)
{
    switch (space) { // Проверяем, к какому адресному пространству обращается мастер
    // Перенаправляем запись битов в функцию работы с Coils
    case MB_COILS: return device_write_coils(ctx, addr, count, values);
    // Перенаправляем запись слов в функцию работы с Holding-регистрами
    case MB_HOLDING: return device_write_holding(ctx, addr, count, values);
    // Попытка записи в Input Registers или Discrete Inputs возвращает ошибку адреса
    default: return error(ctx, MB_ILLEGAL_ADDRESS);
    }
}
