#include "pt100_stm32.h"
#include "pt100_flash.h"
#include "pt100_rtc.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
GPIO_TypeDef gpio_a={0},gpio_b={1};
FakeCore fake_core;
FakeDwt fake_dwt;
uint32_t SystemCoreClock=96000000;
static uint32_t ms,epoch=1760000000;
static int selected=-1,fail_channel=-1,usb_busy,rebooted;
static uint8_t config[8],ready[8];
static uint32_t flash[100];
static const uint16_t cs[]={8,1,2,1024,4,2048,1,2};
static const uint16_t drdy[]={4,4096,8192,16384,32768,256,512,1024};
static const unsigned cs_port[]={0,1,1,1,1,1,0,0};
static const unsigned drdy_port[]={0,1,1,1,1,0,0,0};
uint32_t HAL_GetTick(void) { return ms; }
void HAL_GPIO_WritePin(GPIO_TypeDef *p,uint16_t pin,unsigned value)
{
    unsigned i;
    for(i=0;i<8;++i) if(cs[i]==pin && cs_port[i]==p->id) {
        if(value) { if(selected==(int)i) selected=-1; }
        else { assert(selected==-1); selected=(int)i; }
    }
}
int HAL_GPIO_ReadPin(GPIO_TypeDef *p,uint16_t pin)
{
    unsigned i;
    for(i=0;i<8;++i) if(drdy[i]==pin && drdy_port[i]==p->id) return ready[i] ? 0:1;
    return 1;
}
void HAL_GPIO_Init(GPIO_TypeDef *p,GPIO_InitTypeDef *g) { (void)p; (void)g; }
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *spi,uint8_t *tx,uint8_t *rx,uint16_t n,uint32_t timeout)
{
    (void)spi; assert(timeout==2); assert(selected>=0 && selected<8);
    if(selected==fail_channel) return HAL_ERROR;
    memset(rx,0,n);
    if(tx[0]==0x80) config[selected]=tx[1]&0xfd;
    else if(tx[0]==0) rx[1]=config[selected];
    else { assert(tx[0]==1 && n==8); rx[1]=0x40; ready[selected]=0; }
    return HAL_OK;
}
void NVIC_SystemReset(void) { ++rebooted; }
uint8_t CDC_Transmit_FS(uint8_t *p,uint16_t n) { assert(n>=5 && !modbus_crc16(p,n)); return (uint8_t)usb_busy; }
uint8_t CDC_TransmitBusy_FS(void) { return (uint8_t)usb_busy; }
void pt100_rtc_init(void) {}
void pt100_rtc_poll(void) {}
int pt100_rtc_busy(void) { return 0; }
volatile uint8_t pt100_rtc_error;
int pt100_rtc_set(uint32_t value) { epoch=value; return 0; }
int pt100_rtc_get(uint32_t *e,uint16_t *fraction) { *e=epoch; *fraction=(uint16_t)(ms%1000); return 0; }
static int write_flash(void *ctx,size_t offset,uint32_t word) { (void)ctx; flash[offset]=word; return 0; }
static int flash_done(void *ctx) { (void)ctx; return 1; }
void pt100_flash_init(pt100_store_t *s)
{
    s->memory=flash; s->word_count=100; s->begin_word=write_flash; s->poll_word=flash_done;
}
static void steps(unsigned count)
{
    while(count--) {
        unsigned i;
        ++ms; fake_dwt.CYCCNT+=96000;
        if(ms%21==0) for(i=0;i<8;++i) if(config[i]&0x40) ready[i]=1;
        pt100_app_poll(); assert(selected==-1);
    }
}
static void coil(uint8_t address,int value)
{
    uint8_t q[]={1,5,0,address,0,0,0,0};
    uint16_t crc;
    q[4]=value ? 0xff:0;
    crc=modbus_crc16(q,6); q[6]=(uint8_t)crc; q[7]=(uint8_t)(crc>>8);
    pt100_app_usb_receive(q,8); steps(1);
}
int main(void)
{
    SPI_HandleTypeDef spi={0};
    unsigned i;
    memset(flash,0xff,sizeof(flash));
    pt100_board_gpio_init(); pt100_app_init(&spi);
    for(i=0;i<PT100_BOARD_CHANNEL_COUNT;++i) assert(config[i]==1); /* cold boot no bias */
    assert(!pt100_control.run_requested);
    coil(0,1); steps(300);
    assert(pt100_control.running && pt100_control.runtime_ms>=300);
    for(i=0;i<PT100_DEFAULT_CHANNELS;++i) assert(config[i]==0xc1);
    assert(pt100_last_frame.rtc_valid && pt100_last_frame.utc_seconds==epoch);
    assert(pt100_control.inputs[0x20]==8192 && pt100_control.processed[0].valid);
    usb_busy=1; coil(0,0); steps(20);
    assert(!pt100_control.running);
    for(i=0;i<PT100_BOARD_CHANNEL_COUNT;++i) assert(config[i]==1); /* STOP despite blocked USB */
    assert(pt100_control.inputs[0x20]==0xffff && pt100_control.inputs[0x15]==0x8000);
    usb_busy=0; steps(2);
    coil(1,1); steps(100); assert(!pt100_control.commands && flash[18]==0x43464732);
    coil(0,1); steps(100);
    fail_channel=1; coil(0,0); steps(20);
    assert(!pt100_control.running && pt100_control.hardware_error);
    assert(config[0]==1 && config[2]==1); /* failure cannot prevent other STOP writes */
    fail_channel=-1; coil(2,1); steps(20); assert(!pt100_control.commands);
    for(i=0;i<PT100_BOARD_CHANNEL_COUNT;++i) assert(config[i]==1);
    config[1]=0xc1; /* independently restarted chip while controller is idle */
    coil(0,0); steps(20); assert(config[1]==1); /* repeated STOP is effective */
    usb_busy=1; coil(4,1); steps(10); assert(!rebooted);
    usb_busy=0; steps(2); assert(rebooted);
    puts("app: boot STOP, FreeRun, timestamp, STOP while USB busy, SPI failure isolation, save and deferred reboot passed");
    return 0;
}
