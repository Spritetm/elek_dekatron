#include <stdio.h>
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "usbpd_esp.h"
#include "driver/i2c.h"

static const char *TAG="main";


static void ledc_init(void) {
	// Prepare and then apply the LEDC PWM timer configuration
	ledc_timer_config_t ledc_timer = {
		.speed_mode		  = LEDC_LOW_SPEED_MODE,
		.timer_num		  = LEDC_TIMER_0,
		.duty_resolution  = LEDC_TIMER_9_BIT,
		.freq_hz		  = 100000,
		.clk_cfg		  = LEDC_AUTO_CLK
	};
	ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

	// Prepare and then apply the LEDC PWM channel configuration
	ledc_channel_config_t ledc_channel = {
		.speed_mode		= LEDC_LOW_SPEED_MODE,
		.channel		= LEDC_CHANNEL_0,
		.timer_sel		= LEDC_TIMER_0,
		.intr_type		= LEDC_INTR_DISABLE,
		.gpio_num		= 6,
		.duty			= 950
	};
	ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}

void app_main(void) {
	ledc_init();

	adc_oneshot_unit_handle_t adc1_handle;
	adc_oneshot_unit_init_cfg_t init_config1 = {
		.unit_id = ADC_UNIT_1,
	};
	ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

	adc_oneshot_chan_cfg_t config = {
		.bitwidth = ADC_BITWIDTH_DEFAULT,
		.atten = ADC_ATTEN_DB_0,
	};
	ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_4, &config));

	//-------------ADC1 Calibration Init---------------//
	adc_cali_handle_t adc_cali_handle;
	ESP_LOGI(TAG, "calibration scheme version is %s", "Curve Fitting");
	adc_cali_curve_fitting_config_t cali_config = {
		.unit_id = ADC_UNIT_1,
		.chan = ADC_CHANNEL_4,
		.atten = ADC_ATTEN_DB_0,
		.bitwidth = ADC_BITWIDTH_DEFAULT,
	};
	ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_config, &adc_cali_handle));
	
	
	int i2c_master_port = 0;

	i2c_config_t conf = {
		.mode = I2C_MODE_MASTER,
		.sda_io_num = 4,
		.scl_io_num = 5,
		.sda_pullup_en = GPIO_PULLUP_ENABLE,
		.scl_pullup_en = GPIO_PULLUP_ENABLE,
		.master.clk_speed = 100000,
	};
	ESP_ERROR_CHECK(i2c_param_config(i2c_master_port, &conf));
	ESP_ERROR_CHECK(i2c_driver_install(i2c_master_port, conf.mode, 0, 0, 0));
	ESP_ERROR_CHECK(usbpd_esp_init(i2c_master_port));
	return;
	
	while(1) {
		int adc_raw, voltage;
		ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ADC_CHANNEL_4, &adc_raw));
		ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc_cali_handle, adc_raw, &voltage));
		printf("Raw %d volt %d\n", adc_raw, voltage);
		vTaskDelay(pdMS_TO_TICKS(500));
	}
	
	// Set duty to 50%
	ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 4096));
	// Update duty to apply the new value
	ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
}
