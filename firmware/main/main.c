#include <stdio.h>
#include <string.h>
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "usbpd_esp.h"
#include "driver/i2c.h"
#include "dekatron.h"
#include "esp_netif_types.h"
#include "wifi_manager.h"
#include "driver/gpio.h"
#include "snmpgetter.h"
#include "webconfig.h"
#include "io.h"

static const char *TAG="main";

static SemaphoreHandle_t deka_start_sema;

/*
Callback for USB-PD driver. We want 12V but are also happy with 9 or 15V.
*/
static int prefered_idx=0;
static int usbpd_cb(int type, int mv, int ma) {
	const int preference[]={15000, 9000, 12000, 0}; //most prefered is last
	if (type==USBPD_ESP_CB_INITIAL) prefered_idx=0;
	if (type==USBPD_ESP_CB_CHOSEN) {
		ESP_LOGI(TAG, "Chosen USB-PD config: %d mV @ %d mA", mv, ma);
		webconfig_set_usbpd(mv, ma);
		//If we picked something compatible, turn led off, if not blink fast.
		int can_start=0;
		io_led_blink_set(LED_RED, BLINK_FAST);
		for (int i=0; preference[i]!=0; i++) {
			if (preference[i]==mv) {
				io_led_blink_set(LED_RED, BLINK_OFF);
				can_start=1;
			}
		}
		if (can_start) xSemaphoreGive(deka_start_sema);
	} else {
		for (int i=0; preference[i]!=0; i++) {
			if (mv==preference[i] && prefered_idx<=i) {
				prefered_idx=i;
				return 1; //pick this one
			}
		}
	}
	return 0;
}

//Connection state, to blink the green LED. Note that the flags are set from
//various threads, so we need a mux to serialize access.
#define FLAG_CONNECTED (1<<0)
#define FLAG_AP (1<<1)
#define FLAG_SNMP (1<<2)
static int conn_flags=0;
static SemaphoreHandle_t conn_flag_mutex;

static void set_conn_flag(int bitmask, int set) {
	xSemaphoreTake(conn_flag_mutex, portMAX_DELAY);
	if (set) conn_flags|=bitmask; else conn_flags&=~bitmask;
	printf("Conn flags %x\n", conn_flags);
	if (conn_flags&FLAG_AP) {
		io_led_blink_set(LED_GREEN, BLINK_ON);
	} else if ((conn_flags&FLAG_CONNECTED)==0) {
		io_led_blink_set(LED_GREEN, BLINK_SLOW);
	} else if ((conn_flags&FLAG_SNMP)==0) {
		io_led_blink_set(LED_GREEN, BLINK_FAST);
	} else {
		io_led_blink_set(LED_GREEN, BLINK_OFF);
	}
	xSemaphoreGive(conn_flag_mutex);
}

//Callback for when we have a network connection.
static void cb_connection_ok(void *pvParameter) {
	ip_event_got_ip_t* param = (ip_event_got_ip_t*)pvParameter;
	char str_ip[32];
	esp_ip4addr_ntoa(&param->ip_info.ip, str_ip, 32);
	ESP_LOGI(TAG, "I have a connection and my IP is %s!", str_ip);
	for (int i=0; str_ip[i]!=0; i++) {
		deka_queue_anim(DEKA_ANIM_TYPE_CHAR, str_ip[i], 10000, 2000);
	}
	deka_queue_anim(DEKA_ANIM_TYPE_SPIN, 0, 10000, 0);
	set_conn_flag(FLAG_CONNECTED, 1);
}

static void cb_connection_disconnected(void *pvParameter) {
	set_conn_flag(FLAG_CONNECTED, 0);
}

static void cb_connection_apstart(void *pvParameter) {
	set_conn_flag(FLAG_AP, 1);
}

static void cb_connection_apstop(void *pvParameter) {
	set_conn_flag(FLAG_AP, 0);
}


static void dekatron_start() {
	ESP_LOGI(TAG, "Snmpgetter start");
	char snmpip[256], community[256], oid_in[256], oid_out[256];
	webconfig_get_config_str("snmpip", snmpip, sizeof(snmpip));
	webconfig_get_config_str("community", community, sizeof(community));
	webconfig_get_config_str("oid_in", oid_in, sizeof(oid_in));
	webconfig_get_config_str("oid_out", oid_out, sizeof(oid_out));
	ESP_LOGI(TAG, "Using config snmpip=%s community=%s oid_in=%s oid_out=%s", 
			snmpip, community, oid_in, oid_out);
	snmpgetter_start(snmpip, 161, community, oid_in, oid_out);
	char rot_str[16];
	webconfig_get_config_str("rotation", rot_str, sizeof(rot_str));
	deka_set_rotation(atoi(rot_str));
}

