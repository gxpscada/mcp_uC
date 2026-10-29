#include "pt100_control.h"
#include "pt100_calendar.h"
#include "pt100_store.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static uint8_t reply[256];
static size_t reply_len;
static int tx_busy;
static int send_reply(void *unused,const uint8_t *p,size_t n)
{
    (void)unused;
    if (tx_busy) return -1;
    memcpy(reply,p,n); reply_len=n; return 0;
}
static size_t finish(uint8_t *q,size_t n)
{
    uint16_t crc=modbus_crc16(q,n); q[n++]=(uint8_t)crc; q[n++]=(uint8_t)(crc>>8); return n;
}
static void request(modbus_rtu_t *m,uint8_t *q,size_t n)
{
    n=finish(q,n); reply_len=0;
    modbus_rtu_receive(m,q,n,10); modbus_rtu_poll(m,10,0,send_reply);
    if (reply_len) assert(modbus_crc16(reply,reply_len)==0);
}
static void test_processing(void)
{
    pt100_filter_t f={0};
    pt100_processing_settings_t s={3,4,0,1,1,0,1000,4000,1};
    pt100_processed_t p;
    float t;
    unsigned i,w;
    assert(!pt100_cvd_temperature(100.0f,100,&t) && fabsf(t)<0.0001f);
    assert(!pt100_cvd_temperature(138.5055f,100,&t) && fabsf(t-100)<0.001f);
    assert(!pt100_cvd_temperature(60.25584f,100,&t) && fabsf(t+100)<0.002f);
    assert(!pt100_cvd_temperature(1385.055f,1000,&t) && fabsf(t-100)<0.001f);
    assert(pt100_cvd_temperature(-1,100,&t)<0);
    assert(pt100_cvd_temperature(NAN,100,&t)<0);
    for (i=0;i<3;++i) assert(pt100_process(&f,&s,(uint16_t)(8192+i%2),&p)==0);
    assert(pt100_process(&f,&s,8193,&p)==1 && p.adc_q1==16385);
    assert(pt100_process(&f,&s,8194,&p)==1 && p.adc_q1==16386);
    for (w=3;w<=7;w+=2) {
        pt100_filter_reset(&f); s.use_median=1; s.use_average=0; s.median_window=(uint8_t)w;
        for (i=0;i<w-1;++i) assert(!pt100_process(&f,&s,8192,&p));
        assert(pt100_process(&f,&s,32767,&p)==1 && p.adc_q1==16384);
    }
    for (w=2;w<=16;w*=2) {
        pt100_filter_reset(&f); s.use_median=0; s.use_average=1; s.average_window=(uint8_t)w;
        for (i=0;i<w-1;++i) assert(!pt100_process(&f,&s,(uint16_t)(8192+i%2),&p));
        assert(pt100_process(&f,&s,8193,&p)==1 && p.adc_q1==16385);
    }
    pt100_filter_reset(&f); s.use_average=0; s.adc_offset=-1;
    assert(pt100_process(&f,&s,8193,&p)==1 && p.corrected_q1==16384 && fabsf(p.temperature)<0.0001f);
    s.adc_offset=-32768;
    assert(pt100_process(&f,&s,0,&p)<0);
    s.adc_offset=32767;
    assert(pt100_process(&f,&s,32767,&p)<0);
    s.adc_offset=0; s.gain=2;
    assert(pt100_process(&f,&s,11346,&p)==1);
    assert(!pt100_cvd_temperature(p.resistance,1000,&t) && fabsf(p.temperature-2*t)<0.001f);
}
static void test_calendar(void)
{
    pt100_calendar_t c;
    uint32_t epoch;
    assert(!pt100_epoch_to_calendar(946684800u,&c));
    assert(c.year==2000 && c.month==1 && c.day==1 && c.weekday==6);
    assert(!pt100_calendar_to_epoch(&c,&epoch) && epoch==946684800u);
    c.year=2024; c.month=2; c.day=29; c.hour=23; c.minute=59; c.second=59;
    assert(!pt100_calendar_to_epoch(&c,&epoch));
    assert(!pt100_epoch_to_calendar(epoch+1,&c) && c.month==3 && c.day==1 && !c.hour);
    c.year=2023; c.month=2; c.day=29;
    assert(pt100_calendar_to_epoch(&c,&epoch)<0);
    assert(!pt100_epoch_to_calendar(4102444799u,&c));
    assert(!pt100_calendar_to_epoch(&c,&epoch) && epoch==4102444799u);
    assert(pt100_epoch_to_calendar(0,&c)<0);
}
static void test_modbus(void)
{
    modbus_rtu_t m;
    pt100_control_t c;
    uint8_t q[256]={1,3,0,0,0,1};
    uint16_t previous;
    size_t n;
    pt100_control_init(&c);
    modbus_rtu_init(&m,1,&c,pt100_control_read,pt100_control_write);
    n=finish(q,6); reply_len=0;
    modbus_rtu_receive(&m,q,3,10); modbus_rtu_poll(&m,10,0,send_reply); assert(!reply_len);
    modbus_rtu_receive(&m,q+3,n-3,11); modbus_rtu_poll(&m,11,0,send_reply);
    assert(reply_len==7 && reply[1]==3 && reply[4]==3 && !modbus_crc16(reply,reply_len));
    q[1]=6; q[3]=0x19; q[5]=3;
    request(&m,q,6); assert(reply[1]==0x86 && reply[2]==3 && c.config.holding[0x19]==4);
    q[5]=8; tx_busy=1; request(&m,q,6);
    assert(!reply_len && m.response_length==8 && c.config.holding[0x19]==8);
    previous=(uint16_t)m.requests;
    modbus_rtu_poll(&m,11,0,send_reply); assert(m.requests==previous);
    tx_busy=0; modbus_rtu_poll(&m,12,0,send_reply); assert(reply_len==8 && !m.response_length);
    q[3]=9; q[4]=0xff; q[5]=0xfe; request(&m,q,6); assert(c.config.holding[9]==65534);
    q[0]=0; q[3]=9; q[4]=0; q[5]=7; request(&m,q,6);
    assert(!reply_len && c.config.holding[9]==7); /* broadcast no response */
    q[0]=1; q[5]=9; n=finish(q,6); q[n-1]^=1; reply_len=0;
    modbus_rtu_receive(&m,q,n,20); modbus_rtu_poll(&m,20,0,send_reply);
    assert(!reply_len && c.config.holding[9]==7 && m.crc_errors==1);
    { uint8_t multi[]={1,16,0,0x18,0,3,6,0,123,0,4,0,4,0,0};
      previous=c.config.holding[0x18]; request(&m,multi,13);
      assert(reply[1]==0x90 && reply[2]==3 && c.config.holding[0x18]==previous);
      multi[12]=5; request(&m,multi,13);
      assert(reply[1]==16 && c.config.holding[0x18]==123 && c.config.holding[0x1a]==5); }
    { uint8_t coil[]={1,5,0,7,0,0,0,0};
      request(&m,coil,6); assert(!(c.config.flags&(1u<<7)));
      coil[3]=8; request(&m,coil,6); assert(!(c.config.flags&(1u<<8)));
      coil[3]=4; coil[4]=0xff; request(&m,coil,6); assert(c.commands==PT100_CMD_REBOOT);
      c.commands=0; }
    { uint16_t save=1, value=12, stop=0;
      assert(!pt100_control_write(&c,MB_COILS,1,1,&save));
      previous=c.config.holding[9];
      assert(pt100_control_write(&c,MB_HOLDING,9,1,&value)==MB_DEVICE_BUSY);
      assert(c.config.holding[9]==previous);
      assert(!pt100_control_write(&c,MB_COILS,0,1,&stop));
      c.commands=0; }
    { uint8_t read[]={1,4,0,0x15,0,19,0,0}; request(&m,read,6); assert(reply_len==43); }
    { uint8_t read[]={1,4,0,0x11,0,1,0,0}; request(&m,read,6); assert(reply[1]==0x84 && reply[2]==2); }
    { uint8_t other[]={2,6,0,9,0,99,0,0}; request(&m,other,6); assert(!reply_len && c.config.holding[9]==7); }
    { uint8_t unknown[]={1,0x41,0,0,0,0,0,0}; request(&m,unknown,6); assert(reply[1]==0xc1 && reply[2]==1); }
    modbus_rtu_receive(&m,q,3,100); modbus_rtu_poll(&m,100,0,send_reply);
    modbus_rtu_poll(&m,200,0,send_reply); assert(!m.request_length && m.timeouts==1);
    { uint8_t noise[MB_RX_SIZE]={0}; modbus_rtu_receive(&m,noise,sizeof(noise),300);
      modbus_rtu_poll(&m,300,0,send_reply); assert(m.overflows==1 && m.head==m.tail); }
    /* Thousands of bounded noisy USB chunks must not overflow ADU/response. */
    for (unsigned k=0;k<2000;++k) {
        uint8_t noise[23]; for(unsigned j=0;j<sizeof(noise);++j) noise[j]=(uint8_t)(k*37u+j*13u);
        modbus_rtu_receive(&m,noise,sizeof(noise),400+k);
        modbus_rtu_poll(&m,400+k,0,send_reply);
        assert(m.request_length<MB_ADU_MAX && m.response_length<=MB_ADU_MAX);
    }
}
static uint32_t memory[100];
static int pending;
static int program(void *unused,size_t offset,uint32_t word)
{
    (void)unused; assert(offset<100 && memory[offset]==0xffffffffu);
    memory[offset]=word; pending=1; return 0;
}
static int programmed(void *unused) { (void)unused; if(pending){pending=0;return 0;}return 1; }
static void drain(pt100_store_t *s) { unsigned n=0; while(s->state){pt100_store_poll(s); assert(++n<100);} }
static void test_store(void)
{
    pt100_store_t s={0};
    pt100_config_t a,b;
    memset(memory,0xff,sizeof(memory));
    s.memory=memory; s.word_count=100; s.begin_word=program; s.poll_word=programmed;
    pt100_config_defaults(&a); b=a;
    assert(!pt100_store_load(&s,&b));
    assert(!pt100_store_save(&s,&a)); drain(&s); assert(!s.error);
    b.holding[9]=123; assert(pt100_store_load(&s,&b) && b.holding[9]==0);
    a.holding[9]=42; assert(!pt100_store_save(&s,&a));
    pt100_store_poll(&s); pt100_store_poll(&s); /* power loss before commit */
    assert(pt100_store_load(&s,&b) && b.holding[9]==0 && s.next_slot==40);
    assert(!pt100_store_save(&s,&a)); drain(&s);
    assert(pt100_store_load(&s,&b) && b.holding[9]==42);
    memory[42]^=1; /* corrupt latest data, previous committed record survives */
    assert(pt100_store_load(&s,&b) && b.holding[9]==0);
    assert(!pt100_store_save(&s,&a)); drain(&s);
    assert(!pt100_store_save(&s,&a)); drain(&s);
    assert(pt100_store_save(&s,&a)<0 && s.error);
}
int main(void)
{
    test_processing(); test_calendar(); test_modbus(); test_store();
    puts("firmware: processing, calendar, Modbus/CRC/atomic writes, noisy RX, flash recovery passed");
    return 0;
}
