/**
 * @file pt100_app.c
 * @brief Главный автомат измерений и согласование Modbus, датчиков, RTC и Flash.
 */
#include "pt100_stm32.h"
#include "pt100_board.h"
#include "pt100_rtc.h"
#include "pt100_flash.h"
#include "usbd_cdc_if.h"
#include <string.h>
pt100_acquisition_t pt100_acquisition;
pt100_frame_t pt100_last_frame;
pt100_control_t pt100_control;
modbus_rtu_t pt100_modbus;
uint32_t pt100_tick_hz;
max31865_result_t pt100_init_status[PT100_CHANNELS];
static max31865_t devices[PT100_CHANNELS];
static pt100_stm32_channel_t ports[PT100_CHANNELS];
static pt100_store_t store;
static volatile uint8_t app_ready;
static uint8_t phase, channel_index, action_started;
static uint32_t since, run_since;
enum { OFF, BIAS, SETTLE, STARTING, RUNNING, STOPPING };

static int usb_send(void *unused,const uint8_t *p,size_t n)
{
    (void)unused;
    return CDC_Transmit_FS((uint8_t *)p,(uint16_t)n)==USBD_OK ? 0 : -1;
}
static int read_map(void *ctx,unsigned space,uint16_t addr,uint16_t count,uint16_t *out)
{
    /* With no measurement yet, expose current RTC so the host can verify SET.
     * During acquisition RTC registers belong to the same sample snapshot. */
    if (!pt100_control.frame_seconds) {
        uint32_t epoch; uint16_t ms;
        if (!pt100_rtc_get(&epoch,&ms)) {
            pt100_control.inputs[0x1e]=(uint16_t)epoch;
            pt100_control.inputs[0x1f]=(uint16_t)(epoch>>16);
        }
    }
    return pt100_control_read(ctx,space,addr,count,out);
}
/**
 * @brief Передаёт принятый USB CDC фрагмент в очередь Modbus.
 * @param[in] p Буфер принятых данных; содержимое копируется до возврата.
 * @param[in] n Число принятых байтов.
 * @details Вызывается USB callback, в том числе из ISR. До завершения app_init
 * данные игнорируются. Разбор карты и аппаратные команды в ISR не выполняются.
 */
void pt100_app_usb_receive(const uint8_t *p,size_t n)
{
    if (app_ready) modbus_rtu_receive(&pt100_modbus,p,n,HAL_GetTick());
}
/**
 * @brief Уведомляет Modbus о деинициализации или разрыве USB-соединения.
 * @details До готовности приложения ничего не делает. После готовности только
 * запрашивает очистку транспортного состояния; измерения автоматически не останавливает.
 */
void pt100_app_usb_reset(void)
{
    if (app_ready) modbus_rtu_disconnect(&pt100_modbus);
}
/**
 * @brief Настраивает CS и DRDY физически доступных каналов по таблице платы.
 * @details Перед переводом CS в выход выставляет высокий уровень, чтобы не выбирать
 * микросхему случайно. DRDY — вход с подтяжкой вверх. Используются только первые
 * PT100_BOARD_CHANNEL_COUNT строк таблицы; остальные пины не затрагиваются.
 * @pre Вызывать при старте после HAL_Init(), до инициализации драйверов датчиков.
 * Настройка альтернативной функции SPI выполняется отдельно через MSP.
 */
void pt100_board_gpio_init(void)
{
    pt100_stm32_channel_t map[PT100_CHANNELS]=PT100_BOARD_CHANNELS(0);
    GPIO_InitTypeDef gpio={0};
    unsigned i;
    PT100_BOARD_GPIO_CLOCK_ENABLE();
    for (i=0;i<PT100_BOARD_CHANNEL_COUNT;++i) {
        HAL_GPIO_WritePin(map[i].cs_port,map[i].cs_pin,GPIO_PIN_SET);
        gpio.Pin=map[i].cs_pin; gpio.Mode=GPIO_MODE_OUTPUT_PP;
        gpio.Pull=GPIO_PULLUP; gpio.Speed=GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(map[i].cs_port,&gpio);
        gpio.Pin=map[i].drdy_pin; gpio.Mode=GPIO_MODE_INPUT; gpio.Pull=GPIO_PULLUP;
        HAL_GPIO_Init(map[i].drdy_port,&gpio);
    }
}
/**
 * @brief Собирает приложение из аппаратных адаптеров, карты Modbus и переносимых модулей.
 * @param[in] spi Инициализированный HAL-дескриптор общей SPI-шины; должен жить постоянно.
 * @details Загружает настройки из журнала, создаёт контексты датчиков, включает DWT,
 * запускает RTC и разрешает приём USB в приложение. MAX31865 инициализируются
 * с выключенными AUTO/BIAS; измерения начинаются только по команде Run.
 * @pre GPIO и SPI уже настроены. Это стартовая инициализация, а не процедура перезапуска
 * работающего приложения; далее регулярно вызывать pt100_app_poll().
 */
