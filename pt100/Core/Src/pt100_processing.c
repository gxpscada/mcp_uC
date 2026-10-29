/**
 * @file pt100_processing.c
 * @brief Фильтрация ADC, знаковая поправка и преобразование сопротивления в температуру.
 */
#include "pt100_processing.h"
#include <math.h>
#include <string.h>
/**
 * @brief Очищает историю медианного фильтра и скользящего среднего одного канала.
 * @param[out] f Ненулевой указатель на состояние фильтров.
 * @details Вызывать перед первым отсчётом, после ошибки датчика и после изменения окон
 * или режима обработки. После сброса окна должны заполниться заново; старые отсчёты
 * не должны смешиваться с данными, полученными при других настройках.
 */
void pt100_filter_reset(pt100_filter_t *f) { memset(f, 0, sizeof(*f)); }
/**
 * @brief Проверяет допустимость параметров обработки без изменения состояния.
 * @param[in] s Настройки канала; NULL считается ошибкой.
 * @return 1, если окна допустимы, а r0, rref и gain конечны и положительны; иначе 0.
 * @details Медианное окно допускает 3, 5 или 7 отсчётов; среднее — 2, 4, 8 или 16.
 * Размеры проверяются даже для выключенных фильтров. Проверка не доказывает,
 * что конкретный отсчёт после калибровки попадёт в диапазон преобразования.
 */
int pt100_filter_valid(const pt100_processing_settings_t *s)
{
    return s && (s->median_window == 3 || s->median_window == 5 || s->median_window == 7) &&
        (s->average_window == 2 || s->average_window == 4 ||
         s->average_window == 8 || s->average_window == 16) &&
        isfinite(s->r0) && s->r0 > 0 && isfinite(s->rref) && s->rref > 0 &&
        isfinite(s->gain) && s->gain > 0;
}
static double ratio(double t)
{
    return 1.0 + 3.9083e-3*t - 5.775e-7*t*t +
        (t < 0 ? -4.183e-12*(t-100)*t*t*t : 0);
}
/**
 * @brief Преобразует сопротивление платинового RTD в температуру по Каллендару — Ван Дюзену.
 * @param[in] resistance Измеренное сопротивление датчика, Ом.
 * @param[in] r0 Сопротивление этого типа датчика при 0 °C, Ом: обычно 100 или 1000.
 * @param[out] temperature Температура в °C; указатель не может быть NULL. При ошибке значение не обновляется.
 * @retval 0 Преобразование выполнено.
 * @retval -1 Неверные аргументы или сопротивление вне диапазона модели от -200 до +850 °C.
 * @details Используются A=3.9083e-3, B=-5.775e-7, C=-4.183e-12.
 * При T >= 0 решается квадратное уравнение в форме, уменьшающей потерю точности
 * при вычитании близких чисел. Ниже нуля член C учитывается через 32 шага бисекции.
 * Число шагов ограничено, поэтому поиск не может зависнуть в ожидании сходимости.
 * Промежуточные вычисления ведутся в double; индивидуальный коэффициент k здесь не применяется.
 */
