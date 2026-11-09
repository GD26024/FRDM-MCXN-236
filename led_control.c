#include <stdbool.h>

#include "fsl_clock.h"
#include "fsl_gpio.h"
#include "fsl_port.h"
#include "board.h"
#include "led_control.h"

static bool s_red_is_on;

void led_control_init(void)
{
    gpio_pin_config_t pin_config = {
        .pinDirection = kGPIO_DigitalOutput,
        .outputLogic = LOGIC_LED_OFF,
    };

    CLOCK_EnableClock(kCLOCK_Gpio4);
    CLOCK_EnableClock(kCLOCK_Port4);

    PORT_SetPinMux(PORT4, BOARD_LED_RED_GPIO_PIN, kPORT_MuxAlt0);
    PORT_SetPinMux(PORT4, BOARD_LED_GREEN_GPIO_PIN, kPORT_MuxAlt0);
    PORT_SetPinMux(PORT4, BOARD_LED_BLUE_GPIO_PIN, kPORT_MuxAlt0);

    GPIO_PinInit(BOARD_LED_RED_GPIO, BOARD_LED_RED_GPIO_PIN, &pin_config);
    GPIO_PinInit(BOARD_LED_GREEN_GPIO, BOARD_LED_GREEN_GPIO_PIN, &pin_config);
    GPIO_PinInit(BOARD_LED_BLUE_GPIO, BOARD_LED_BLUE_GPIO_PIN, &pin_config);
    s_red_is_on = false;
}

void led_control_all_off(void)
{
    LED_RED_OFF();
    LED_GREEN_OFF();
    LED_BLUE_OFF();
    s_red_is_on = false;
}

void led_control_set_color(led_color_t color)
{
    led_control_all_off();

    switch (color)
    {
        case LED_COLOR_RED:
            LED_RED_ON();
            s_red_is_on = true;
            break;
        case LED_COLOR_GREEN:
            LED_GREEN_ON();
            break;
        case LED_COLOR_BLUE:
            LED_BLUE_ON();
            break;
        default:
            break;
    }
}

void led_control_toggle_red(void)
{
    if (s_red_is_on)
    {
        led_control_all_off();
    }
    else
    {
        led_control_set_color(LED_COLOR_RED);
    }
}