void pt100_app_init(SPI_HandleTypeDef *spi)
{
    pt100_stm32_channel_t map[PT100_CHANNELS]=PT100_BOARD_CHANNELS(spi);
    unsigned i;
    app_ready=0;
    memcpy(ports,map,sizeof(ports));
    memset(&pt100_acquisition,0,sizeof(pt100_acquisition));
    pt100_control_init(&pt100_control);
    pt100_flash_init(&store);
    (void)pt100_store_load(&store,&pt100_control.config);
    modbus_rtu_init(&pt100_modbus,PT100_MODBUS_ADDRESS,&pt100_control,read_map,pt100_control_write);
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT=0; DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    pt100_tick_hz=SystemCoreClock;
    pt100_acquisition.ticks=pt100_stm32_ticks;
    pt100_acquisition.timestamp=pt100_rtc_get;
    pt100_acquisition.settings.trigger=PT100_TRIGGER_MODE;
    pt100_acquisition.settings.timeout_ms=PT100_FRAME_TIMEOUT_MS;
    for (i=0;i<PT100_BOARD_CHANNEL_COUNT;++i) {
        pt100_channel_t *c=&pt100_acquisition.channel[i];
        c->device=&devices[i]; c->ready_context=&ports[i]; c->ready=pt100_stm32_ready;
        pt100_init_status[i]=max31865_init(&devices[i],&ports[i],pt100_stm32_transfer,4,50);
        if (pt100_init_status[i]!=MAX31865_OK) pt100_control.hardware_error=1;
    }
    pt100_rtc_init();
    phase=OFF; app_ready=1;
}
static void hardware_failure(void)
{
    pt100_control.hardware_error=1; pt100_control.error_code=4;
}
/**
 * @brief Продвигает автомат OFF → BIAS → SETTLE → STARTING → RUNNING → STOPPING.
 * @param[in] now Монотонное время HAL в миллисекундах.
 * @details Включение/выключение идёт по одному каналу за проход. После включения BIAS
 * всех выбранных каналов выдерживается общая задержка установления. Перед AUTO
 * считывается старый отсчёт, чтобы освободить прежний DRDY. При STOP обходятся все
 * физические каналы, даже неактивные. Отказ одного SPI-обмена не отменяет попытки
 * отключить остальные датчики; подтверждение ошибок хранится отдельно.
 */
static void acquisition_poll(uint32_t now)
{
    unsigned count=pt100_control.config.holding[0];
    if (phase!=OFF && phase!=STOPPING) pt100_control.runtime_ms=now-run_since;
    if (pt100_control.stop_requested || (!pt100_control.run_requested && phase!=OFF && phase!=STOPPING)) {
        pt100_control.stop_requested=0;
        phase=STOPPING; channel_index=0; pt100_control.running=0;
        pt100_control_invalidate(&pt100_control,PT100_STOPPED);
    }
    if (phase==OFF && pt100_control.run_requested) {
        phase=BIAS; channel_index=0; run_since=now; pt100_control.runtime_ms=0;
        pt100_acquisition.settings.channel_count=(uint8_t)count;
        pt100_acquisition.observed_mask=0; pt100_acquisition.sequence=0;
        pt100_control_invalidate(&pt100_control,PT100_FILTER_WARMUP);
    } else if (phase==BIAS) {
        unsigned i=channel_index++;
        max31865_result_t r=max31865_init(&devices[i],&ports[i],pt100_stm32_transfer,4,50);
        if (r==MAX31865_OK) r=max31865_bias(&devices[i]);
        pt100_init_status[i]=r;
        pt100_acquisition.channel[i].enabled=(uint8_t)(r==MAX31865_OK);
        if (r!=MAX31865_OK) hardware_failure();
        if (channel_index==count) { since=now; phase=SETTLE; }
    } else if (phase==SETTLE) {
        if ((uint32_t)(now-since)>=PT100_BIAS_SETTLE_MS) { phase=STARTING; channel_index=0; }
    } else if (phase==STARTING) {
        unsigned i=channel_index++;
        if (pt100_acquisition.channel[i].enabled) {
            max31865_sample_t stale;
            max31865_result_t r=max31865_read(&devices[i],&stale); /* release old DRDY */
            if (r==MAX31865_OK) r=max31865_start(&devices[i]);
            pt100_init_status[i]=r;
            pt100_acquisition.channel[i].enabled=(uint8_t)(r==MAX31865_OK);
            if (r!=MAX31865_OK) { (void)max31865_stop(&devices[i]); hardware_failure(); }
        }
        if (channel_index==count) {
            phase=RUNNING; pt100_control.running=1;
            pt100_acquisition.last_frame_ms=now;
        }
    } else if (phase==RUNNING) {
        if (pt100_acquisition_poll(&pt100_acquisition,now,&pt100_last_frame))
            pt100_control_frame(&pt100_control,&pt100_last_frame);
    } else if (phase==STOPPING) {
        unsigned i=channel_index++;
        if (max31865_stop(&devices[i])!=MAX31865_OK) hardware_failure();
        pt100_acquisition.channel[i].enabled=0;
        if (channel_index==PT100_BOARD_CHANNEL_COUNT) phase=OFF;
    }
}
/**
 * @brief Выполняет принятую сервисную команду после остановки датчиков и отправки ответа USB.
 * @details Ожидание освобождения TX важно для Reboot: сброс не должен оборвать ответ
 * на саму команду. Save и RTC продолжаются между вызовами через action_started.
 * Clear повторяет STOP и сбрасывает fault каждого физического канала. Defaults меняет
 * только RAM; постоянное сохранение требует отдельного Save. Командный бит очищается
 * после завершения либо обнаруженной ошибки; подробности отказа остаются в карте.
 */
