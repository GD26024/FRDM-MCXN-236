/*
 * Copyright (c) 2013 - 2015, Freescale Semiconductor, Inc.
 * Copyright 2016-2017, 2024 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "board.h"
#include "fsl_clock.h"
#include "fsl_common.h"
#include "fsl_debug_console.h"
#include "fsl_gpio.h"
#include "fsl_lpuart.h"
#include "fsl_port.h"
#include "app.h"
#include "led_control.h"
#include "temperature.h"

#define COMMAND_BUFFER_SIZE 48U
#define BUTTON_DEBOUNCE_MS 30U

static volatile uint32_t s_milliseconds;
static bool s_sw2_raw_pressed;
static bool s_sw2_debounced_pressed;
static uint32_t s_sw2_raw_change_ms;

void SysTick_Handler(void)
{
    ++s_milliseconds;
}

static void print_help(void)
{
    PRINTF("Commands:\r\n");
    PRINTF("  HELP       Show this list\r\n");
    PRINTF("  LED RED    Turn on the red LED\r\n");
    PRINTF("  LED GREEN  Turn on the green LED\r\n");
    PRINTF("  LED BLUE   Turn on the blue LED\r\n");
    PRINTF("  LED OFF    Turn off all LEDs\r\n");
    PRINTF("  LED TOGGLE Toggle the red LED\r\n");
    PRINTF("  TEMP?      Read the MCU internal temperature\r\n");
    PRINTF("  UPTIME?    Show time since startup\r\n");
    PRINTF("  SW2        Press SW2 to print current uptime\r\n");
}

static void print_temperature(void)
{
    float temperature = temperature_read_celsius();
    int32_t hundredths = (int32_t)((temperature * 100.0f) + (temperature >= 0.0f ? 0.5f : -0.5f));

    if (hundredths < 0)
    {
        hundredths = -hundredths;
        PRINTF("MCU internal temperature: -%d.%d%d C\r\n", (int)(hundredths / 100),
               (int)((hundredths % 100) / 10), (int)(hundredths % 10));
    }
    else
    {
        PRINTF("MCU internal temperature: %d.%d%d C\r\n", (int)(hundredths / 100),
               (int)((hundredths % 100) / 10), (int)(hundredths % 10));
    }
}

static void print_uptime_value(void)
{
    uint32_t total_seconds = s_milliseconds / 1000U;
    uint32_t hours = total_seconds / 3600U;
    uint32_t minutes = (total_seconds % 3600U) / 60U;
    uint32_t seconds = total_seconds % 60U;

    if (hours < 10U)
    {
        PRINTF("0%u:", (unsigned int)hours);
    }
    else
    {
        PRINTF("%u:", (unsigned int)hours);
    }

    PRINTF("%u%u:%u%u\r\n", (unsigned int)(minutes / 10U), (unsigned int)(minutes % 10U),
           (unsigned int)(seconds / 10U), (unsigned int)(seconds % 10U));
}

static void configure_sw2(void)
{
    gpio_pin_config_t input_config = {
        .pinDirection = kGPIO_DigitalInput,
        .outputLogic = 0U,
    };

    CLOCK_EnableClock(kCLOCK_Gpio0);
    CLOCK_EnableClock(kCLOCK_Port0);
    PORT_SetPinMux(PORT0, BOARD_SW2_GPIO_PIN, kPORT_MuxAlt0);
    PORT0->PCR[BOARD_SW2_GPIO_PIN] =
        (PORT0->PCR[BOARD_SW2_GPIO_PIN] & ~PORT_PCR_IBE_MASK) | PORT_PCR_IBE(1U);
    GPIO_PinInit(BOARD_SW2_GPIO, BOARD_SW2_GPIO_PIN, &input_config);

    s_sw2_raw_pressed = (GPIO_PinRead(BOARD_SW2_GPIO, BOARD_SW2_GPIO_PIN) == 0U);
    s_sw2_debounced_pressed = s_sw2_raw_pressed;
    s_sw2_raw_change_ms = s_milliseconds;
}

static void poll_sw2(void)
{
    uint32_t now = s_milliseconds;
    bool raw_pressed = (GPIO_PinRead(BOARD_SW2_GPIO, BOARD_SW2_GPIO_PIN) == 0U);

    if (raw_pressed != s_sw2_raw_pressed)
    {
        s_sw2_raw_pressed = raw_pressed;
        s_sw2_raw_change_ms = now;
    }

    if ((raw_pressed != s_sw2_debounced_pressed) &&
        ((uint32_t)(now - s_sw2_raw_change_ms) >= BUTTON_DEBOUNCE_MS))
    {
        s_sw2_debounced_pressed = raw_pressed;
        if (s_sw2_debounced_pressed)
        {
            PRINTF("SW2 pressed. Uptime: ");
            print_uptime_value();
        }
    }
}

static void execute_command(char *command)
{
    for (size_t index = 0U; command[index] != '\0'; ++index)
    {
        command[index] = (char)toupper((unsigned char)command[index]);
    }

    if (strcmp(command, "HELP") == 0)
    {
        print_help();
    }
    else if (strcmp(command, "LED RED") == 0)
    {
        led_control_set_color(LED_COLOR_RED);
        PRINTF("Red LED is on.\r\n");
    }
    else if (strcmp(command, "LED GREEN") == 0)
    {
        led_control_set_color(LED_COLOR_GREEN);
        PRINTF("Green LED is on.\r\n");
    }
    else if (strcmp(command, "LED BLUE") == 0)
    {
        led_control_set_color(LED_COLOR_BLUE);
        PRINTF("Blue LED is on.\r\n");
    }
    else if (strcmp(command, "LED OFF") == 0)
    {
        led_control_all_off();
        PRINTF("All LEDs are off.\r\n");
    }
    else if (strcmp(command, "LED TOGGLE") == 0)
    {
        led_control_toggle_red();
        PRINTF("Red LED toggled.\r\n");
    }
    else if (strcmp(command, "TEMP?") == 0)
    {
        print_temperature();
    }
    else if (strcmp(command, "UPTIME?") == 0)
    {
        PRINTF("Uptime: ");
        print_uptime_value();
    }
    else if (command[0] != '\0')
    {
        PRINTF("Unknown command. Type HELP.\r\n");
    }
}

static void run_command_loop(void)
{
    char command[COMMAND_BUFFER_SIZE];
    size_t length = 0U;
    bool overflow = false;
    bool swallow_line_feed = false;

    while (1)
    {
        int character = -1;

        if ((LPUART_GetStatusFlags(LPUART4) & (uint32_t)kLPUART_RxDataRegFullFlag) != 0U)
        {
            character = (int)LPUART_ReadByte(LPUART4);
        }

        if (character >= 0)
        {
            if ((character == '\n') && swallow_line_feed)
            {
                swallow_line_feed = false;
            }
            else if ((character == '\r') || (character == '\n'))
            {
                swallow_line_feed = (character == '\r');
                PUTCHAR('\r');
                PUTCHAR('\n');

                if (overflow)
                {
                    PRINTF("Command too long. Type HELP.\r\n");
                }
                else
                {
                    command[length] = '\0';
                    execute_command(command);
                }

                length = 0U;
                overflow = false;
            }
            else
            {
                swallow_line_feed = false;

                if ((character == '\b') || (character == 127))
                {
                    if (length > 0U)
                    {
                        --length;
                        PUTCHAR('\b');
                        PUTCHAR(' ');
                        PUTCHAR('\b');
                    }
                }
                else if ((character >= 32) && (character <= 126))
                {
                    if (length < (COMMAND_BUFFER_SIZE - 1U))
                    {
                        command[length++] = (char)character;
                        PUTCHAR(character);
                    }
                    else
                    {
                        overflow = true;
                    }
                }
            }
        }

        poll_sw2();
    }
}

int main(void)
{
    BOARD_InitHardware();
    led_control_init();
    temperature_init();

    if (SysTick_Config(SystemCoreClock / 1000U) != 0U)
    {
        PRINTF("Error: SysTick configuration failed.\r\n");
    }

    configure_sw2();

    PRINTF("FRDM-MCXN236 UART Lab\r\n");
    PRINTF("Temperature after reset/startup: ");
    print_temperature();
    PRINTF("Type HELP to list commands.\r\n");
    run_command_loop();

    return 0;
}
