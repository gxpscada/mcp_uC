#include "device_stm32.h"
#include "device_board_stm32.h"
#include "device_rtc.h"
#include "device_flash.h"
#include "usbd_cdc_if.h"
#include <string.h>

device_acquisition_t device_acquisition; // Глобальный контекст модуля сбора данных с датчиков
device_frame_t device_last_frame; // Буфер последнего собранного сырого кадра измерений
device_control_t device_control; // Контекст карты регистров Modbus, настроек и фильтров
modbus_ascii_t device_modbus; // Контекст парсера и передатчика протокола Modbus ASCII
uint32_t device_tick_hz; // Текущая тактовая частота ядра (SystemCoreClock) для перевода тактов DWT в секунды
// Массив результатов инициализации каждого физического датчика MAX31865
max31865_result_t device_init_status[DEVICE_CHANNELS];
static max31865_t devices[DEVICE_CHANNELS]; // Экземпляры драйверов MAX31865 для каждого канала
static device_stm32_channel_t ports[DEVICE_CHANNELS]; // Массив структур с привязкой портов/пинов CS и DRDY
static device_store_t store; // Контекст журналируемого хранилища настроек во Flash
static volatile uint8_t app_ready; // Флаг завершения инициализации (разрешает приём данных в прерывании USB)
static uint8_t phase; // Текущее состояние главного автомата управления измерениями
static uint8_t channel_index; // Индекс текущего обрабатываемого канала при пошаговом включении/выключении
static uint8_t action_started; // Флаг того, что долгая сервисная команда (Save/RTC/Clear) уже запущена
static uint32_t since; // Метка времени (мс) для отсчёта задержки установления тока смещения BIAS
static uint32_t run_since; // Метка времени (мс) момента старта текущего цикла измерений

#define OFF 0u // Состояние: измерения остановлены, все датчики переведены в спящий режим
#define BIAS 1u // Состояние: пошаговое включение источника опорного тока (Vbias) на датчиках
#define SETTLE 2u // Состояние: ожидание заряда входных RC-цепей после включения Vbias
#define STARTING 3u // Состояние: сброс старого DRDY и перевод датчиков в непрерывный режим (AUTO)
#define RUNNING 4u // Состояние: рабочий цикл непрерывного сбора и фильтрации кадров
#define STOPPING 5u // Состояние: пошаговое отключение Vbias и режима AUTO на всех датчиках

// Callback-функция отправки сформированного ответа Modbus через USB CDC
static int usb_send(void *unused, const uint8_t *p, size_t n)
{
    (void)unused; // Контекст не используется, подавляем предупреждение компилятора
    // Передаём пакет в стек USB FS; возвращаем 0 при успехе или -1, если буфер занят
    return CDC_Transmit_FS((uint8_t *)p, (uint16_t)n) == USBD_OK ? 0 : -1;
}

// Промежуточная обёртка чтения регистров Modbus для подстановки живого времени RTC до начала
// измерений
static int read_map(void *ctx, unsigned space, uint16_t addr, uint16_t count, uint16_t *out)
{
    if (!device_control.frame_seconds) { // Если ни одного кадра измерений ещё не было (время кадра равно 0)
        uint32_t epoch; // Переменная для секунд Unix-времени
        uint16_t ms; // Переменная для миллисекунд
        // Читаем текущее время из аппаратного RTC; если оно валидно (вернулся 0)
        if (!device_rtc_get(&epoch, &ms)) {
            // Записываем младшие 16 бит времени в Input-регистр RTC_LO
            device_control.inputs[IR_RTC_LO] = (uint16_t)epoch;
            // Записываем старшие 16 бит времени в Input-регистр RTC_HI
            device_control.inputs[IR_RTC_HI] = (uint16_t)(epoch >> 16);
        }
    }
    // Вызываем основную функцию чтения карты регистров
    return device_control_read(ctx, space, addr, count, out);
}

// Обработчик приёма сырых байтов из прерывания USB CDC
void device_app_usb_receive(const uint8_t *p, size_t n)
{
    // Если инициализация завершена, складываем принятые байты в кольцевой буфер Modbus с меткой
    // времени
    if (app_ready) modbus_ascii_receive(&device_modbus, p, n, HAL_GetTick());
}