static void commands_poll(void)
{
    uint16_t command=pt100_control.commands;
    if (!command || phase!=OFF || pt100_modbus.response_length || CDC_TransmitBusy_FS()) return;
    if (command==PT100_CMD_REBOOT) { NVIC_SystemReset(); return; }
    if (command==PT100_CMD_SAVE) {
        if (!action_started) {
            if (pt100_store_save(&store,&pt100_control.config)) { hardware_failure(); pt100_control.commands=0; }
            else action_started=1;
            return;
        }
        if (store.state) return;
        if (store.error) hardware_failure();
    } else if (command==PT100_CMD_RTC) {
        if (!action_started) {
            uint32_t epoch=(uint32_t)pt100_control.config.holding[0x1b]|
                ((uint32_t)pt100_control.config.holding[0x1c]<<16);
            if (pt100_rtc_error) { hardware_failure(); pt100_control.commands=0; return; }
            if (pt100_rtc_busy()) return;
            if (pt100_rtc_set(epoch)) { hardware_failure(); pt100_control.commands=0; return; }
            action_started=1; return;
        }
        if (pt100_rtc_error) hardware_failure();
        else if (pt100_rtc_busy()) return;
    } else if (command==PT100_CMD_CLEAR) {
        if (!action_started) {
            action_started=1; channel_index=0;
            pt100_control.error_code=0; pt100_control.hardware_error=0;
        }
        {
            unsigned i=channel_index++;
            if (max31865_stop(&devices[i])!=MAX31865_OK ||
                max31865_clear_faults(&devices[i])!=MAX31865_OK) hardware_failure();
        }
        if (channel_index<PT100_BOARD_CHANNEL_COUNT) return;
        pt100_control_invalidate(&pt100_control,PT100_STOPPED);
    } else if (command==PT100_CMD_DEFAULTS) {
        pt100_config_defaults(&pt100_control.config);
        pt100_control_invalidate(&pt100_control,PT100_STOPPED);
    }
    pt100_control.commands=0; action_started=0;
}
/**
 * @brief Обслуживает RTC, Flash, Modbus, сбор данных и отложенные команды за один проход.
 * @details Вызывать непрерывно из главного цикла. Порядок позволяет сначала принять
 * команду, затем продвинуть автомат датчиков и только после остановки выполнить
 * сервисное действие. Ожидания BIAS/DRDY/USB разбиты на состояния без HAL_Delay.
 * SPI-транзакции остаются синхронными с ограниченным HAL-тайм-аутом.
 * @pre pt100_app_init() завершена. Функция не реентерабельна и не предназначена для ISR.
 */
void pt100_app_poll(void)
{
    uint32_t now=HAL_GetTick();
    pt100_rtc_poll();
    pt100_store_poll(&store);
    modbus_rtu_poll(&pt100_modbus,now,0,usb_send);
    acquisition_poll(now);
    commands_poll();
    if (pt100_rtc_error) hardware_failure();
}
