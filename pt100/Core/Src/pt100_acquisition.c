/**
 * @file pt100_acquisition.c
 * @brief Сбор кадров от нескольких MAX31865 по DRDY с ограниченным ожиданием.
 */
#include "pt100_acquisition.h"
#include <string.h>
/**
 * @brief Проверяет DRDY и при готовности либо тайм-ауте формирует общий кадр каналов.
 * @param[in,out] a Состояние сбора: каналы, настройки, callbacks и диагностические счётчики.
 * @param[in] now_ms Текущее монотонное время в миллисекундах, например HAL_GetTick().
 * @param[out] f Новый кадр; обновляется только при возврате 1.
 * @retval 1 Кадр сформирован, в том числе кадр с ошибками или неготовыми каналами.
 * @retval 0 Кадр ещё не готов либо базовые аргументы/настройки неверны.
 * @pre a предварительно обнулён; заданы ticks, timeout_ms и channel_count от 1 до 8.
 * Для включённых каналов настроены device и ready. last_frame_ms задаётся при запуске.
 * @details WAIT_ALL ждёт все выбранные каналы. TRIGGER_FIRST ориентируется именно
 * на канал 0; остальные может прочитать до готовности, сохраняя статус NOT_READY.
 * По тайм-ауту ожидание заканчивается, чтобы отказ одного датчика не останавливал систему.
 * Метка RTC снимается перед последовательным чтением SPI и относится к кадру в целом,
 * а не к точному моменту преобразования каждого независимого MAX31865.
 * @note Вызывать из главного цикла, не из ISR. Ожидания DRDY внутри нет, но SPI-callback
 * синхронный и должен иметь ограниченное время выполнения. DWT-интервалы выражены
 * в тиках; измеряемый интервал должен быть меньше периода переполнения счётчика.
 */
int pt100_acquisition_poll(pt100_acquisition_t *a, uint32_t now_ms, pt100_frame_t *f)
{
    unsigned i;
    uint8_t mask = 0;
    uint32_t start;
    int timed_out;
    unsigned count;
    uint8_t expected;
    if (!a || !f || !a->ticks || !a->settings.timeout_ms) return 0;
    count = a->settings.channel_count;
    if (!count || count > PT100_MAX_CHANNELS) return 0;
    expected = (uint8_t)((1u << count) - 1u);
    for (i = 0; i < count; ++i) {
        pt100_channel_t *c = &a->channel[i];
        if (c->enabled && c->ready && c->ready(c->ready_context)) {
            mask |= (uint8_t)(1u << i);
            if (!(a->observed_mask & (1u << i))) {
                a->ready_ticks[i] = a->ticks(a->clock_context);
                if (!a->observed_mask) a->first_ready_ticks = a->ready_ticks[i];
                a->observed_mask |= (uint8_t)(1u << i);
            }
        }
    }
    timed_out = (uint32_t)(now_ms - a->last_frame_ms) >= a->settings.timeout_ms;
    if (!timed_out && (a->settings.trigger == PT100_TRIGGER_FIRST ?
                      !(mask & 1u) : mask != expected)) return 0;
    start = a->ticks(a->clock_context);
    memset(f, 0, sizeof(*f));
    if (a->timestamp) f->rtc_valid=(uint8_t)(a->timestamp(&f->utc_seconds,&f->milliseconds)==0);
    f->sequence = ++a->sequence;
    f->ready_mask = mask;
    f->period_ticks = f->sequence == 1 ? 0 : start - a->last_frame_ticks;
    if (timed_out) ++a->timeout_frames;
    for (i = count; i < PT100_MAX_CHANNELS; ++i)
        f->channel[i].status = PT100_STOPPED;
    for (i = 0; i < count; ++i) {
        pt100_channel_t *c = &a->channel[i];
        pt100_channel_data_t *out = &f->channel[i];
        out->read_offset_ticks = a->ticks(a->clock_context) - start;
        if (mask & (1u << i)) {
            out->ready_offset_ticks = a->ready_ticks[i] - a->first_ready_ticks;
            if (out->ready_offset_ticks > f->ready_span_ticks)
                f->ready_span_ticks = out->ready_offset_ticks;
        }
        if (!c->enabled) { out->status = PT100_INIT_ERROR; continue; }
        if (!(mask & (1u << i))) {
            out->status = PT100_NOT_READY;
            /* First-channel experiment can read stale data. Keep NOT_READY. */
            if (a->settings.trigger != PT100_TRIGGER_FIRST) continue;
        }
        if (max31865_read(c->device, &out->sample) != MAX31865_OK) {
            out->status |= PT100_BUS_ERROR; ++a->bus_errors[i];
        } else if (out->sample.rtd_fault || out->sample.faults) {
            out->status |= PT100_SENSOR_FAULT; ++a->sensor_errors[i];
        }
    }
    f->read_ticks = a->ticks(a->clock_context) - start;
    if (f->read_ticks > a->max_read_ticks) a->max_read_ticks = f->read_ticks;
    a->last_frame_ticks = start; a->last_frame_ms = now_ms;
    a->observed_mask = 0;
    return 1;
}
