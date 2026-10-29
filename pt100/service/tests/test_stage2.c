#include "pt100_stream.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {
    uint8_t config, fault, bit;
    uint16_t raw;
    int fail, ready, mismatch, reads;
} mock_t;
static uint32_t counter;
static uint32_t ticks(void *unused) { (void)unused; counter += 100; return counter; }
static int ready(void *ctx) { return ((mock_t *)ctx)->ready; }
static int transfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t n)
{
    mock_t *m = ctx;
    memset(rx, 0, n);
    if (m->fail) return -1;
    if (tx[0] == 0x80) {
        assert(n == 2);
        m->config = tx[1] & 0xfdu;
        if (tx[1] & 2) { m->fault = 0; m->bit = 0; }
    } else if (tx[0] == 0) {
        assert(n == 2); rx[1] = m->config ^ (m->mismatch ? 1 : 0);
    } else {
        uint16_t word = (uint16_t)((m->raw << 1) | m->bit);
        assert(tx[0] == 1 && n == 8);
        rx[1] = (uint8_t)(word >> 8); rx[2] = (uint8_t)word;
        rx[7] = m->fault; m->ready = 0; ++m->reads;
    }
    return 0;
}
static int busy;
static char sent[32];
static int send_line(void *ctx, const uint8_t *line, size_t n)
{
    (void)ctx;
    if (busy) return -1;
    memcpy(sent, line, n); sent[n] = 0; return 0;
}
int main(void)
{
    mock_t mock[8] = {{0}};
    max31865_t dev[8] = {{0}};
    pt100_acquisition_t a = {0};
    pt100_frame_t f;
    pt100_stream_t stream = {0};
    max31865_sample_t sample;
    char line[32];
    unsigned i;
    a.settings.timeout_ms = 100; a.ticks = ticks;
    a.settings.channel_count = 4;
    for (i = 0; i < 8; ++i) {
        mock[i].raw = (uint16_t)(1000 + i);
        assert(max31865_init(&dev[i], &mock[i], transfer, 4, 50) == MAX31865_OK);
        assert(mock[i].config == 0x01);
        assert(max31865_start(&dev[i]) == MAX31865_OK);
        assert(mock[i].config == 0xc1);
        a.channel[i].device = &dev[i]; a.channel[i].ready_context = &mock[i];
        a.channel[i].ready = ready; a.channel[i].enabled = 1;
    }
    assert(max31865_init(&dev[0], &mock[0], transfer, 5, 50) == MAX31865_BAD_ARGUMENT);
    mock[0].mismatch = 1;
    assert(max31865_start(&dev[0]) == MAX31865_CONFIG_ERROR);
    mock[0].mismatch = 0;
    assert(!pt100_acquisition_poll(&a, 20, &f));
    for (i = 0; i < 4; ++i) mock[i].ready = 1;
    counter = 0xffffff00u; /* duration arithmetic crosses timer wrap */
    assert(pt100_acquisition_poll(&a, 21, &f));
    assert(f.sequence == 1 && f.ready_mask == 15 && f.read_ticks == 500);
    assert(f.channel[3].read_offset_ticks - f.channel[0].read_offset_ticks == 300);
    for (i = 0; i < 4; ++i) {
        assert(f.channel[i].sample.raw == 1000 + i);
        assert(f.channel[i].status == PT100_VALID);
        assert(mock[i].reads == 1);
    }
    assert(pt100_ascii_format(&f, line, sizeof(line)) == strlen("1000 1001 1002 1003\r\n"));
    assert(strcmp(line, "1000 1001 1002 1003\r\n") == 0);
    assert(pt100_ascii_format(&f, line, 4) == 0);
    busy = 1;
    for (i = 0; i < PT100_STREAM_DEPTH + 2; ++i) pt100_stream_push(&stream, &f);
    pt100_stream_poll(&stream, 0, send_line);
    assert(stream.count == 16 && stream.dropped == 2 && stream.accepted == 0);
    busy = 0;
    for (i = 0; i < 16; ++i) pt100_stream_poll(&stream, 0, send_line);
    assert(stream.count == 0 && stream.accepted == 16);
    assert(strcmp(sent, "1000 1001 1002 1003\r\n") == 0);
    mock[0].ready = mock[1].ready = mock[2].ready = 1;
    mock[0].fault = 0x84; mock[0].bit = 1; mock[1].fail = 1;
    assert(!pt100_acquisition_poll(&a, 42, &f));
    assert(pt100_acquisition_poll(&a, 121, &f));
    assert(f.channel[0].status == PT100_SENSOR_FAULT);
    assert(f.channel[0].sample.faults == 0x84 && f.channel[0].sample.rtd_fault == 1);
    assert(f.channel[1].status == PT100_BUS_ERROR);
    assert(f.channel[3].status == PT100_NOT_READY);
    assert(f.channel[3].sample.raw == 0 && mock[3].reads == 1);
    assert(a.timeout_frames == 1 && a.bus_errors[1] == 1 && a.sensor_errors[0] == 1);
    assert(pt100_ascii_format(&f, line, sizeof(line)) > 0);
    assert(strcmp(line, "-1 -1 1002 -1\r\n") == 0);
    assert(max31865_clear_faults(&dev[0]) == MAX31865_OK);
    assert(mock[0].fault == 0 && mock[0].config == 0xc1);
    mock[1].fail = 0;
    a.settings.trigger = PT100_TRIGGER_FIRST;
    mock[0].ready = 1;
    assert(pt100_acquisition_poll(&a, 142, &f));
    assert(f.channel[3].status == PT100_NOT_READY && mock[3].reads == 2);
    assert(f.channel[3].sample.raw == 1003);
    a.channel[2].enabled = 0;
    mock[0].ready = 1;
    assert(pt100_acquisition_poll(&a, 163, &f));
    assert(f.channel[2].status == PT100_INIT_ERROR);
    mock[0].raw = 32767;
    assert(max31865_read(&dev[0], &sample) == MAX31865_OK && sample.raw == 32767);
    mock[0].raw = 0;
    assert(max31865_read(&dev[0], &sample) == MAX31865_OK && sample.raw == 0);
    a.last_frame_ms = 0xfffffff0u;
    a.settings.trigger = PT100_WAIT_ALL;
    assert(pt100_acquisition_poll(&a, 84, &f)); /* ms counter wrap */
    a.settings.channel_count=8;
    for (i=0;i<8;++i) { mock[i].ready=1; a.channel[i].enabled=1; }
    assert(pt100_acquisition_poll(&a,105,&f));
    assert(f.ready_mask==0xff && f.channel[7].sample.raw==1007);
    assert(max31865_stop(&dev[0])==MAX31865_OK && mock[0].config==1);
    assert(max31865_bias(&dev[0])==MAX31865_OK && mock[0].config==0x81);
    assert(max31865_start(&dev[0])==MAX31865_OK && mock[0].config==0xc1);
    assert(max31865_stop(&dev[0])==MAX31865_OK && mock[0].config==1);
    puts("stage2: driver, acquisition, faults, timeout, timer wrap, ASCII and queue passed");
    return 0;
}
