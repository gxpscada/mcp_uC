#ifndef TEST_USB_H
#define TEST_USB_H
#include <stdint.h>
#define USBD_OK 0
uint8_t CDC_Transmit_FS(uint8_t *,uint16_t);
uint8_t CDC_TransmitBusy_FS(void);
#endif
