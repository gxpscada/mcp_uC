/**
 * @file pt100_ascii.h
 * @brief Совместимость со старым четырёхканальным ASCII-форматом, вне текущего Modbus-тракта.
 */
#ifndef PT100_ASCII_H
#define PT100_ASCII_H
#include "pt100_acquisition.h"
/* Four columns; invalid = -1, valid raw ADC = 0..32767. Returns byte length
 * excluding NUL, or zero if capacity is insufficient. */
size_t pt100_ascii_format(const pt100_frame_t *, char *, size_t);
#endif
