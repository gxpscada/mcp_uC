/**
 * @file pt100_calendar.c
 * @brief Преобразование Unix UTC и календаря RTC в пределах 2000..2099 годов.
 */
#include "pt100_calendar.h"
static unsigned leap(unsigned y) { return y%4==0 && (y%100!=0 || y%400==0); }
static unsigned days(unsigned y, unsigned m)
{
    static const uint8_t d[]={31,28,31,30,31,30,31,31,30,31,30,31};
    return d[m-1]+(m==2 ? leap(y):0);
}
/**
 * @brief Переводит Unix-время UTC в календарь RTC.
 * @param[in] epoch Секунды с 1970-01-01 00:00:00 UTC; принимаются годы 2000..2099.
 * @param[out] c Календарь; не NULL. При недопустимом epoch не изменяется.
 * @return 0 при успехе, -1 при выходе за поддерживаемые годы.
 * @details weekday: 1 — понедельник, 7 — воскресенье. Часовые пояса, летнее время
 * и отдельное представление високосной секунды не используются.
 */
int pt100_epoch_to_calendar(uint32_t epoch, pt100_calendar_t *c)
{
    uint32_t d=epoch/86400u, rest=epoch%86400u;
    unsigned y=1970,m=1;
    if (epoch<946684800u || epoch>4102444799u) return -1; /* 2000..2099 UTC */
    c->weekday=(uint8_t)((d+3u)%7u+1u);
    while (d>=365u+leap(y)) { d-=365u+leap(y); ++y; }
    while (d>=days(y,m)) { d-=days(y,m); ++m; }
    c->year=(uint16_t)y; c->month=(uint8_t)m; c->day=(uint8_t)(d+1);
    c->hour=(uint8_t)(rest/3600u); c->minute=(uint8_t)((rest%3600u)/60u); c->second=(uint8_t)(rest%60u);
    return 0;
}
/**
 * @brief Проверяет календарную дату и переводит её в Unix-секунды UTC.
 * @param[in] c Календарь 2000..2099; weekday не используется, длина месяца и високосный год проверяются.
 * @param[out] epoch Результат в секундах с 1970-01-01; при ошибке не изменяется.
 * @return 0 при успехе, -1 при неверной дате/времени.
 * @pre Оба указателя не равны NULL. Секунды допускаются только в диапазоне 0..59.
 */
int pt100_calendar_to_epoch(const pt100_calendar_t *c, uint32_t *epoch)
{
    unsigned y,m;
    uint32_t d=0;
    if (c->year<2000 || c->year>2099 || c->month<1 || c->month>12 ||
        c->day<1 || c->day>days(c->year,c->month) || c->hour>23 || c->minute>59 || c->second>59) return -1;
    for (y=1970;y<c->year;++y) d+=365u+leap(y);
    for (m=1;m<c->month;++m) d+=days(c->year,m);
    d+=c->day-1u;
    *epoch=d*86400u+(uint32_t)c->hour*3600u+(uint32_t)c->minute*60u+c->second;
    return 0;
}
