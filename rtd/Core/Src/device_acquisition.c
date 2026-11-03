#include "device_acquisition.h"
#include <string.h>

// Неблокирующий опрос готовности всех активных каналов и чтение кадра по SPI
int device_acquisition_poll(device_acquisition_t *a, uint32_t now_ms, device_frame_t *f)
{
    unsigned i; // Индекс цикла обхода каналов
    uint8_t mask = 0; // Битовая маска текущей готовности каналов (бит i = 1, если DRDY i-го канала активен)
    uint32_t start; // Метка времени DWT (в тактах CPU) начала чтения кадра по SPI
    int timed_out; // Флаг истечения максимального времени ожидания готовности кадра
    unsigned count; // Количество активных опрашиваемых каналов из конфигурации
    uint8_t expected; // Ожидаемая битовая маска, при которой готовы все выбранные каналы

    // Выходим, если не переданы структуры, не задан callback таймера или нулевой таймаут
    if (!a || !f || !a->ticks || !a->settings.timeout_ms) return 0;
    count = a->settings.channel_count; // Считываем заданное число активных каналов
    // Проверяем, что число каналов входит в допустимый диапазон 1..DEVICE_MAX_CHANNELS
    if (!count || count > DEVICE_MAX_CHANNELS) return 0;
    // Формируем маску из count младших единиц (например, для 4 каналов: (1<<4)-1 = 0x0F)
    expected = (uint8_t)((1u << count) - 1u);

    for (i = 0; i < count; ++i) { // Проходим по всем активным каналам для проверки их выводов DRDY
        device_channel_t *c = &a->channel[i]; // Берём указатель на структуру текущего канала i
        // Если канал включён, имеет callback опроса и линия DRDY показывает готовность
        if (c->enabled && c->ready && c->ready(c->ready_context)) {
            mask |= (uint8_t)(1u << i); // Выставляем i-й бит в текущей маске готовности
            // Если готовность этого канала в рамках текущего кадра замечена впервые
            if (!(a->observed_mask & (1u << i))) {
                a->ready_ticks[i] = a->ticks(a->clock_context); // Засекаем момент появления DRDY в тактах DWT
                // Если это самый первый готовый канал в кадре, фиксируем его время как базовое
                if (!a->observed_mask) a->first_ready_ticks = a->ready_ticks[i];
                // Помечаем канал в накопительной маске, чтобы не перезатереть метку времени на
                // следующем цикле опроса
                a->observed_mask |= (uint8_t)(1u << i);
            }
        }
    }

    // Проверяем превышение таймаута (беззнаковое вычитание корректно при переполнении HAL_GetTick)
    timed_out = (uint32_t)(now_ms - a->last_frame_ms) >= a->settings.timeout_ms;
    // Если таймаут не вышел и условие запуска (готов канал 0 либо готовы все каналы) не выполнено —
    // ждём дальше
    if (!timed_out && (a->settings.trigger == DEVICE_TRIGGER_FIRST ? !(mask & 1u) : mask != expected)) return 0;

    // Фиксируем такты DWT перед стартом последовательного чтения микросхем по SPI
    start = a->ticks(a->clock_context);
    memset(f, 0, sizeof(*f)); // Полностью очищаем структуру выходного кадра перед заполнением новыми данными
    // Запрашиваем метку времени RTC; если функция вернула 0, ставим флаг валидности времени
    if (a->timestamp) f->rtc_valid = (uint8_t)(a->timestamp(&f->utc_seconds, &f->milliseconds) == 0);
    f->sequence = ++a->sequence; // Увеличиваем сквозной счётчик собранных кадров и записываем номер в кадр
    // Сохраняем в кадр битовую маску каналов, которые успели выставить DRDY до начала чтения
    f->ready_mask = mask;
    // Вычисляем период между кадрами в тактах CPU (для первого кадра период равен 0)
    f->period_ticks = f->sequence == 1 ? 0 : start - a->last_frame_ticks;
    // Считаем кадры, собранные по тайм-ауту.
    if (timed_out) ++a->timeout_frames;

    for (i = count; i < DEVICE_MAX_CHANNELS; ++i) // Обходим все неиспользуемые (резервные) каналы выше count
        f->channel[i].status = DEVICE_STOPPED; // Явно помечаем отключённые каналы статусом STOPPED

    for (i = 0; i < count; ++i) { // Последовательно опрашиваем по SPI все активные каналы
        device_channel_t *c = &a->channel[i]; // Указатель на управляющую структуру канала
        // Указатель на ячейку результатов канала в выходном кадре
        device_channel_data_t *out = &f->channel[i];
        // Записываем задержку начала чтения этого канала относительно старта сборки кадра
        out->read_offset_ticks = a->ticks(a->clock_context) - start;
        if (mask & (1u << i)) { // Если данный канал выставил сигнал DRDY
            // Считаем разбег времени готовности относительно самого быстрого канала в кадре
            out->ready_offset_ticks = a->ready_ticks[i] - a->first_ready_ticks;
            // Если этот разбег больше текущего максимума по кадру
            if (out->ready_offset_ticks > f->ready_span_ticks)
                // Обновляем максимальную ширину окна разброса готовности DRDY
                f->ready_span_ticks = out->ready_offset_ticks;
        }
        if (!c->enabled) { // Если канал не прошёл инициализацию при старте
            out->status = DEVICE_INIT_ERROR; // Записываем статус ошибки инициализации
            continue; // Пропускаем обращение по SPI к неисправному каналу
        }
        if (!(mask & (1u << i))) { // Если к моменту чтения канал не выставил сигнал готовности DRDY
            out->status = DEVICE_NOT_READY; // Помечаем данные канала как неготовые
            // В штатном режиме (ожидание всех) пропускаем SPI-чтение неготового датчика
            if (a->settings.trigger != DEVICE_TRIGGER_FIRST) continue;
        }
        // Читаем регистры АЦП и ошибок из MAX31865 по SPI; проверяем успешность обмена
        if (max31865_read(c->device, &out->sample) != MAX31865_OK) {
            out->status |= DEVICE_BUS_ERROR; // При сбое транзакции SPI добавляем флаг ошибки шины
            ++a->bus_errors[i]; // Увеличиваем счётчик ошибок SPI для данного канала
        // Если SPI-обмен прошёл, но сама микросхема MAX31865 сообщила об обрыве/КЗ
        // термосопротивления
        } else if (out->sample.sensor_fault || out->sample.faults) {
            out->status |= DEVICE_SENSOR_FAULT; // Выставляем программный флаг аппаратной аварии датчика
            ++a->sensor_errors[i]; // Инкрементируем счётчик ошибок чувствительного элемента по каналу
        }
    }

    // Вычисляем суммарное время чтения всех каналов по SPI в тактах CPU
    f->read_ticks = a->ticks(a->clock_context) - start;
    // Обновляем пиковое (наихудшее) время чтения SPI за всё время работы
    if (f->read_ticks > a->max_read_ticks) a->max_read_ticks = f->read_ticks;
    a->last_frame_ticks = start; // Сохраняем метку DWT текущего кадра для расчёта периода следующего кадра
    a->last_frame_ms = now_ms; // Обновляем время формирования кадра в мс для перезапуска отсчёта таймаута
    a->observed_mask = 0; // Сбрасываем маску замеченных сигналов DRDY для нового цикла накопления
    return 1; // Возвращаем 1 — новый кадр полностью собран и готов к математической обработке
}
