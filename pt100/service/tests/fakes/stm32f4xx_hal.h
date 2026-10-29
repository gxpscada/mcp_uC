#ifndef TEST_HAL_H
#define TEST_HAL_H
#include <stdint.h>
typedef struct { unsigned id; } GPIO_TypeDef;
typedef struct { unsigned unused; } SPI_HandleTypeDef;
typedef struct { uint32_t Pin,Mode,Pull,Speed; } GPIO_InitTypeDef;
typedef struct { uint32_t DEMCR; } FakeCore;
typedef struct { uint32_t CYCCNT,CTRL; } FakeDwt;
extern GPIO_TypeDef gpio_a,gpio_b;
extern FakeCore fake_core;
extern FakeDwt fake_dwt;
extern uint32_t SystemCoreClock;
#define GPIOA (&gpio_a)
#define GPIOB (&gpio_b)
#define GPIO_PIN_0 (1u<<0)
#define GPIO_PIN_1 (1u<<1)
#define GPIO_PIN_2 (1u<<2)
#define GPIO_PIN_3 (1u<<3)
#define GPIO_PIN_8 (1u<<8)
#define GPIO_PIN_9 (1u<<9)
#define GPIO_PIN_10 (1u<<10)
#define GPIO_PIN_11 (1u<<11)
#define GPIO_PIN_12 (1u<<12)
#define GPIO_PIN_13 (1u<<13)
#define GPIO_PIN_14 (1u<<14)
#define GPIO_PIN_15 (1u<<15)
#define GPIO_PIN_SET 1
#define GPIO_PIN_RESET 0
#define GPIO_MODE_OUTPUT_PP 1
#define GPIO_MODE_INPUT 2
#define GPIO_PULLUP 1
#define GPIO_SPEED_FREQ_HIGH 1
#define CoreDebug (&fake_core)
#define DWT (&fake_dwt)
#define CoreDebug_DEMCR_TRCENA_Msk 1u
#define DWT_CTRL_CYCCNTENA_Msk 1u
#define __HAL_RCC_GPIOA_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOB_CLK_ENABLE() ((void)0)
typedef enum { HAL_OK, HAL_ERROR } HAL_StatusTypeDef;
uint32_t HAL_GetTick(void);
void HAL_GPIO_WritePin(GPIO_TypeDef *,uint16_t,unsigned);
int HAL_GPIO_ReadPin(GPIO_TypeDef *,uint16_t);
void HAL_GPIO_Init(GPIO_TypeDef *,GPIO_InitTypeDef *);
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *,uint8_t *,uint8_t *,uint16_t,uint32_t);
void NVIC_SystemReset(void);
#endif
