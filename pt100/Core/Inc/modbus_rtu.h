/**
 * @file modbus_rtu.h
 * @brief Ограниченный по памяти транспорт Modbus RTU ADU поверх USB CDC.
 */
#ifndef MODBUS_RTU_H
#define MODBUS_RTU_H
#include <stddef.h>
#include <stdint.h>
#define MB_ADU_MAX 256u
#define MB_RX_SIZE 512u
enum { MB_COILS, MB_DISCRETE, MB_HOLDING, MB_INPUT };
enum { MB_ILLEGAL_FUNCTION=1, MB_ILLEGAL_ADDRESS=2, MB_ILLEGAL_VALUE=3,
       MB_DEVICE_FAILURE=4, MB_DEVICE_BUSY=6 };
typedef int (*mb_read_fn)(void *, unsigned, uint16_t, uint16_t, uint16_t *);
typedef int (*mb_write_fn)(void *, unsigned, uint16_t, uint16_t, const uint16_t *);
/**
 * @brief Один экземпляр транспорта. ISR публикует байты в ring через head, главный цикл забирает
 * через tail. request/response владеют буферами ADU; response_length удерживает неотправленный
 * ответ. Диагностика crc_errors/timeouts/requests/exceptions/overflows накапливается с init.
 * Контекст не копировать во время работы.
 */
typedef struct {
    volatile uint8_t ring[MB_RX_SIZE];
    volatile uint16_t head, tail;
    volatile uint8_t reset_requested;
    volatile uint32_t last_rx_ms, overflows;
    uint8_t request[MB_ADU_MAX], response[MB_ADU_MAX];
    uint16_t request_length, response_length;
    uint8_t address;
    uint32_t crc_errors, timeouts, requests, exceptions;
    void *context;
    mb_read_fn read;
    mb_write_fn write;
} modbus_rtu_t;
uint16_t modbus_crc16(const uint8_t *, size_t);
void modbus_rtu_init(modbus_rtu_t *, uint8_t, void *, mb_read_fn, mb_write_fn);
/* Single interrupt producer, single main-loop consumer. USB chunk boundaries
 * are NOT RTU frame boundaries. Length + CRC reassembly; 100ms partial timeout.
 * Serial RTU t1.5/t3.5 detection belongs to a UART adapter, not USB scheduling. */
void modbus_rtu_receive(modbus_rtu_t *, const uint8_t *, size_t, uint32_t);
void modbus_rtu_disconnect(modbus_rtu_t *);
/* send copies bytes and returns 0 if accepted, nonzero if busy. No wait loops. */
void modbus_rtu_poll(modbus_rtu_t *, uint32_t, void *,
                     int (*send)(void *, const uint8_t *, size_t));
#endif
