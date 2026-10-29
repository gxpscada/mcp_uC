/**
 * @file pt100_store.c
 * @brief Переносимый журнал конфигурации с CRC и последним маркером commit.
 */
#include "pt100_store.h"
#include "pt100_control.h"
#include <string.h>
#define STORE_MAGIC 0x50544332u
#define STORE_COMMIT 0x43464732u
static uint32_t checksum(const uint32_t *r)
{
    /* Canonical byte order independent of native host byte order. */
    uint8_t bytes[68];
    unsigned i;
    uint32_t crc;
    for (i=0;i<68;++i) bytes[i]=(uint8_t)(r[i/4]>>(8u*(i%4)));
    crc=modbus_crc16(bytes,sizeof(bytes));
    return crc | ((crc^0xffffu)<<16);
}
/**
 * @brief Сканирует журнал и восстанавливает последнюю полностью записанную допустимую конфигурацию.
 * @param[in,out] s Журнал с заданными memory и word_count; состояние записи и ошибки сбрасывается.
 * @param[out] out Конфигурация; остаётся прежней, если валидная запись не найдена.
 * @return 1, если конфигурация найдена, иначе 0.
 * @details Проверяются magic, версия, CRC и маркер commit. Оборванные и повреждённые
 * записи пропускаются, но занятые ими места повторно не используются. next_slot
 * указывает за последний непустой слот. Обычно out заранее содержит defaults.
 * @pre Вызывать до начала сохранений; оба указателя и область memory действительны.
 */
int pt100_store_load(pt100_store_t *s,pt100_config_t *out)
{
    size_t slot;
    int found=0;
    s->next_slot=0; s->state=0; s->error=0;
    for (slot=0;slot+PT100_STORE_WORDS<=s->word_count;slot+=PT100_STORE_WORDS) {
        uint32_t r[PT100_STORE_WORDS];
        unsigned i, used=0;
        pt100_config_t c;
        for (i=0;i<PT100_STORE_WORDS;++i) { r[i]=s->memory[slot+i]; used|=r[i]!=0xffffffffu; }
        if (!used) continue;
        s->next_slot=slot+PT100_STORE_WORDS;
        if (r[0]!=STORE_MAGIC || r[1]!=1 || r[18]!=STORE_COMMIT || r[17]!=checksum(r)) continue;
        for (i=0;i<PT100_HOLDING_COUNT;++i) c.holding[i]=(uint16_t)(r[2+i/2]>>(16u*(i%2)));
        c.flags=(uint16_t)(r[16]>>16);
        if (pt100_config_valid(&c)) { *out=c; found=1; }
    }
    return found;
}
/**
 * @brief Подготавливает снимок конфигурации для пошагового добавления в журнал.
 * @param[in,out] s Инициализированный и просканированный журнал с аппаратными callbacks.
 * @param[in] c Конфигурация для сохранения; после принятия копируется во внутренний record.
 * @retval 0 Сохранение принято; оно ещё не завершено.
 * @retval -1 Журнал занят, конфигурация неверна либо нет подходящего чистого слота.
 * @details Слот занимает 20 слов (80 байт). Данные и CRC программируются раньше commit;
 * последнее резервное слово остаётся стёртым. После обрыва питания незавершённая запись
 * не заменяет предыдущую. Стирания сектора нет: заполненный журнал требует внешнего обслуживания.
 * @see pt100_store_poll
 */
int pt100_store_save(pt100_store_t *s,const pt100_config_t *c)
{
    unsigned i;
    if (s->state || !pt100_config_valid(c)) return -1;
    if (s->next_slot+PT100_STORE_WORDS>s->word_count) { s->error=1; return -1; }
    for (i=0;i<PT100_STORE_WORDS;++i)
        if (s->memory[s->next_slot+i]!=0xffffffffu) { s->error=1; return -1; }
    memset(s->record,0,sizeof(s->record));
    s->record[0]=STORE_MAGIC; s->record[1]=1;
    for (i=0;i<PT100_HOLDING_COUNT;++i) s->record[2+i/2]|=(uint32_t)c->holding[i]<<(16u*(i%2));
    s->record[16]|=(uint32_t)c->flags<<16;
    s->record[17]=checksum(s->record); s->record[18]=STORE_COMMIT; s->record[19]=0xffffffffu;
    s->index=0; s->error=0; s->state=1;
    return 0;
}
/**
 * @brief Продвигает сохранение на один шаг: запуск слова либо проверка завершения.
 * @param[in,out] s Контекст журнала с действительными begin_word и poll_word.
 * @details begin_word получает смещение в 32-битных словах от memory и возвращает 0
 * при принятии. poll_word возвращает 0 при занятости, 1 при успехе, -1 при ошибке.
 * Каждое записанное слово проверяется чтением. Завершение определяется по state==0,
 * успех — дополнительно по error==0. После ошибки слот пропускается, чтобы не дописывать
 * поверх частично запрограммированной Flash. Вызывать регулярно из главного цикла.
 */
void pt100_store_poll(pt100_store_t *s)
{
    if (s->state==1) {
        if (s->begin_word(s->context,s->next_slot+s->index,s->record[s->index])) {
            s->error=1; s->state=0; s->next_slot+=PT100_STORE_WORDS;
        } else s->state=2;
    } else if (s->state==2) {
        int result=s->poll_word(s->context);
        if (!result) return;
        if (result<0 || s->memory[s->next_slot+s->index]!=s->record[s->index]) {
            s->error=1; s->state=0; s->next_slot+=PT100_STORE_WORDS;
        } else if (++s->index==19) { s->state=0; s->next_slot+=PT100_STORE_WORDS; }
        else s->state=1;
    }
}