// Обработчик сброса или отключения кабеля USB
void device_app_usb_reset(void)
{
    // При разрыве соединения запрашиваем сброс недопринятого кадра и очереди отправки Modbus
    if (app_ready) modbus_ascii_disconnect(&device_modbus);
}

// Начальная настройка выводов GPIO для линий CS и DRDY всех каналов платы
void device_board_gpio_init(void)
{
    // Инициализируем локальную таблицу пинов из макроса конфигурации платы
    device_stm32_channel_t map[DEVICE_CHANNELS] = DEVICE_BOARD_CHANNELS(0);
    GPIO_InitTypeDef gpio = {0}; // Обнуляем структуру настройки периферии GPIO HAL
    unsigned i; // Счётчик цикла по физическим каналам
    device_stm32_drdy_reset();
    DEVICE_BOARD_GPIO_CLOCK_ENABLE(); // Подаём тактирование на порты GPIO, используемые датчиками
    for (i = 0; i < DEVICE_BOARD_CHANNEL_COUNT; ++i) { // Проходим по всем физически распаянным каналам
        // Заранее выставляем высокий (неактивный) уровень на пине CS в выходном регистре ODR
        HAL_GPIO_WritePin(map[i].cs_port, map[i].cs_pin, GPIO_PIN_SET);
        gpio.Pin = map[i].cs_pin; // Указываем пин CS текущего канала
        gpio.Mode = GPIO_MODE_OUTPUT_PP; // Режим работы: двухтактный выход (Push-Pull)
        gpio.Pull = GPIO_PULLUP; // Включаем внутреннюю подтяжку к питанию для надёжного удержания единицы
        gpio.Speed = GPIO_SPEED_FREQ_HIGH; // Высокая скорость перезаряда фронтов для работы с SPI
        HAL_GPIO_Init(map[i].cs_port, &gpio); // Применяем конфигурацию к порту вывода CS
        gpio.Pin = map[i].drdy_pin; // Указываем пин готовности DRDY текущего канала
        gpio.Mode = GPIO_MODE_IT_FALLING; // DRDY активен по спадающему фронту.
        gpio.Pull = GPIO_PULLUP; // Подтяжка к плюсу, так как активный уровень DRDY у MAX31865 — низкий
        HAL_GPIO_Init(map[i].drdy_port, &gpio); // Применяем конфигурацию к порту вывода DRDY
        __HAL_GPIO_EXTI_CLEAR_IT(map[i].drdy_pin);
        {
            IRQn_Type irq = map[i].drdy_pin >= GPIO_PIN_10 ? EXTI15_10_IRQn :
                map[i].drdy_pin >= GPIO_PIN_5 ? EXTI9_5_IRQn : EXTI4_IRQn;
            HAL_NVIC_SetPriority(irq, 6, 0);
            HAL_NVIC_EnableIRQ(irq);
        }
    }
}

