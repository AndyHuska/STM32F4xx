#include "driver.h"

#if defined(BOARD_PMC_02_AA)

void board_init (void)
{
    GPIO_InitTypeDef gpio = {
        .Pin = GPIO_PIN_0,
        .Mode = GPIO_MODE_OUTPUT_OD,
        .Pull = GPIO_NOPULL,
        .Speed = GPIO_SPEED_FREQ_LOW
    };

    __HAL_RCC_GPIOD_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_0, GPIO_PIN_SET);
    HAL_GPIO_Init(GPIOD, &gpio);

    gpio.Pin = GPIO_PIN_1;
    gpio.Mode = GPIO_MODE_INPUT;
    HAL_GPIO_Init(GPIOD, &gpio);

#if PMC_OUTPUTS_ENABLE
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_0, GPIO_PIN_RESET);
#endif
}

#endif