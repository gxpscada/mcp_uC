/**
 * @file pt100_rtc.c
 * @brief Пошаговая работа аппаратного RTC STM32F4 и получение метки UTC.
 */
#include "pt100_rtc.h"
#include "pt100_calendar.h"
#include "pt100_board.h"
#define RTC_MAGIC 0x50545231u
static uint8_t state, writing;
static uint32_t since, requested;
volatile uint8_t pt100_rtc_error;
static uint32_t bcd(unsigned v) { return (v/10u)*16u+v%10u; }
static uint8_t unbcd(uint32_t v) { return (uint8_t)(((v>>4)&15u)*10u+(v&15u)); }
static void unlock(void) { RTC->WPR=0xca; RTC->WPR=0x53; }
static void fail(void)
{
    unlock(); RTC->ISR &= ~RTC_ISR_INIT; RTC->WPR=0xff;
    state=0; pt100_rtc_error=1;
}
/**
 * @brief Запускает неблокирующую инициализацию RTC и выбранного источника тактирования.
 * @details Дальнейшие шаги выполняет pt100_rtc_poll(). Источник задаётся PT100_RTC_USE_LSE:
 * внешний LSE 32768 Гц либо внутренний LSI с номиналом 32000 Гц. Существующий backup-домен
 * с правильным маркером сохраняется. Если уже выбран другой источник, выдаётся ошибка;
 * автоматического сброса backup-домена и потери сохранённого времени нет.
 * Первичная дата 2000-01-01 служит только инициализации: валидной меткой времени
 * она не считается до явного pt100_rtc_set().
 */
void pt100_rtc_init(void)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    PWR->CR |= PWR_CR_DBP;
    state=1; writing=0; since=HAL_GetTick(); pt100_rtc_error=0;
#if PT100_RTC_USE_LSE
    RCC->BDCR |= RCC_BDCR_LSEON;
#else
    RCC->CSR |= RCC_CSR_LSION;
#endif
}
/**
 * @brief Сообщает, что RTC пока не находится в рабочем состоянии.
 * @return 0 только в готовом состоянии, ненулевое значение при инициализации, записи или ошибке.
 * @warning При ошибке функция также возвращает занятость. Нужно отдельно проверять
 * pt100_rtc_error, иначе вызывающий код может бесконечно ждать восстановления.
 */
int pt100_rtc_busy(void) { return state!=3; }
/**
 * @brief Выполняет один шаг ожидания генератора или записи календаря RTC.
 * @details Вызывать из главного цикла после init и во время установки времени.
 * Ожидание генератора ограничено 3000 мс, входа RTC в INIT — 100 мс.
 * При отказе устанавливается pt100_rtc_error. Внутреннего цикла ожидания нет.
 */
void pt100_rtc_poll(void)
{
    if (state==1) {
        uint32_t source=PT100_RTC_USE_LSE ? RCC_BDCR_RTCSEL_0 : RCC_BDCR_RTCSEL_1;
        int ready=PT100_RTC_USE_LSE ? (RCC->BDCR&RCC_BDCR_LSERDY)!=0 : (RCC->CSR&RCC_CSR_LSIRDY)!=0;
        if (!ready) { if ((uint32_t)(HAL_GetTick()-since)>3000u) fail(); return; }
        if ((RCC->BDCR&RCC_BDCR_RTCSEL) && (RCC->BDCR&RCC_BDCR_RTCSEL)!=source) { fail(); return; }
        RCC->BDCR |= source | RCC_BDCR_RTCEN;
        if (RTC->BKP0R==RTC_MAGIC) { state=3; return; }
        requested=946684800u; writing=0;
        unlock(); RTC->ISR |= RTC_ISR_INIT;
        since=HAL_GetTick(); state=2;
    } else if (state==2) {
        pt100_calendar_t c;
        if (!(RTC->ISR&RTC_ISR_INITF)) { if ((uint32_t)(HAL_GetTick()-since)>100u) fail(); return; }
        (void)pt100_epoch_to_calendar(requested,&c);
        RTC->CR = RTC_CR_BYPSHAD; /* 24-hour, direct coherent reads below */
        RTC->PRER = (127u<<16) | (PT100_RTC_USE_LSE ? 255u : 249u);
        RTC->TR = (bcd(c.hour)<<16)|(bcd(c.minute)<<8)|bcd(c.second);
        RTC->DR = (bcd(c.year-2000u)<<16)|((uint32_t)c.weekday<<13)|(bcd(c.month)<<8)|bcd(c.day);
        RTC->ISR &= ~RTC_ISR_INIT;
        RTC->BKP0R=RTC_MAGIC; RTC->BKP1R=writing ? RTC_MAGIC : 0;
        RTC->WPR=0xff; state=3;
    }
}
/**
 * @brief Принимает новое время UTC для последующей записи аппаратного RTC.
 * @param[in] epoch Unix-секунды, соответствующие 2000..2099 годам.
 * @return 0, если запрос принят, -1 при неверной дате или неготовности RTC.
 * @details Возврат 0 ещё не означает завершённую запись. Продолжать вызывать poll,
 * проверять pt100_rtc_error и ожидать готовность. Подсекундная часть при установке
 * не передаётся; задержка транспорта до вызова в значение epoch автоматически не вносится.
 */
int pt100_rtc_set(uint32_t epoch)
{
    pt100_calendar_t c;
    if (pt100_epoch_to_calendar(epoch,&c)) return -1;
    if (state!=3) return -1;
    requested=epoch; writing=1; since=HAL_GetTick();
    unlock(); RTC->ISR |= RTC_ISR_INIT; state=2;
    return 0;
}
/**
 * @brief Снимает согласованный календарь RTC и его подсекундную часть.
 * @param[out] epoch Unix-секунды UTC; сначала обнуляются.
 * @param[out] ms Доля секунды 0..999 мс, вычисленная из аппаратного делителя; сначала обнуляется.
 * @return 0 для достоверного снимка, -1 при неустановленном/неготовом RTC или неудачном чтении.
 * @pre Оба указателя действительны. Вызывать в том же контексте, что и RTC poll/set.
 * @details Для защиты от перехода через границу секунды проверяется SSR до и после
 * чтения календаря, максимум три попытки. Миллисекунды имеют квантование делителя;
 * они не означают абсолютную точность 1 мс. Точность LSI без калибровки ограничена его дрейфом.
 */
int pt100_rtc_get(uint32_t *epoch, uint16_t *ms)
{
    unsigned attempt;
    *epoch=0; *ms=0;
    if (state!=3 || RTC->BKP1R!=RTC_MAGIC) return -1;
    for (attempt=0;attempt<3;++attempt) {
        uint32_t ss1=RTC->SSR, tr=RTC->TR, dr=RTC->DR, ss2=RTC->SSR;
        pt100_calendar_t c;
        if (ss2>ss1) continue;
        c.year=(uint16_t)(2000u+unbcd((dr>>16)&0xff));
        c.month=unbcd((dr>>8)&0x1f); c.day=unbcd(dr&0x3f);
        c.hour=unbcd((tr>>16)&0x3f); c.minute=unbcd((tr>>8)&0x7f); c.second=unbcd(tr&0x7f);
        if (pt100_calendar_to_epoch(&c,epoch)) return -1;
        *ms=(uint16_t)(((RTC->PRER&0x7fff)-ss1)*1000u/((RTC->PRER&0x7fff)+1u));
        return 0;
    }
    return -1;
}