// Полная инициализация всех программных и аппаратных модулей прибора
void device_app_init(SPI_HandleTypeDef *spi)
{
    // Заполняем таблицу каналов с привязкой к переданному дескриптору SPI
    device_stm32_channel_t map[DEVICE_CHANNELS] = DEVICE_BOARD_CHANNELS(spi);
    unsigned i; // Счётчик цикла инициализации каналов
    app_ready = 0; // Запрещаем обработку входящих USB-пакетов на время старта
    memcpy(ports, map, sizeof(ports)); // Копируем локальную таблицу пинов и SPI в статический массив ports
    memset(&device_acquisition, 0, sizeof(device_acquisition)); // Очищаем структуру состояния сборщика кадров
    device_control_init(&device_control); // Инициализируем карту регистров Modbus значениями по умолчанию
    // Привязываем менеджер хранилища настроек к сектору Flash-памяти микроконтроллера
    device_flash_init(&store);
    // Загружаем последнюю валидную запись настроек из Flash (при отсутствии остаются defaults)
    (void)device_store_load(&store, &device_control.config);
    // Синхронизируем массив Holding-регистров Modbus с загруженной конфигурацией
    (void)registers_encode(&device_control.config, device_control.holding);
    // Инициализируем стек Modbus ASCII с адресом устройства и колбэками чтения/записи
    modbus_ascii_init(&device_modbus, DEVICE_MODBUS_ADDRESS, &device_control, read_map, device_control_write);
    // Включаем глобальное разрешение трассировки в ядре Cortex-M для доступа к блоку DWT
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0; // Обнуляем аппаратный счётчик тактов процессора
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk; // Запускаем счётчик тактов DWT->CYCCNT
    device_tick_hz = SystemCoreClock; // Сохраняем текущую частоту ядра в Гц для пересчёта тактов во время
    device_acquisition.ticks = device_stm32_ticks; // Подключаем функцию чтения DWT к модулю сбора данных
    // Подключаем функцию получения времени RTC для датирования кадров
    device_acquisition.timestamp = device_rtc_get;
    // Задаём режим синхронизации кадра (по всем каналам или по первому)
    device_acquisition.settings.trigger = DEVICE_TRIGGER_MODE;
    // Устанавливаем таймаут ожидания готовности датчиков
    device_acquisition.settings.timeout_ms = DEVICE_FRAME_TIMEOUT_MS;
    for (i = 0; i < DEVICE_BOARD_CHANNEL_COUNT; ++i) { // Инициализируем каждый физический канал MAX31865
        device_channel_t *c = &device_acquisition.channel[i]; // Получаем указатель на слот канала в сборщике
        c->device = &devices[i]; // Привязываем экземпляр структуры драйвера MAX31865
        c->ready_context = &ports[i]; // Передаём структуру с пинами как контекст для проверки DRDY
        c->ready = device_stm32_ready; // Назначаем функцию чтения пина DRDY
        // Записываем базовую конфигурацию (число проводов, фильтр 50/60 Гц) по SPI
        device_init_status[i] = max31865_init(&devices[i], &ports[i], device_stm32_transfer, DEVICE_SENSOR_WIRES, DEVICE_SENSOR_FILTER_HZ);
        // Если датчик не ответил или не подтвердил регистр конфигурации, поднимаем флаг аппаратной
        // ошибки
        if (device_init_status[i] != MAX31865_OK) device_control.hardware_error = 1;
    }
    device_rtc_init(); // Запускаем неблокирующую инициализацию часов реального времени
    phase = OFF; // Устанавливаем начальное состояние автомата измерений в OFF
    app_ready = 1; // Разрешаем приём команд по USB CDC
}

// Фиксация внутренней аппаратной ошибки прибора в карте регистров
static void hardware_failure(void)
{
    device_control.hardware_error = 1; // Выставляем флаг аппаратного сбоя (отражается в Discrete Inputs)
    device_control.error_code = 4; // Записываем код ошибки 4 (Slave Device Failure по стандарту Modbus)
}

