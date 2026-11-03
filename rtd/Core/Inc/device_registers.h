#ifndef DEVICE_REGISTERS_H
#define DEVICE_REGISTERS_H

#include "device_types.h"
#include "register_map.h" // П

int register_validateAndSet(uint16_t *map, uint16_t address, uint16_t value); // Прототип проверки сырого значения и его записи в массив Holding-регистров (0 — ОК, 2 — ошибка адреса, 3 — ошибка значения)
void register_updateFromModbus(device_config_t *config, const uint16_t *map, uint16_t address); // Прототип перевода одного проверенного регистра из формата Modbus в физическое поле структуры config
int register_set2modbus(const device_config_t *config, uint16_t *map, uint16_t address); // Прототип обратного масштабирования и упаковки одного физического параметра в Holding-регистр
int registers_encode(const device_config_t *config, uint16_t *map); // Прототип атомарной выгрузки всей структуры конфигурации в массив Holding-регистров
int registers_decode(device_config_t *config, const uint16_t *map); // Прототип атомарной проверки и распаковки массива Holding-регистров в структуру конфигурации
void device_config_defaults(device_config_t *config); // Прототип заполнения конфигурации заводскими параметрами по умолчанию
int device_config_valid(const device_config_t *config); // Прототип полной проверки целостности и представимости всех параметров конфигурации

#endif
