#include "device_flash.h"
#include "device_board_stm32.h"

// Метка времени (HAL_GetTick) начала записи текущего 32-битного слова для контроля таймаута
static uint32_t started;

// Запуск программирования одного 32-битного слова во Flash-память по смещению offset
static int begin_word(void *unused, size_t offset, uint32_t word)
{
    (void)unused; // Контекст не используется
    // Отказываем, если контроллер Flash сейчас занят или смещение выходит за границы выделенного
    // сектора
    if ((FLASH->SR & FLASH_SR_BSY) || offset >= DEVICE_FLASH_SIZE / 4u) return -1;
    // Если регистр управления FLASH->CR заблокирован от записи (бит LOCK == 1)
    if (FLASH->CR & FLASH_CR_LOCK) {
        // Записываем первый аппаратный ключ разблокировки Flash STM32F4 в регистр KEYR
        FLASH->KEYR = 0x45670123u;
        FLASH->KEYR = 0xcdef89abu; // Записываем второй аппаратный ключ разблокировки Flash STM32F4
    }
    // Если после ввода ключей бит LOCK не снялся, возвращаем ошибку аппаратуры
    if (FLASH->CR & FLASH_CR_LOCK) return -1;
    // Сбрасываем все флаги завершения и ошибок в статусном регистре FLASH->SR записью единиц
    // (rc_w1)
    FLASH->SR = FLASH_SR_EOP | FLASH_SR_OPERR | FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR;
    // Очищаем биты стирания/размера и выставляем PSIZE = 10b (программирование по 32 бита при
    // питании 2.7..3.6 В) и бит PG (Programming)
    FLASH->CR = (FLASH->CR & ~(FLASH_CR_PSIZE | FLASH_CR_SER | FLASH_CR_MER | FLASH_CR_PG)) | FLASH_CR_PSIZE_1 | FLASH_CR_PG;
    started = HAL_GetTick(); // Фиксируем время старта физической записи слова в миллисекундах
    // Выполняем прямую запись 32-битного слова по целевому физическому адресу во Flash, инициируя
    // цикл программирования
    *(volatile uint32_t *)(uintptr_t)(DEVICE_FLASH_BASE + offset * 4u) = word;
    return 0; // Операция записи успешно запущена
}

// Опрос завершения записи слова во Flash и сброс кэша данных ART-ускорителя
static int poll_word(void *unused)
{
    uint32_t errors; // Переменная для маски аппаратных ошибок программирования
    (void)unused; // Контекст не используется
    // Пока стоит бит занятости BSY: если прошло больше 100 мс — возвращаем ошибку таймаута (-1),
    // иначе 0 (продолжаем ждать)
    if (FLASH->SR & FLASH_SR_BSY) return (uint32_t)(HAL_GetTick() - started) > 100u ? -1 : 0;
    // Считываем флаги ошибок (защита от записи, ошибка выравнивания, параллелизма или
    // последовательности)
    errors = FLASH->SR & (FLASH_SR_OPERR | FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR);
    // Снимаем бит режима программирования PG и снова запираем контроллер Flash битом LOCK
    FLASH->CR = (FLASH->CR & ~FLASH_CR_PG) | FLASH_CR_LOCK;

    { // Блок принудительного сброса кэша данных (Data Cache) контроллера ART Accelerator
        // Запоминаем, был ли включён кэш данных (бит DCEN в регистре FLASH->ACR)
        uint32_t enabled = FLASH->ACR & FLASH_ACR_DCEN;
        // Перед сбросом кэша по документации STM32F4 необходимо обязательно выключить бит DCEN
        FLASH->ACR &= ~FLASH_ACR_DCEN;
        // Устанавливаем бит DCRST для полной инвалидации строк кэша данных (чтобы процессор читал
        // обновлённые ячейки Flash, а не старый кэш)
        FLASH->ACR |= FLASH_ACR_DCRST;
        FLASH->ACR &= ~FLASH_ACR_DCRST; // Снимаем бит сброса DCRST
        FLASH->ACR |= enabled; // Восстанавливаем исходное состояние бита включения кэша данных DCEN
    }
    // Если были зафиксированы аппаратные ошибки — возвращаем -1, иначе 1 (слово успешно
    // запрограммировано)
    return errors ? -1 : 1;
}

// Инициализация структуры абстрактного хранилища указателями на сектор Flash и функции записи
void device_flash_init(device_store_t *s)
{
    // Устанавливаем указатель для прямого чтения массива 32-битных слов из сектора Flash
    s->memory = (const volatile uint32_t *)(uintptr_t)DEVICE_FLASH_BASE;
    s->word_count = DEVICE_FLASH_SIZE / 4u; // Вычисляем ёмкость сектора в 32-битных словах
    s->begin_word = begin_word; // Привязываем callback старта программирования слова
    s->poll_word = poll_word; // Привязываем callback проверки окончания программирования
    s->context = 0; // Дополнительный контекст не требуется (передаём NULL)
}
