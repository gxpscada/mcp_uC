/**
 * @file pt100_control.h
 * @brief Прикладная карта Modbus, проверка настроек и согласованный снимок измерений.
 */
#ifndef PT100_CONTROL_H
#define PT100_CONTROL_H
#include "pt100_acquisition.h"
#include "pt100_processing.h"
#include "modbus_rtu.h"
enum { PT100_CMD_SAVE=1u<<1, PT100_CMD_CLEAR=1u<<2, PT100_CMD_RTC=1u<<3,
       PT100_CMD_REBOOT=1u<<4, PT100_CMD_DEFAULTS=1u<<5 };
/**
 * @brief Состояние прикладной карты. config — настройки, filters/processed — обработка каналов,
 * inputs — снимок регистров. run_requested отличается от фактического running. commands содержит
 * отложенное действие; stop_requested сохраняет даже повторный STOP. runtime_ms — время текущего
 * сеанса, frame_seconds/frame_milliseconds — метка последнего кадра.
 */
typedef struct {
    pt100_config_t config;
    pt100_filter_t filters[PT100_MAX_CHANNELS];
    pt100_processed_t processed[PT100_MAX_CHANNELS];
    uint16_t inputs[0x37];
    uint16_t error_code;
    uint8_t hardware_error, run_requested, running, config_changed, stop_requested;
    uint16_t commands;
    uint32_t runtime_ms, frame_seconds;
    uint16_t frame_milliseconds;
} pt100_control_t;
void pt100_config_defaults(pt100_config_t *);
int pt100_config_valid(const pt100_config_t *);
void pt100_control_init(pt100_control_t *);
void pt100_control_invalidate(pt100_control_t *, uint8_t status);
void pt100_control_frame(pt100_control_t *, const pt100_frame_t *);
int pt100_control_read(void *, unsigned, uint16_t, uint16_t, uint16_t *);
int pt100_control_write(void *, unsigned, uint16_t, uint16_t, const uint16_t *);
#endif
