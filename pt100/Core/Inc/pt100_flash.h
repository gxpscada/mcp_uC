/**
 * @file pt100_flash.h
 * @brief Аппаратный адаптер журнала для выделенного сектора Flash STM32F4.
 */
#ifndef PT100_FLASH_H
#define PT100_FLASH_H
#include "pt100_store.h"
void pt100_flash_init(pt100_store_t *);
#endif
