/**
 * @file pt100_control.c
 * @brief Прикладная карта Modbus, проверка настроек и согласованный снимок измерений.
 */
#include "pt100_control.h"
#include "pt100_calendar.h"
#include <math.h>
#include <string.h>
static const float r0[PT100_MAX_CHANNELS]=PT100_R0_VALUES;
/**
 * @brief Формирует заводские настройки в RAM из параметров сборки.
 * @param[out] c Конфигурация для заполнения; не NULL.
 * @details Задаёт число датчиков, Rref, b=0, k=1, окна среднего 4 и медианы 3.
 * Флаги обработки берутся из PT100_DEFAULT_FLAGS. Ничего не пишет во Flash и не меняет RTC.
 */
void pt100_config_defaults(pt100_config_t *c)
{
    static const uint16_t refs[PT100_MAX_CHANNELS]=PT100_RREF_X10_VALUES;
    unsigned i;
    memset(c,0,sizeof(*c)); c->holding[0]=PT100_DEFAULT_CHANNELS;
    for (i=0;i<PT100_MAX_CHANNELS;++i) { c->holding[1+i]=refs[i]; c->holding[0x11+i]=100; }
    c->holding[0x19]=4; c->holding[0x1a]=3; c->flags=PT100_DEFAULT_FLAGS;
}
/**
 * @brief Проверяет целостность набора сохраняемых настроек.
 * @param[in] c Ненулевой указатель на конфигурацию.
 * @return 1 для допустимого набора, иначе 0.
 * @details Число активных датчиков не должно превышать число физических каналов сборки.
 * Проверяются флаги, положительные Rref/k всех восьми каналов и допустимые окна.
 * Знаковая b хранится как 16-битное представление и допускает весь диапазон int16_t.
 * Поля подготовленного времени RTC здесь не проверяются: их валидирует команда установки.
 */
int pt100_config_valid(const pt100_config_t *c)
{
    unsigned i;
    if (!c->holding[0] || c->holding[0]>PT100_BOARD_CHANNEL_COUNT ||
        (c->flags & ~PT100_DEFAULT_FLAGS)) return 0;
    for (i=0;i<PT100_MAX_CHANNELS;++i)
        if (!c->holding[1+i] || !c->holding[0x11+i]) return 0;
    return (c->holding[0x19]==2 || c->holding[0x19]==4 || c->holding[0x19]==8 || c->holding[0x19]==16) &&
           (c->holding[0x1a]==3 || c->holding[0x1a]==5 || c->holding[0x1a]==7);
}
/**
 * @brief Сбрасывает историю обработки и помечает все измерения недостоверными.
 * @param[in,out] c Контекст карты.
 * @param[in] status Причина недостоверности из PT100_*; помещается в старший байт состояния каждого канала.
 * @details Температура получает маркер 0x8000, ADC — 0xFFFF, метка кадра обнуляется.
 * Настройки и накопленное время работы не сбрасываются. Используется при STOP,
 * смене параметров и начале нового измерительного сеанса, чтобы не выдавать старые данные.
 */
void pt100_control_invalidate(pt100_control_t *c, uint8_t status)
{
    unsigned i;
    memset(c->filters,0,sizeof(c->filters)); memset(c->processed,0,sizeof(c->processed));
    for (i=0;i<PT100_MAX_CHANNELS;++i) {
        c->inputs[0x09+i]=(uint16_t)((unsigned)status<<8);
        c->inputs[0x15+i]=0x8000; c->inputs[0x20+i]=0xffff;
    }
    c->frame_seconds=0; c->frame_milliseconds=0;
    c->inputs[0x1e]=c->inputs[0x1f]=0;
}
/**
 * @brief Создаёт начальное состояние карты Modbus с остановленными измерениями.
 * @param[out] c Контекст карты; обнуляется целиком.
 * @details Устанавливает defaults, версии HW/SW, недостоверные значения T/ADC и STOPPED.
 * Не обращается к SPI, RTC и Flash: аппаратная инициализация выполняется приложением.
 */
void pt100_control_init(pt100_control_t *c)
{
    memset(c,0,sizeof(*c)); pt100_config_defaults(&c->config);
    pt100_control_invalidate(c,PT100_STOPPED);
    c->inputs[0x35]=PT100_HW_REVISION; c->inputs[0x36]=PT100_SW_REVISION;
}
/**
 * @brief Обрабатывает кадр и обновляет согласованный снимок Input Registers.
 * @param[in,out] c Карта регистров, настройки и отдельные истории фильтров каналов.
 * @param[in] f Кадр сбора; оба указателя должны быть действительными.
 * @details ADC-регистры получают именно исходные 15 бит, без фильтрации и поправки b.
 * Температура передаётся как int16 в сотых долях °C; 0x8000 зарезервирован для ошибки.
 * Выход за представимый диапазон температуры отмечается RANGE_ERROR. При аппаратной
 * ошибке история канала очищается; при заполнении окон выставляется FILTER_WARMUP.
 * RTC Lo/Hi относится к тому же кадру; миллисекунды сохраняются только внутри контекста.
 * @pre Вызывать в том же главном цикле, что и чтение Modbus: отдельной блокировки снимка нет.
 */
