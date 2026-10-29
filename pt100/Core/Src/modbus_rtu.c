/**
 * @file modbus_rtu.c
 * @brief Ограниченный по памяти транспорт Modbus RTU ADU поверх USB CDC.
 */
#include "modbus_rtu.h"
#include <string.h>
/**
 * @brief Вычисляет CRC-16 Modbus с начальным значением 0xFFFF и полиномом 0xA001.
 * @param[in] data Последовательность байтов; должна быть доступна на длину n.
 * @param[in] n Число байтов, включая CRC, если проверяется уже принятый ADU.
 * @return CRC как число uint16_t. Для исправного полного пакета с CRC возвращается 0.
 * @note В Modbus CRC передаётся младшим байтом вперёд; регистры внутри PDU — старшим.
 */
uint16_t modbus_crc16(const uint8_t *data, size_t n)
{
    uint16_t crc = 0xffff;
    size_t i;
    for (i=0; i<n; ++i) {
        unsigned j;
        crc ^= data[i];
        for (j=0; j<8; ++j) crc = (uint16_t)((crc >> 1) ^ ((crc & 1u) ? 0xa001u : 0));
    }
    return crc;
}
static uint16_t be16(const uint8_t *p) { return (uint16_t)(((uint16_t)p[0]<<8)|p[1]); }
static void put16(uint8_t *p, uint16_t v) { p[0]=(uint8_t)(v>>8); p[1]=(uint8_t)v; }
/**
 * @brief Обнуляет транспорт и связывает его с прикладной картой регистров.
 * @param[out] m Контекст транспорта; должен оставаться доступным после возврата.
 * @param[in] address Собственный адрес устройства; приложение должно выбрать unicast-адрес 1..247.
 * @param[in] ctx Пользовательский контекст, передаваемый функциям read и write.
 * @param[in] read Обработчик чтения: заполняет массив uint16_t, возвращает 0 или код исключения Modbus.
 * @param[in] write Обработчик записи: принимает массив uint16_t, возвращает 0 или код исключения Modbus.
 * @pre m, read и write заданы. При инициализации приём из прерывания ещё не разрешён.
 * @details Для Coils/Discrete каждый элемент массива — отдельный логический 0/1,
 * для регистров — 16-битное число. Упаковку битов и порядок байтов выполняет транспорт.
 * Многорегистровую запись обработчик должен сначала проверить целиком и только затем применить.
 */
void modbus_rtu_init(modbus_rtu_t *m, uint8_t address, void *ctx, mb_read_fn read, mb_write_fn write)
{
    memset(m, 0, sizeof(*m)); m->address=address; m->context=ctx; m->read=read; m->write=write;
}
/**
 * @brief Копирует очередной USB-фрагмент в приёмное кольцо без разбора команд.
 * @param[in,out] m Инициализированный контекст транспорта.
 * @param[in] p Принятые байты; после возврата исходный буфер можно использовать повторно.
 * @param[in] n Размер фрагмента в байтах.
 * @param[in] ms Монотонное время получения фрагмента, мс; та же шкала, что у poll.
 * @details Допускается один producer в USB ISR и один consumer в главном цикле.
 * Границы USB-пакетов не считаются границами Modbus ADU. Если места не хватает,
 * фрагмент отклоняется целиком, растёт overflows и запрашивается сброс разбора.
 * @warning Нельзя одновременно вызывать приём из нескольких прерываний/потоков:
 * volatile не превращает кольцо в универсальную многопоточную очередь.
 */
void modbus_rtu_receive(modbus_rtu_t *m, const uint8_t *p, size_t n, uint32_t ms)
{
    uint16_t head=m->head;
    size_t i;
    if (n > MB_RX_SIZE-1u-(uint16_t)(head-m->tail)) {
        ++m->overflows; m->reset_requested=1; return;
    }
    for (i=0; i<n; ++i) m->ring[(head++) & (MB_RX_SIZE-1u)]=p[i];
    m->last_rx_ms=ms;
    m->head=head;
}
/**
 * @brief Запрашивает отбрасывание незавершённого обмена после отключения USB.
 * @param[in,out] m Контекст транспорта.
 * @details Безопасно вызывается из USB callback: только устанавливает флаг.
 * Очередь RX, частичный запрос и ожидающий отправки ответ очищаются следующим poll.
 * Уже выполненная прикладная команда при этом не отменяется.
 */
