#ifndef DEVICE_STM32_H
#define DEVICE_STM32_H

#include "stm32f4xx_hal.h"
#include "device_acquisition.h"
#include "device_control.h"

// Структура аппаратной привязки одного физического датчика MAX31865 к периферии микроконтроллера STM32
typedef struct {
    SPI_HandleTypeDef *spi; // Указатель на дескриптор контроллера шины SPI в библиотеке HAL
    GPIO_TypeDef *cs_port; // Указатель на регистры порта GPIO для линии выбора кристалла (Chip Select)
    GPIO_TypeDef *drdy_port; // Указатель на регистры порта GPIO для линии сигнала готовности данных (DRDY)
    uint16_t cs_pin; // Битовая маска пина CS (константа вида GPIO_PIN_x)
    uint16_t drdy_pin; // Битовая маска пина DRDY (константа вида GPIO_PIN_x)
    uint32_t spi_timeout_ms; // Максимальное время ожидания завершения одной транзакции HAL_SPI в миллисекундах
} device_stm32_channel_t; // Имя типа аппаратного дескриптора канала

int device_stm32_transfer(void *, const uint8_t *, uint8_t *, size_t); // Прототип адаптера синхронного обмена по SPI с управлением пином CS
void device_stm32_drdy_reset(void);
int device_stm32_ready(void *); // Прототип адаптера чтения активного низкого уровня готовности на пине DRDY
uint32_t device_stm32_ticks(void *); // Прототип адаптера чтения 32-битного счётчика тактов ядра DWT->CYCCNT
void device_app_init(SPI_HandleTypeDef *); // Прототип главной функции инициализации прикладного уровня прошивки
void device_app_poll(void); // Прототип главной неблокирующей функции суперцикла приложения
void device_board_gpio_init(void); // Прототип функции настройки выводов GPIO для линий CS и DRDY всех датчиков платы
void device_app_usb_receive(const uint8_t *, size_t); // Прототип функции передачи принятых пакетов из прерывания USB CDC в буфер Modbus
void device_app_usb_reset(void); // Прототип функции сброса состояния транспорта при отключении кабеля USB

extern device_acquisition_t device_acquisition; // Внешнее объявление структуры состояния сборщика (доступна для просмотра в отладчике SWD/Live Watch)
extern device_frame_t device_last_frame; // Внешнее объявление структуры последнего прочитанного кадра измерений
extern device_control_t device_control; // Внешнее объявление структуры управления и карты регистров Modbus
extern modbus_ascii_t device_modbus; // Внешнее объявление структуры состояния и статистики транспорта Modbus ASCII
extern uint32_t device_tick_hz; // Внешнее объявление переменной тактовой частоты процессора в Гц для пересчёта тиков DWT в микросекунды
extern max31865_result_t device_init_status[DEVICE_CHANNELS]; // Внешнее объявление массива результатов инициализации каждого датчика MAX31865

#endif