void pt100_control_frame(pt100_control_t *c, const pt100_frame_t *f)
{
    unsigned i;
    c->frame_seconds=f->utc_seconds; c->frame_milliseconds=f->milliseconds;
    c->inputs[0x1e]=(uint16_t)f->utc_seconds; c->inputs[0x1f]=(uint16_t)(f->utc_seconds>>16);
    for (i=0;i<PT100_MAX_CHANNELS;++i) {
        const pt100_channel_data_t *d=&f->channel[i];
        uint8_t status=d->status;
        pt100_processed_t *p=&c->processed[i];
        pt100_processing_settings_t s;
        int result;
        if (i>=c->config.holding[0]) status=PT100_STOPPED;
        c->inputs[0x15+i]=0x8000;
        c->inputs[0x20+i]=status ? 0xffff : d->sample.raw;
        memset(p,0,sizeof(*p));
        if (status) pt100_filter_reset(&c->filters[i]);
        else {
            s.median_window=(uint8_t)c->config.holding[0x1a];
            s.average_window=(uint8_t)c->config.holding[0x19];
            s.use_median=(c->config.flags&(1u<<6))!=0;
            s.use_average=(c->config.flags&(1u<<7))!=0;
            s.use_calibration=(c->config.flags&(1u<<8))!=0;
            s.adc_offset=(int16_t)(c->config.holding[9+i]<32768u ?
                (int32_t)c->config.holding[9+i] : (int32_t)c->config.holding[9+i]-65536);
            s.r0=r0[i]; s.rref=c->config.holding[1+i]*0.1f; s.gain=c->config.holding[0x11+i]*0.01f;
            result=pt100_process(&c->filters[i],&s,d->sample.raw,p);
            if (!result) status=PT100_FILTER_WARMUP;
            else if (result<0 || p->temperature*100.0f < -32767.0f || p->temperature*100.0f > 32767.0f)
                status=PT100_RANGE_ERROR;
            else {
                int32_t t=(int32_t)lroundf(p->temperature*100.0f);
                c->inputs[0x15+i]=(uint16_t)t;
            }
        }
        c->inputs[0x09+i]=(uint16_t)(((unsigned)status<<8)|d->sample.faults|d->sample.rtd_fault);
    }
}
static int error(pt100_control_t *c,int e)
{
    c->error_code=(uint16_t)(e==MB_ILLEGAL_ADDRESS ? 1 : e==MB_ILLEGAL_VALUE ? 2 : 4);
    return e;
}
static int flag_error(const pt100_control_t *c)
{
    unsigned i;
    if (c->error_code || c->hardware_error) return 1;
    for (i=0;i<c->config.holding[0];++i)
        if ((c->inputs[9+i]>>8)&(PT100_NOT_READY|PT100_BUS_ERROR|PT100_SENSOR_FAULT|PT100_INIT_ERROR|PT100_RANGE_ERROR)) return 1;
    return 0;
}
/**
 * @brief Читает только разрешённые адреса согласованной карты Modbus.
 * @param[in,out] ctx Указатель на pt100_control_t, переданный как void* транспортом.
 * @param[in] space Пространство MB_COILS, MB_DISCRETE, MB_HOLDING или MB_INPUT.
 * @param[in] addr Начальный адрес с нуля, без префиксов 3xxxx/4xxxx.
 * @param[in] count Число элементов; транспорт предварительно проверяет допустимость размера.
 * @param[out] out Массив минимум из count элементов uint16_t, в том числе для Coils.
 * @return 0 при успехе или код исключения Modbus при недопустимом диапазоне.
 * @details Диапазон сначала проверяется целиком: чтение через неразрешённый промежуток
 * не допускается. Runtime обновляется из контекста, измерения берутся из последнего
 * снимка. Командные Coils показывают ожидающее выполнение, а Run — запрос запуска,
 * не физическое подтверждение, что все микросхемы уже перешли в AUTO.
 */
