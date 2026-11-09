#ifndef LED_CONTROL_H
#define LED_CONTROL_H

typedef enum
{
    LED_COLOR_RED,
    LED_COLOR_GREEN,
    LED_COLOR_BLUE
} led_color_t;

void led_control_init(void);
void led_control_set_color(led_color_t color);
void led_control_all_off(void);
void led_control_toggle_red(void);

#endif