void modbus_rtu_disconnect(modbus_rtu_t *m) { m->reset_requested=1; }
static void execute(modbus_rtu_t *m)
{
    const uint8_t *q=m->request;
    uint8_t *r=m->response, fc=q[1];
    uint16_t values[125], address, count, crc;
    unsigned i, bytes, space;
    size_t n=0;
    int error=0, broadcast=q[0]==0;
    if (q[0]!=m->address && !broadcast) return;
    if (m->request_length < 8) return;
    address=be16(q+2); count=be16(q+4);
    r[0]=q[0]; r[1]=fc;
    ++m->requests;
    if (fc>=1 && fc<=4) {
        if (broadcast) return;
        /* Map bounds are much smaller than protocol limits. Up to 125
         * unpacked values fits the bounded stack buffer for all spaces. */
        if (!count || count>125 || (uint32_t)address+count>65536u) error=MB_ILLEGAL_VALUE;
        else {
            space=fc==1 ? MB_COILS : fc==2 ? MB_DISCRETE : fc==3 ? MB_HOLDING : MB_INPUT;
            error=m->read(m->context,space,address,count,values);
            if (!error) {
                bytes=fc<=2 ? (count+7u)/8u : count*2u;
                r[2]=(uint8_t)bytes; memset(r+3,0,bytes);
                for (i=0; i<count; ++i) {
                    if (fc<=2) r[3+i/8] |= (uint8_t)((values[i]!=0) << (i%8));
                    else put16(r+3+i*2,values[i]);
                }
                n=3u+bytes;
            }
        }
    } else if (fc==5 || fc==6) {
        if (fc==5 && count!=0 && count!=0xff00) error=MB_ILLEGAL_VALUE;
        else {
            values[0]=fc==5 ? (uint16_t)(count==0xff00) : count;
            error=m->write(m->context,fc==5 ? MB_COILS : MB_HOLDING,address,1,values);
            if (!error) { memcpy(r,q,6); n=6; }
        }
    } else if (fc==15 || fc==16) {
        bytes=fc==15 ? (count+7u)/8u : count*2u;
        if (!count || count>123 || q[6]!=bytes || m->request_length!=9u+bytes ||
            (uint32_t)address+count>65536u) error=MB_ILLEGAL_VALUE;
        else {
            for (i=0; i<count; ++i)
                values[i]=fc==15 ? (uint16_t)((q[7+i/8]>>(i%8))&1) : be16(q+7+i*2);
            error=m->write(m->context,fc==15 ? MB_COILS : MB_HOLDING,address,count,values);
            if (!error) { memcpy(r,q,6); n=6; }
        }
    } else error=MB_ILLEGAL_FUNCTION;
    if (broadcast) return;
    if (error) { r[1]=(uint8_t)(fc|0x80u); r[2]=(uint8_t)error; n=3; ++m->exceptions; }
    crc=modbus_crc16(r,n); r[n++]=(uint8_t)crc; r[n++]=(uint8_t)(crc>>8);
    m->response_length=(uint16_t)n;
}
/**
 * @brief Выполняет ограниченный шаг разбора Modbus и попытку отправки ответа.
 * @param[in,out] m Контекст транспорта; вызывать регулярно из главного цикла.
 * @param[in] ms Текущее монотонное время в миллисекундах.
 * @param[in] txctx Контекст транспортного обработчика send.
 * @param[in] send Возвращает 0, только если данные скопированы/приняты; ненулевое значение
 * означает занятость.
 * @details За вызов обрабатывается не более 256 входных байтов и не более одного запроса.
 * При занятом передатчике ответ сохраняется; запись регистров повторно не исполняется.
 * Неполный обмен сбрасывается после 100 мс без новых принятых данных. Пакет с неверным
 * CRC отбрасывается без ответа. Broadcast-запись с адресом 0 исполняется без ответа.
 * Поддержаны FC01/02/03/04/05/06/0F/10; неизвестный код разбирается как запрос длиной 8 байт.
 * @note Это сборка RTU ADU поверх USB CDC по длине и CRC. Интервалы UART t1.5/t3.5
 * здесь не измеряются; для физического UART нужен соответствующий транспортный адаптер.
 * Ограничения буфера: до 125 значений при чтении и до 123 при множественной записи,
 * включая битовые пространства. Прикладная карта может ограничивать диапазон сильнее.
 */
void modbus_rtu_poll(modbus_rtu_t *m, uint32_t ms, void *txctx,
                     int (*send)(void *, const uint8_t *, size_t))
{
    unsigned budget=MB_ADU_MAX;
    if (m->reset_requested) {
        m->reset_requested=0; m->tail=m->head;
        m->request_length=m->response_length=0;
    }
    if (m->response_length) {
        if (!send(txctx,m->response,m->response_length)) m->response_length=0;
        return;
    }
    if ((m->request_length || m->tail!=m->head) &&
        (uint32_t)(ms-m->last_rx_ms)>=100u) {
        m->request_length=0; m->tail=m->head; ++m->timeouts;
        return;
    }
    while (budget-- && m->tail!=m->head) {
        uint16_t expected=8, n;
        m->request[m->request_length++]=m->ring[m->tail & (MB_RX_SIZE-1u)];
        ++m->tail;
        n=m->request_length;
        if (n<2) continue;
        if (m->request[1]==15 || m->request[1]==16) {
            if (n<7) continue;
            expected=(uint16_t)(9u+m->request[6]);
        }
        if (expected>MB_ADU_MAX) { m->request_length=0; m->tail=m->head; ++m->overflows; return; }
        if (n==expected) {
            if (!modbus_crc16(m->request,n)) execute(m); else ++m->crc_errors;
            m->request_length=0;
            if (m->response_length && !send(txctx,m->response,m->response_length)) m->response_length=0;
            return;
        }
    }
}