// Шаг конечного автомата управления датчиками и процессом измерений
static void acquisition_poll(uint32_t now)
{
    // Читаем количество задействованных каналов из текущих настроек
    unsigned count = device_control.config.active_channels;
    // Пока идёт сеанс измерения, обновляем счётчик времени наработки в мс
    if (phase != OFF && phase != STOPPING) device_control.runtime_ms = now - run_since;

    // Если пришёл запрос на остановку или снят флаг RUN во время работы
    if (device_control.stop_requested || (!device_control.run_requested && phase != OFF && phase != STOPPING)) {
        device_control.stop_requested = 0; // Сбрасываем одноразовый флаг запроса остановки
        phase = STOPPING; // Переводим автомат в фазу пошагового выключения датчиков
        channel_index = 0; // Начинаем отключение с нулевого канала
        device_control.running = 0; // Сбрасываем внутренний признак активного сбора данных
        // Сбрасываем фильтры и помечаем выходные регистры всех каналов статусом STOPPED
        device_control_invalidate(&device_control, DEVICE_STOPPED);
    }

    // Если прибор остановлен и поступил запрос на старт измерений
    if (phase == OFF && device_control.run_requested) {
        phase = BIAS; // Переходим к фазе подачи тока смещения на датчики
        channel_index = 0; // Сбрасываем индекс канала для пошагового обхода
        run_since = now; // Фиксируем время старта сеанса
        device_control.runtime_ms = 0; // Обнуляем время работы текущего сеанса
        // Передаём актуальное число активных каналов в модуль сборщика
        device_acquisition.settings.channel_count = (uint8_t)count;
        device_acquisition.settings.channel_mask = (uint8_t)device_control.config.channel_mask;
        device_stm32_drdy_reset();
        device_acquisition.observed_mask = 0; // Очищаем маску замеченных сигналов DRDY от прошлых запусков
        device_acquisition.sequence = 0; // Сбрасываем нумерацию кадров
        // Переводим статусы каналов в режим прогрева цифровых фильтров
        device_control_invalidate(&device_control, DEVICE_FILTER_WARMUP);
    // Фаза поочерёдного включения напряжения смещения Vbias (по 1 каналу за вызов poll)
    } else if (phase == BIAS) {
        unsigned i = channel_index++; // Берём текущий индекс канала и сдвигаем указатель на следующий
        // Переинициализируем конфигурацию датчика перед запуском
        int selected = (device_control.config.channel_mask & (1u << i)) != 0;
        max31865_result_t r = selected ? max31865_init(&devices[i], &ports[i], device_stm32_transfer, DEVICE_SENSOR_WIRES, DEVICE_SENSOR_FILTER_HZ) : MAX31865_OK;
        // Если чип ответил корректно, включаем бит Vbias в его конфигурационном регистре
        if (selected && r == MAX31865_OK) r = max31865_bias(&devices[i]);
        device_init_status[i] = r; // Сохраняем статус инициализации канала для диагностики
        // Разрешаем опрос канала только при успешном включении Vbias
        device_acquisition.channel[i].enabled = (uint8_t)(selected && r == MAX31865_OK);
        if (r != MAX31865_OK) hardware_failure(); // При ошибке обмена фиксируем аппаратный сбой
        if (channel_index == count) { // Когда прошли все выбранные активные каналы
            since = now; // Засекаем время начала выдержки установления аналогового тракта
            phase = SETTLE; // Переводим автомат в фазу ожидания заряда емкостей линии RTD
        }
    } else if (phase == SETTLE) { // Фаза неблокирующего ожидания после включения Vbias
        // Если прошло заданное время установления (например, 10–20 мс)
        if ((uint32_t)(now - since) >= DEVICE_BIAS_SETTLE_MS) {
            phase = STARTING; // Переходим к фазе запуска непрерывных преобразований
            channel_index = 0; // Сбрасываем индекс канала в 0
        }
    // Фаза перевода исправных каналов в режим непрерывного преобразования (AUTO)
    } else if (phase == STARTING) {
        unsigned i = channel_index++; // Выбираем очередной канал
        if (device_acquisition.channel[i].enabled) { // Если канал успешно прошёл этап включения Vbias
            max31865_sample_t stale; // Временная структура для холостого вычитывания старых данных
            // После установления Vbias очищаем старые ошибки перед каждым START.
            max31865_result_t r = max31865_clear_faults(&devices[i]);
            // Читаем регистры АЦП, чтобы сбросить старый активный уровень DRDY.
            if (r == MAX31865_OK) r = max31865_read(&devices[i], &stale);
            // Если чтение прошло успешно, поднимаем бит режима непрерывных измерений (Conversion
            // Mode = Auto)
            if (r == MAX31865_OK) r = max31865_start(&devices[i]);
            device_init_status[i] = r; // Обновляем диагностический статус канала
            // Оставляем канал активным только при отсутствии ошибок
            device_acquisition.channel[i].enabled = (uint8_t)(r == MAX31865_OK);
            if (r != MAX31865_OK) { // Если при старте произошёл сбой
                // Пытаемся принудительно выключить Vbias и AUTO на проблемном чипе
                (void)max31865_stop(&devices[i]);
                hardware_failure(); // Выставляем глобальный флаг аппаратной ошибки
            }
        }
        if (channel_index == count) { // Когда все активные каналы переведены в режим AUTO
            phase = RUNNING; // Переводим автомат в режим циклического сбора кадров
            device_control.running = 1; // Поднимаем флаг активного процесса измерений
            device_acquisition.last_frame_ms = now; // Инициализируем точку отсчёта таймаута для первого кадра
        }
    } else if (phase == RUNNING) { // Штатная фаза сбора и обработки данных
        // Проверяем готовность кадра; если новый кадр вычитан из датчиков
        if (device_acquisition_poll(&device_acquisition, now, &device_last_frame)) {
            // Передаём кадр в цепочку фильтрации, расчёта температуры и обновления регистров Modbus
            device_control_frame(&device_control, &device_last_frame);
            modbus_ascii_measurement(&device_modbus);
        }
    } else if (phase == STOPPING) { // Фаза поочерёдного отключения всех физических каналов
        unsigned i = channel_index++; // Берём индекс очередного физического канала
        // Сбрасываем биты Vbias и AUTO в датчике; при ошибке SPI фиксируем сбой
        if (max31865_stop(&devices[i]) != MAX31865_OK) hardware_failure();
        device_acquisition.channel[i].enabled = 0; // Помечаем канал как отключённый
        // Когда обошли абсолютно все физические каналы платы, переходим в состояние OFF
        if (channel_index == DEVICE_BOARD_CHANNEL_COUNT) phase = OFF;
    }
}

