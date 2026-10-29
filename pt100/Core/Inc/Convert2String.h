#ifndef CONVERT2STRING_H_
#define CONVERT2STRING_H_

//+
static inline size_t uint32_to_str(uint32_t value, char *buf, size_t buf_size){
    if (!buf || buf_size == 0) return 0;
    char tmp[11];
    size_t len = 0;

    if (value == 0) {
        if (buf_size > 1) { buf[0] = '0'; buf[1] = '\0'; }
        return 1;
    }

    while (value > 0 && len < sizeof(tmp) - 1) {
        tmp[len++] = (char)('0' + (value % 10));
        value /= 10;
    }
    tmp[len] = '\0';

    size_t i = 0;
    while (len > 0 && i < buf_size - 1) {
        buf[i++] = tmp[--len];
    }
    buf[i] = '\0';
    return i;
};

// 2 digits
static inline size_t float_to_str_2dec(float val, char *buf, size_t buf_size){
	if (!buf || buf_size < 6) return 0; // Минимум "-0.00" + '\0' (6 байт)
	int is_negative = (val < 0.0f);
	if (is_negative) val = -val;
	// Единое округление до двух знаков после запятой (0.01)
	uint32_t total = (uint32_t)(val * 100.0f + 0.5f);
	uint32_t int_part = total / 100;
	uint32_t frac_part = total % 100; // 2 знака (0..99)

	size_t pos = 0; // Выводим знак минус только если число отрицательное и не округлилось до 0.00
	if (is_negative && total > 0 && pos + 1 < buf_size) { buf[pos++] = '-'; }

	pos += uint32_to_str(int_part, buf + pos, buf_size - pos);
	if (pos + 3 < buf_size) {
		buf[pos++] = '.';
		buf[pos++] = (char)('0' + (frac_part / 10));
		buf[pos++] = (char)('0' + (frac_part % 10));
		buf[pos] = '\0';
	} else {
		// Если места под точку и 2 знака не хватило
		buf[0] = '\0';
		return 0;
	}
	return pos;
};

// 3 digits
static inline size_t float_to_str_3dec(float val, char *buf, size_t buf_size) {
	if (!buf || buf_size < 7) return 0; // минимум "-0.000" + '\0' (7 байт)
	int is_negative = (val < 0.0f);
	if (is_negative) val = -val;
	// Единое масштабирование и округление до 3 знаков (0.001)
	uint32_t total = (uint32_t)(val * 1000.0f + 0.5f);
	uint32_t int_part = total / 1000;
	uint32_t frac_part = total % 1000; // 3 знака (0..999)

	size_t pos = 0;
	if (is_negative && pos + 1 < buf_size) {
		buf[pos++] = '-';
	}

	pos += uint32_to_str(int_part, buf + pos, buf_size - pos);

	if (pos + 4 < buf_size) {
		buf[pos++] = '.';
		buf[pos++] = (char)('0' + (frac_part / 100)); // Сотни (1-й знак)
		buf[pos++] = (char)('0' + ((frac_part / 10) % 10)); // Десятки (2-й знак)
		buf[pos++] = (char)('0' + (frac_part % 10)); // Единицы (3-й знак)
		}
	buf[pos] = '\0';
	return pos;
};

static uint16_t Average4_Array(const uint16_t *data) {
	// Буфер суммы 32 бита, чтобы избежать переполнения при сложении
	uint32_t sum = (uint32_t)data[0] + data[1] + data[2] + data[3];
	// Прибавление +2 перед сдвигом на 2 бита (делением на 4) обеспечивает
	// математическое округление к ближайшему целому числу вместо отбрасывания дробной части
	return (uint16_t)((sum + 2) >> 2);
}

#endif /* MAX31865_H_ */
