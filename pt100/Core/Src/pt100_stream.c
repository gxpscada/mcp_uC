/**
 * @file pt100_stream.c
 * @brief Ограниченная очередь старого ASCII-потока, сохранённая для прежних тестов.
 */
#include "pt100_stream.h"
/**
 * @brief Добавляет диагностическую ASCII-строку в ограниченную очередь.
 * @param[in,out] s Предварительно обнулённая очередь старого потокового режима.
 * @param[in] f Кадр для преобразования в четыре текстовых столбца.
 * @details При заполнении очереди или ошибке форматирования отбрасывается новый кадр
 * и увеличивается dropped. Уже ожидающие строки сохраняются. Вызывать только
 * из главного цикла; синхронизации с ISR нет. В текущем Modbus-приложении не используется.
 */
void pt100_stream_push(pt100_stream_t *s, const pt100_frame_t *f)
{
    unsigned index;
    size_t n;
    if (s->count == PT100_STREAM_DEPTH) { ++s->dropped; return; }
    index = (s->head + s->count) % PT100_STREAM_DEPTH;
    n = pt100_ascii_format(f, s->lines[index], PT100_LINE_SIZE);
    if (!n) { ++s->dropped; return; }
    s->lengths[index] = (uint16_t)n;
    ++s->count;
}
/**
 * @brief Пытается отправить одну накопленную ASCII-строку без ожидания готовности USB.
 * @param[in,out] s Очередь строк.
 * @param[in] ctx Пользовательский контекст передатчика.
 * @param[in] send Возвращает 0 после копирования/потребления данных; иначе строка остаётся в очереди.
 * @details При принятии строка удаляется и увеличивается accepted. Буфер может быть
 * переиспользован сразу после успеха, поэтому передатчик не должен сохранять ссылку
 * на него для отложенного чтения. Вызывать из того же главного цикла, что и push.
 */
void pt100_stream_poll(pt100_stream_t *s, void *ctx,
                       int (*send)(void *, const uint8_t *, size_t))
{
    if (s->count && send(ctx, (const uint8_t *)s->lines[s->head], s->lengths[s->head]) == 0) {
        s->head = (s->head + 1u) % PT100_STREAM_DEPTH;
        --s->count; ++s->accepted;
    }
}