int pt100_control_read(void *ctx,unsigned space,uint16_t addr,uint16_t count,uint16_t *out)
{
    pt100_control_t *c=ctx;
    unsigned i;
    for (i=0;i<count;++i) {
        unsigned a=(unsigned)addr+i;
        int ok=space==MB_COILS ? a<=8 : space==MB_DISCRETE ? a==0 :
            space==MB_HOLDING ? a<PT100_HOLDING_COUNT : space==MB_INPUT ?
            (a==1 || a==2 || (a>=9 && a<=0x10) || (a>=0x15 && a<=0x27) || a==0x35 || a==0x36) : 0;
        if (!ok) return error(c,MB_ILLEGAL_ADDRESS);
    }
    c->inputs[1]=(uint16_t)c->runtime_ms; c->inputs[2]=(uint16_t)(c->runtime_ms>>16);
    c->inputs[0x1d]=c->error_code;
    for (i=0;i<count;++i) {
        unsigned a=(unsigned)addr+i;
        if (space==MB_COILS) out[i]=a==0 ? c->run_requested : a<=5 ? (uint16_t)((c->commands>>a)&1) : (uint16_t)((c->config.flags>>a)&1);
        else if (space==MB_DISCRETE) out[i]=(uint16_t)flag_error(c);
        else if (space==MB_HOLDING) out[i]=c->config.holding[a];
        else out[i]=c->inputs[a];
    }
    return 0;
}
/**
 * @brief Проверяет запись целиком и атомарно применяет её к программной карте.
 * @param[in,out] ctx Указатель на pt100_control_t.
 * @param[in] space MB_COILS или MB_HOLDING; другие пространства доступны только для чтения.
 * @param[in] addr Начальный адрес с нуля.
 * @param[in] count Число записываемых элементов.
 * @param[in] values Массив count значений; Coils представлены числами 0/1, b — дополнительным кодом int16.
 * @return 0 при принятии; MB_ILLEGAL_ADDRESS/MB_ILLEGAL_VALUE при ошибке или MB_DEVICE_BUSY при занятости.
 * @details Проверка идёт на копии конфигурации: недопустимый элемент не оставляет
 * частично записанных настроек. Команды Save/Clear/RTC/Reboot/Defaults ставятся в очередь,
 * а не выполняются в обработчике Modbus. Для них Run должен быть выключен; одновременно
 * принимается одна команда. Пока команда ожидает завершения, разрешён повторный STOP,
 * но другие записи блокируются. Число каналов нельзя менять при запрошенном Run.
 * @note Успешный ответ подтверждает принятие запроса. Фактическую работу SPI/Flash/RTC
 * и завершение команд обеспечивает pt100_app_poll().
 */
int pt100_control_write(void *ctx,unsigned space,uint16_t addr,uint16_t count,const uint16_t *values)
{
    pt100_control_t *c=ctx;
    pt100_config_t next=c->config;
    unsigned i;
    uint16_t commands=0;
    uint8_t run=c->run_requested;
    uint8_t stop=0;
    /* Keep the configuration stable until a deferred command completes.
     * Repeated STOP remains available even during maintenance. */
    if (c->commands && !(space==MB_COILS && addr==0 && count==1 && values[0]==0))
        return MB_DEVICE_BUSY;
    if (!count || (uint32_t)addr+count>(space==MB_HOLDING ? PT100_HOLDING_COUNT : space==MB_COILS ? 9u : 0u))
        return error(c,MB_ILLEGAL_ADDRESS);
    if (space==MB_HOLDING) {
        for (i=0;i<count;++i) next.holding[addr+i]=values[i];
        if (!pt100_config_valid(&next)) return error(c,MB_ILLEGAL_VALUE);
        if (c->run_requested && next.holding[0]!=c->config.holding[0]) return MB_DEVICE_BUSY;
    } else {
        for (i=0;i<count;++i) {
            unsigned a=(unsigned)addr+i;
            if (values[i]>1) return error(c,MB_ILLEGAL_VALUE);
            if (!a) { run=(uint8_t)values[i]; if (!run) stop=1; }
            else if (a<=5) { if (values[i]) commands|=(uint16_t)(1u<<a); }
            else if (values[i]) next.flags|=(uint16_t)(1u<<a);
            else next.flags&=(uint16_t)~(1u<<a);
        }
        if (commands && (commands&(commands-1u))) return error(c,MB_ILLEGAL_VALUE);
        if (commands && c->commands) return MB_DEVICE_BUSY;
        if (commands && run) return MB_DEVICE_BUSY;
        if (commands==PT100_CMD_RTC) {
            pt100_calendar_t calendar;
            uint32_t epoch=(uint32_t)next.holding[0x1b]|((uint32_t)next.holding[0x1c]<<16);
            if (pt100_epoch_to_calendar(epoch,&calendar)) return error(c,MB_ILLEGAL_VALUE);
        }
        if (run && c->commands) return MB_DEVICE_BUSY;
    }
    if (memcmp(c->config.holding,next.holding,27u*sizeof(uint16_t)) || c->config.flags!=next.flags) {
        c->config_changed=1;
        pt100_control_invalidate(c,c->running ? PT100_FILTER_WARMUP : PT100_STOPPED);
    }
    c->config=next; c->run_requested=run; c->commands|=commands; c->stop_requested|=stop;
    return 0;
}
