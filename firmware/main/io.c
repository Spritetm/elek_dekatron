//Some I/O functions live here: LEDs and button code.
/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Jeroen Domburg <jeroen@spritesmods.com> wrote this file. As long as you retain 
 * this notice you can do whatever you want with this stuff. If we meet some day, 
 * and you think this stuff is worth it, you can buy me a beer in return. 
 * ----------------------------------------------------------------------------
 */


#include <stdio.h>
#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "io.h"
#include "driver/gpio.h"

int led_r_state=0, led_g_state=0;


static void set_led(int gpio, int state, int t) {
	int s=0;
	if (state==BLINK_SLOW) s=(t&2);
	if (state==BLINK_FAST) s=(t&1);
	if (state==BLINK_ON) s=1;
	gpio_set_level(gpio, s);
}

void blink_callback() {
	static unsigned int t=0;
	t++;
	set_led(IO_LEDR, led_r_state, t);
	set_led(IO_LEDG, led_g_state, t+1);
}


void io_led_blink_set(int led, int blink) {
	if (led==LED_RED) led_r_state=blink;
	if (led==LED_GREEN) led_g_state=blink;
}

void io_init() {
	gpio_config_t cfg_in={
		.pin_bit_mask=(1<<IO_BTN),
		.mode=GPIO_MODE_INPUT,
		.pull_up_en=GPIO_PULLUP_ENABLE
	};
	gpio_config(&cfg_in);
	gpio_config_t cfg_out={
		.pin_bit_mask=(1<<IO_LEDR)|(1<<IO_LEDG),
		.mode=GPIO_MODE_OUTPUT,
	};
	gpio_config(&cfg_out);

	const esp_timer_create_args_t blink_timer_args = {
		.callback = &blink_callback,
		.name = "blink"
	};
	esp_timer_handle_t blink_timer;
	ESP_ERROR_CHECK(esp_timer_create(&blink_timer_args, &blink_timer));
	ESP_ERROR_CHECK(esp_timer_start_periodic(blink_timer, 200000));
}

int io_btn_pressed() {
	return !gpio_get_level(IO_BTN);
}