// Выполнение отложенных сервисных команд (перезагрузка, запись Flash, установка RTC, сброс ошибок,
// сброс настроек)
static void commands_poll(void)
{
    uint16_t command = device_control.commands; // Читаем битовую маску запрошенной сервисной команды
    // Выполняем команду только при остановленных измерениях и после полной отправки USB-ответа
    // мастеру
    if (!command || phase != OFF || device_modbus.response_length || CDC_TransmitBusy_FS()) return;

    switch (command) { // Выбираем обработчик в зависимости от кода команды
    case DEVICE_CMD_REBOOT: // Команда программной перезагрузки микроконтроллера
        // Вызываем аппаратный системный сброс ядра Cortex-M через контроллер прерываний NVIC
        NVIC_SystemReset();
        return; // Выход (фактически управление сюда не вернётся)

    case DEVICE_CMD_SAVE: { // Команда сохранения текущих настроек во Flash-память
        if (!action_started) { // Если процесс записи во Flash ещё не был инициирован
            // Запускаем пословную запись структуры конфигурации в журнал Flash; если старт вернул
            // ошибку
            if (device_store_save(&store, &device_control.config)) {
                hardware_failure(); // Фиксируем аппаратную ошибку в регистрах статуса
                device_control.commands = 0; // Сбрасываем команду
            } else {
                action_started = 1; // Помечаем, что запись успешно стартовала и выполняется в фоне
            }
            return; // Возвращаем управление в главный цикл до следующего вызова poll
        }
        // Пока автомат записи Flash не перешёл в состояние покоя (0), продолжаем ждать
        if (store.state) return;
        // Если по завершении записи обнаружена ошибка верификации или программирования Flash,
        // поднимаем флаг аварии
        if (store.error) hardware_failure();
        break; // Запись завершена, выходим из switch для очистки флагов команды
    }

    case DEVICE_CMD_RTC: { // Команда установки нового времени в аппаратные часы RTC
        if (!action_started) { // Первый проход выполнения команды установки времени
            // Берём новое значение Unix-времени, записанное мастером в Holding-регистры
            uint32_t epoch = device_control.config.rtc_pending;
            // Если модуль RTC находится в аварийном состоянии (например, не запустился кварц)
            if (device_rtc_error) {
                hardware_failure(); // Выставляем код аппаратной ошибки
                device_control.commands = 0; // Снимаем команду
                return; // Выходим из функции
            }
            // Если автомат RTC ещё занят предыдущей операцией, ждём следующего цикла
            if (device_rtc_busy()) return;
            // Запускаем перевод RTC в режим INIT и передаём целевое время; если дата некорректна
            if (device_rtc_set(epoch)) {
                hardware_failure(); // Фиксируем ошибку выполнения команды
                device_control.commands = 0; // Очищаем бит команды
                return; // Завершаем обработку
            }
            action_started = 1; // Помечаем, что запрос на запись календаря отправлен в автомат RTC
            return; // Ждём завершения записи в следующих проходах главного цикла
        }
        // Если в процессе входа в режим INIT произошёл таймаут RTC, фиксируем отказ
        if (device_rtc_error) hardware_failure();
        // Если запись регистров RTC ещё продолжается, выходим без сброса команды
        else if (device_rtc_busy()) return;
        break; // Установка часов успешно завершена
    }

    case DEVICE_CMD_CLEAR: { // Команда очистки накопленных флагов ошибок датчиков и контроллера
        if (!action_started) { // При первом входе в команду очистки
            action_started = 1; // Поднимаем флаг активного процесса обхода каналов
            channel_index = 0; // Начинаем обход с нулевого физического канала
            device_control.error_code = 0; // Обнуляем регистр кода ошибки протокола/устройства
            device_control.hardware_error = 0; // Сбрасываем общий флаг аппаратной неисправности
        }
        {
            unsigned i = channel_index++; // Берём текущий физический канал и переходим к следующему.
            // Выключаем канал и посылаем команду сброса регистров ошибок (Fault Status Clear) в
            // MAX31865
            if (max31865_stop(&devices[i]) != MAX31865_OK || max31865_clear_faults(&devices[i]) != MAX31865_OK)
                hardware_failure(); // Если датчик не ответил по SPI, снова поднимаем флаг аппаратной ошибки
        }
        // Если опрошены ещё не все физические каналы, продолжаем на следующей итерации главного
        // цикла
        if (channel_index < DEVICE_BOARD_CHANNEL_COUNT) return;
        // После очистки всех датчиков обновляем статусы в карте регистров на STOPPED
        device_control_invalidate(&device_control, DEVICE_STOPPED);
        break; // Очистка полностью завершена
    }

    case DEVICE_CMD_DEFAULTS: { // Команда возврата рабочих настроек в ОЗУ к заводским значениям
        // Заполняем структуру конфигурации константами по умолчанию
        device_config_defaults(&device_control.config);
        // Перекодируем заводские настройки в массив Holding-регистров Modbus
        (void)registers_encode(&device_control.config, device_control.holding);
        // Сбрасываем состояние фильтров и выходных регистров измерений
        device_control_invalidate(&device_control, DEVICE_STOPPED);
        break; // Выходим для снятия флага команды
    }

    default: // Защита на случай попадания неизвестного бита команды
        hardware_failure(); // Фиксируем ошибку устройства
        break; // Переходим к сбросу регистра команд
    }

    device_control.commands = 0; // Очищаем регистр активной сервисной команды после её завершения
    action_started = 0; // Сбрасываем флаг пошагового выполнения для будущих команд
}

// Главная функция фонового цикла (вызывается непрерывно из while(1) в main.c)
void device_app_poll(void)
{
    // Снимаем текущее системное время в миллисекундах один раз на весь проход цикла
    uint32_t now = HAL_GetTick();
    device_rtc_poll(); // Продвигаем неблокирующий автомат инициализации и обновления часов RTC
    device_store_poll(&store); // Продвигаем пословную запись настроек во Flash-память, если она активна
    // Разбираем принятые байты Modbus ASCII, выполняем запросы к карте регистров и отправляем
    // ответы по USB
    modbus_ascii_poll(&device_modbus, now, 0, usb_send);
    acquisition_poll(now); // Выполняем шаг автомата управления датчиками MAX31865 и чтения кадров
    commands_poll(); // Обрабатываем отложенные сервисные команды (Save, RTC, Clear, Defaults, Reboot)
    // Если часы RTC перешли в состояние ошибки, отражаем это в глобальном статусе прибора
    if (device_rtc_error) hardware_failure();
}
