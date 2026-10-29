/**
 * @file pt100_flash.c
 * @brief Аппаратный адаптер журнала для выделенного сектора Flash STM32F4.
 */
#include "pt100_flash.h"
#include "pt100_board.h"
static uint32_t started;
static int begin_word(void *unused,size_t offset,uint32_t word)
{
    (void)unused;
    if ((FLASH->SR&FLASH_SR_BSY) || offset>=PT100_FLASH_SIZE/4u) return -1;
    if (FLASH->CR&FLASH_CR_LOCK) { FLASH->KEYR=0x45670123u; FLASH->KEYR=0xcdef89abu; }
    if (FLASH->CR&FLASH_CR_LOCK) return -1;
    FLASH->SR=FLASH_SR_EOP|FLASH_SR_OPERR|FLASH_SR_WRPERR|FLASH_SR_PGAERR|FLASH_SR_PGPERR|FLASH_SR_PGSERR;
    FLASH->CR=(FLASH->CR&~(FLASH_CR_PSIZE|FLASH_CR_SER|FLASH_CR_MER|FLASH_CR_PG))|FLASH_CR_PSIZE_1|FLASH_CR_PG;
    started=HAL_GetTick();
    *(volatile uint32_t *)(uintptr_t)(PT100_FLASH_BASE+offset*4u)=word;
    return 0;
}
static int poll_word(void *unused)
{
    uint32_t errors;
    (void)unused;
    if (FLASH->SR&FLASH_SR_BSY) return (uint32_t)(HAL_GetTick()-started)>100u ? -1 : 0;
    errors=FLASH->SR&(FLASH_SR_OPERR|FLASH_SR_WRPERR|FLASH_SR_PGAERR|FLASH_SR_PGPERR|FLASH_SR_PGSERR);
    FLASH->CR=(FLASH->CR&~FLASH_CR_PG)|FLASH_CR_LOCK;
    /* Invalidate cached data after programming the journal. */
    {
        uint32_t enabled=FLASH->ACR&FLASH_ACR_DCEN;
        FLASH->ACR &= ~FLASH_ACR_DCEN;
        FLASH->ACR |= FLASH_ACR_DCRST;
        FLASH->ACR &= ~FLASH_ACR_DCRST;
        FLASH->ACR |= enabled;
    }
    return errors ? -1 : 1;
}
/**
 * @brief Привязывает переносимый журнал настроек к выделенной Flash STM32F4.
 * @param[in,out] s Контекст журнала; заполняются адрес, размер и callbacks программирования.
 * @details Ничего не стирает и не загружает: следующим шагом вызвать pt100_store_load().
 * PT100_FLASH_BASE/PT100_FLASH_SIZE должны описывать область, исключённую из размещения
 * прошивки linker-скриптом. Адаптер рассчитан на один журнал и программирование 32-битных слов.
 * @note Пошаговый API не исключает коротких аппаратных задержек выборки команд
 * из однобанковой Flash во время программирования.
 */
void pt100_flash_init(pt100_store_t *s)
{
    s->memory=(const volatile uint32_t *)(uintptr_t)PT100_FLASH_BASE;
    s->word_count=PT100_FLASH_SIZE/4u; s->begin_word=begin_word; s->poll_word=poll_word;
    s->context=0;
}