//This allows you to enter something like '0.92G' and it'll parse it to a proper
//value.
static uint64_t get_max_bw() {
	char bw_str[16];
	webconfig_get_config_str("max_bw_bps", bw_str, sizeof(bw_str));
	float f=atof(bw_str);
	char ind=bw_str[strlen(bw_str)-1];
	if (ind=='g' || ind=='G') f=f*1024*1024*1024;
	if (ind=='m' || ind=='M') f=f*1024*1024;
	if (ind=='k' || ind=='K') f=f*1024;
	return f;
}



#define PRESS_DUR_LONG 30 //3 seconds
//called every 100ms
void btn_callback(void *arg) {
	static int press_dur=0;
	if (io_btn_pressed()) {
		press_dur++;
		if (press_dur==PRESS_DUR_LONG) {
			//long pressed
			wifi_manager_send_message(WM_ORDER_DISCONNECT_STA, NULL);
			wifi_manager_send_message(WM_ORDER_START_AP, NULL);
		}
	} else {
		if (press_dur!=0 && press_dur<PRESS_DUR_LONG) {
			//short pressed
		}
		press_dur=0;
	}
}

void app_main(void) {
	io_init();
	io_led_blink_set(LED_RED, BLINK_SLOW);
	io_led_blink_set(LED_GREEN, BLINK_SLOW);
	
	deka_start_sema=xSemaphoreCreateBinary();
	conn_flag_mutex=xSemaphoreCreateMutex();
	const esp_timer_create_args_t btn_timer_args = {
		.callback = &btn_callback,
		.name = "btn"
	};
	esp_timer_handle_t btn_timer;
	ESP_ERROR_CHECK(esp_timer_create(&btn_timer_args, &btn_timer));
	ESP_ERROR_CHECK(esp_timer_start_periodic(btn_timer, 100000));

	deka_init();

	int i2c_master_port = 0;

	i2c_config_t conf = {
		.mode = I2C_MODE_MASTER,
		.sda_io_num = 4,
		.scl_io_num = 5,
		.sda_pullup_en = GPIO_PULLUP_ENABLE,
		.scl_pullup_en = GPIO_PULLUP_ENABLE,
		.master.clk_speed = 400000,
	};
	ESP_ERROR_CHECK(i2c_param_config(i2c_master_port, &conf));
	ESP_ERROR_CHECK(i2c_driver_install(i2c_master_port, conf.mode, 0, 0, 0));
	ESP_ERROR_CHECK(usbpd_esp_init(i2c_master_port, usbpd_cb));

	wifi_manager_start();
	wifi_manager_set_callback(WM_EVENT_STA_GOT_IP, &cb_connection_ok);
	wifi_manager_set_callback(WM_ORDER_DISCONNECT_STA, &cb_connection_disconnected);
	wifi_manager_set_callback(WM_ORDER_START_AP, &cb_connection_apstart);
	wifi_manager_set_callback(WM_ORDER_STOP_AP, &cb_connection_apstop);
	webconfig_start();

	dekatron_start();

	//note: field is in *bit* per second so we convert to *bytes* per second as
	//everything else is in bytes per second as well.
	uint64_t max_bw_bps=get_max_bw()/8;

	//Wait for succesful USB PD negotiation to start HV PSU
	ESP_LOGI(TAG, "Wait for USB-PD negotiations");
	set_conn_flag(FLAG_SNMP, 1); //to stop blinking green LED
	xSemaphoreTake(deka_start_sema, portMAX_DELAY);
	deka_start();

	ESP_LOGI(TAG, "Snmpgetter query start");
	while(1) {
		snmpgetter_bw_t bw;
		int r=0;
		do {
			r=snmpgetter_get_bw(&bw, pdMS_TO_TICKS(2000));
			set_conn_flag(FLAG_SNMP, r);
		} while (!r);
		ESP_LOGI(TAG, "in %d Kbps out %d Kbps", bw.bps_in/1024, bw.bps_out/1024);
		float max_speed_rps=20;
		float speed_in_rps=(max_speed_rps*bw.bps_in)/max_bw_bps;
		float speed_out_rps=(max_speed_rps*bw.bps_out)/max_bw_bps;
		float speed_rps=(speed_in_rps>speed_out_rps)?speed_in_rps:speed_out_rps;
		if (speed_rps>max_speed_rps) speed_rps=max_speed_rps;
		if (speed_rps<0.01) speed_rps=0.01; //don't divide by zero
		int delay_us=((1000000.0/30)/speed_rps);
		//printf("speed_rps %f delay %d\n", speed_rps, delay_us);
		deka_queue_anim(DEKA_ANIM_TYPE_SPIN, (bw.bps_in>bw.bps_out)?1:0, delay_us, 0);
		vTaskDelay(pdMS_TO_TICKS(500));
	}
}