int pt100_cvd_temperature(float resistance, float r0, float *temperature)
{
    double r, t;
    if (!temperature || !isfinite(resistance) || !isfinite(r0) || r0 <= 0) return -1;
    r = (double)resistance / r0;
    if (r < ratio(-200.0) || r > ratio(850.0)) return -1;
    if (r >= 1) {
        /* Rationalized positive-temperature quadratic avoids cancellation. */
        t = 2.0*(r-1.0)/(3.9083e-3 + sqrt(3.9083e-3*3.9083e-3 - 4*5.775e-7*(r-1)));
    } else {
        double lo = -200, hi = 0;
        unsigned i;
        /* Monotonic CVD inversion; fixed bound, no convergence hang. */
        for (i = 0; i < 32; ++i) {
            double mid = (lo + hi)*0.5;
            if (ratio(mid) < r) lo = mid; else hi = mid;
        }
        t = (lo + hi)*0.5;
    }
    *temperature = (float)t;
    return 0;
}
/**
 * @brief Обрабатывает один исправный отсчёт: медиана → среднее → поправка ADC → CVD → коэффициент k.
 * @param[in,out] f История фильтров только этого канала; перед первым вызовом обнулить.
 * @param[in] s Настройки обработки. После изменения настроек историю необходимо сбросить.
 * @param[in] raw Исходный 15-битный отсчёт MAX31865, от 0 до 32767.
 * @param[out] out Результат обработки; обнуляется в начале каждого вызова.
 * @retval 1 Окна заполнены, преобразование успешно, out->valid установлен.
 * @retval 0 Идёт заполнение включённых фильтров; температуру использовать пока нельзя.
 * @retval -1 Ошибка настроек, диапазона ADC или преобразования сопротивления.
 * @pre f и out не равны NULL. Отсчёты с аппаратной ошибкой сюда не передаются.
 * @details Среднее хранится в Q1: целое значение означает удвоенный код ADC,
 * то есть единица младшего разряда соответствует половине исходного отсчёта.
 * Вычисляется floor(2*sum/N), затем добавляется 2*b; выход за 0..65534 считается
 * ошибкой, а не насыщается. Сопротивление равно corrected_q1*rref/65536.
 * Коэффициент gain умножает уже вычисленную температуру в °C. Если калибровка
 * выключена, не применяются ни b, ни gain. Фильтрация не изменяет исходный raw.
 * @note Дополнительный дробный разряд не гарантирует дополнительный эффективный бит
 * точности: результат зависит от шума, корреляции отсчётов и аппаратной части.
 */
int pt100_process(pt100_filter_t *f, const pt100_processing_settings_t *s,
                  uint16_t raw, pt100_processed_t *out)
{
    uint16_t value = raw;
    uint32_t q1;
    int32_t corrected;
    unsigned i, j;
    memset(out, 0, sizeof(*out));
    if (!pt100_filter_valid(s) || raw > 32767) return -1;
    if (s->use_median) {
        uint16_t sorted[7];
        f->median[f->median_pos] = raw;
        f->median_pos = (uint8_t)((f->median_pos + 1u) % s->median_window);
        if (f->median_count < s->median_window) ++f->median_count;
        if (f->median_count < s->median_window) return 0;
        memcpy(sorted, f->median, s->median_window * sizeof(uint16_t));
        for (i = 1; i < s->median_window; ++i) {
            uint16_t x = sorted[i];
            for (j = i; j && sorted[j-1] > x; --j) sorted[j] = sorted[j-1];
            sorted[j] = x;
        }
        value = sorted[s->median_window/2];
    }
    q1 = (uint32_t)value * 2u;
    if (s->use_average) {
        unsigned shift = s->average_window == 2 ? 0u : s->average_window == 4 ? 1u :
                         s->average_window == 8 ? 2u : 3u;
        f->sum -= f->average[f->average_pos];
        f->average[f->average_pos] = value;
        f->sum += value;
        f->average_pos = (uint8_t)((f->average_pos + 1u) % s->average_window);
        if (f->average_count < s->average_window) ++f->average_count;
        if (f->average_count < s->average_window) return 0;
        q1 = f->sum >> shift; /* 2 * sum / N: retain half an ADC count. */
    }
    out->adc_q1 = (uint16_t)q1;
    corrected = (int32_t)q1 + (s->use_calibration ? (int32_t)s->adc_offset*2 : 0);
    if (corrected < 0 || corrected > 65534) return -1;
    out->corrected_q1 = (uint16_t)corrected;
    out->resistance = (float)corrected * (s->rref / 65536.0f);
    if (pt100_cvd_temperature(out->resistance, s->r0, &out->temperature)) return -1;
    if (s->use_calibration) out->temperature *= s->gain;
    if (!isfinite(out->temperature)) return -1;
    out->valid = 1;
    return 1;
}
