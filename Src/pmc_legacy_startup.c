#include "main.h"

#if defined(BOARD_PMC_02_AA) && defined(PMC_LEGACY_BOOTLOADER)

static void inhibit_outputs(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIODEN;
    (void)RCC->AHB1ENR;
    GPIOD->BSRR = 1U;
    GPIOD->OTYPER |= 1U;
    GPIOD->OSPEEDR &= ~3U;
    GPIOD->PUPDR &= ~3U;
    GPIOD->MODER = (GPIOD->MODER & ~3U) | 1U;
    __DSB();
}

static void startup_failed(void)
{
    inhibit_outputs();
    while(1)
        __NOP();
}

void pmc_legacy_early_init(void)
{
    __disable_irq();
    inhibit_outputs();
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;

    for(uint32_t index = 0; index < 8; index++) {
        NVIC->ICER[index] = UINT32_MAX;
        NVIC->ICPR[index] = UINT32_MAX;
    }
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;
    __set_BASEPRI(0);
    __set_FAULTMASK(0);

    RCC->AHB1RSTR |= RCC_AHB1RSTR_ETHMACRST | RCC_AHB1RSTR_DMA1RST | RCC_AHB1RSTR_DMA2RST;
    __DSB();
    (void)RCC->AHB1RSTR;
    RCC->AHB1RSTR &= ~(RCC_AHB1RSTR_ETHMACRST | RCC_AHB1RSTR_DMA1RST | RCC_AHB1RSTR_DMA2RST);
    __DSB();

    FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_LATENCY) | FLASH_ACR_LATENCY_5WS;
    (void)FLASH->ACR;
    RCC->CR |= RCC_CR_HSION;
    uint32_t timeout = 1000000U;
    while(!(RCC->CR & RCC_CR_HSIRDY) && --timeout)
        __NOP();
    if(!timeout)
        startup_failed();

    RCC->CFGR &= ~RCC_CFGR_SW;
    timeout = 1000000U;
    while((RCC->CFGR & RCC_CFGR_SWS) && --timeout)
        __NOP();
    if(!timeout)
        startup_failed();

    RCC->CR &= ~(RCC_CR_PLLON | RCC_CR_PLLI2SON);
    timeout = 1000000U;
    while((RCC->CR & (RCC_CR_PLLRDY | RCC_CR_PLLI2SRDY)) && --timeout)
        __NOP();
    if(!timeout)
        startup_failed();

    RCC->CR &= ~(RCC_CR_HSEON | RCC_CR_CSSON | RCC_CR_HSEBYP);
    RCC->CFGR = 0;
    RCC->PLLCFGR = UINT32_C(0x24003010);
    RCC->CIR = UINT32_C(0x00FF0000);

    RCC->AHB1RSTR = UINT32_MAX;
    RCC->AHB2RSTR = UINT32_MAX;
    RCC->AHB3RSTR = UINT32_MAX;
    RCC->APB1RSTR = UINT32_MAX;
    RCC->APB2RSTR = UINT32_MAX;
    __DSB();
    RCC->AHB1RSTR = RCC->AHB2RSTR = RCC->AHB3RSTR = 0;
    RCC->APB1RSTR = RCC->APB2RSTR = 0;
    RCC->AHB1ENR = RCC_AHB1ENR_CCMDATARAMEN | RCC_AHB1ENR_GPIODEN;
    RCC->AHB2ENR = RCC->AHB3ENR = RCC->APB1ENR = RCC->APB2ENR = 0;
    inhibit_outputs();
    SCB->VTOR = UINT32_C(0x08010200);
    __DSB();
    __ISB();
}

#endif