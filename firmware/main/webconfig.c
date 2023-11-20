#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include "esp_netif.h"
#include <esp_http_server.h>
#include "nvs.h"
#include "nvs_flash.h"
#include <cJSON.h>
#include "esp_timer.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "dekatron.h"

#include "wifi_manager.h"
#include "http_app.h"
#include "webconfig.h"

static const char* TAG = "webconfig";

extern const char root_html_start[] asm("_binary_root_html_start");
extern const char root_html_end[] asm("_binary_root_html_end");

//keep in sync with html
static const char* fields[]={"snmpip", "community", "oid_in", "oid_out", "max_bw_bps", "rotation", NULL};
static const char* defaults[]={"10.0.0.1", "public", ".1.3.6.1.2.1.2.2.1.10.1", ".1.3.6.1.2.1.2.2.1.16.1", "1G", "0"};

static nvs_handle_t nvs;


//default to un-negotiated USB voltages
static int usbpd_mv=5000;
static int usbpd_ma=100;

void webconfig_set_usbpd(int mv, int ma) {
	usbpd_mv=mv;
	usbpd_ma=ma;
}

static esp_err_t webconfig_get_handler(httpd_req_t *req) {
	if(strcmp(req->uri, "/") == 0) {
		httpd_resp_set_status(req, "200 OK");
		httpd_resp_set_type(req, "text/html");
		httpd_resp_send(req, root_html_start, root_html_end-root_html_start);
	} else if(strcmp(req->uri, "/getfields") == 0) {
		cJSON *root=cJSON_CreateObject();
		cJSON *vars=cJSON_AddArrayToObject(root, "vars");
		int i=0;
		while (fields[i]!=NULL) {
			char buf[256];
			size_t length=sizeof(buf);
			if (nvs_get_str(nvs, fields[i], buf, &length)!=ESP_OK) {
				//technically shouldn't happen as we set defaults early on, but hey, belts'n'braces...
				strcpy(buf, defaults[i]);
			}
			cJSON *var=cJSON_CreateObject();
			cJSON_AddStringToObject(var, "el", fields[i]);
			cJSON_AddStringToObject(var, "val", buf);
			cJSON_AddItemToArray(vars, var);
			i++;
		}
		cJSON_AddNumberToObject(root, "usbpd_mv", usbpd_mv);
		cJSON_AddNumberToObject(root, "usbpd_ma", usbpd_ma);
		cJSON_AddNumberToObject(root, "deka_pwm", deka_get_pwm());
		cJSON_AddNumberToObject(root, "deka_posdet", deka_get_posdet_ct());
		httpd_resp_set_status(req, "200 OK");
		httpd_resp_set_type(req, "text/json");
		const char *txt=cJSON_Print(root);
		if (txt) {
			httpd_resp_send(req, txt, strlen(txt));
		}
		cJSON_Delete(root);
	} else {
		httpd_resp_send_404(req);
	}
	return ESP_OK;
}

static void reset_cb(void *arg) {
	ESP_LOGE(TAG, "Rebooting SoC.");
	esp_restart();
}

static esp_err_t webconfig_post_handler(httpd_req_t *req) {
	if(strcmp(req->uri, "/setfields") == 0) {
		char *buf=malloc(req->content_len);
		int p=0;
		while (p!=req->content_len) {
			int ret=httpd_req_recv(req, buf+p, req->content_len-p);
			if (ret>=0) {
				p+=ret;
			} else if (ret==HTTPD_SOCK_ERR_TIMEOUT) {
				//just wait a bit longer
			} else {
				ESP_LOGE(TAG, "httpd_req_recv failed");
				httpd_resp_send_500(req);
				return ESP_OK;
			}
		}
		cJSON *root = cJSON_Parse(buf);
		if (root) {
			nvs_handle_t lnvs;
			nvs_open("config", NVS_READWRITE, &lnvs);
			int i=0;
			while (fields[i]!=NULL) {
				cJSON *itm=cJSON_GetObjectItem(root, fields[i]);
				const char *v=cJSON_GetStringValue(itm);
				if (v) {
					nvs_set_str(lnvs, fields[i], v);
				}
				i++;
			}
			nvs_close(lnvs);
			//Queue restart
			const esp_timer_create_args_t timerargs={
				.callback=reset_cb,
				.name="resettimer"
			};
			esp_timer_handle_t resettimer;
			esp_timer_create(&timerargs, &resettimer);
			esp_timer_start_once(resettimer, 1*1000*1000UL);
		}
		ESP_LOGE(TAG, "Setting fields succeeded. Scheduling reboot.");
		httpd_resp_set_status(req, "200 OK");
		httpd_resp_set_type(req, "text/plain");
		httpd_resp_send(req, "OK!", 3);
	} else {
		httpd_resp_send_404(req);
	}
	return ESP_OK;
}

void webconfig_set_defaults() {
	int i=0;
	nvs_handle_t lnvs;
	esp_err_t err=nvs_open("config", NVS_READWRITE, &lnvs);
	if (err!=ESP_OK) {
		ESP_LOGE(TAG,"NVS open error: %s", esp_err_to_name(err));
		return;
	}
	while (fields[i]!=NULL) {
		char buf[256];
		size_t length=sizeof(buf);
		if (nvs_get_str(lnvs, fields[i], buf, &length)!=ESP_OK) {
			ESP_LOGE(TAG, "NVS key '%s' not found. Setting to '%s'.", fields[i], defaults[i]);
	
		nvs_set_str(lnvs, fields[i], defaults[i]);
		}
		i++;
	}
	nvs_close(lnvs);
}


void webconfig_start() {
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ret = nvs_flash_erase();
		ESP_LOGE(TAG,"NVS_flash_erase: %s", esp_err_to_name(ret));
		ret = nvs_flash_init();
		ESP_LOGE(TAG,"NVS_flash_init: %s", esp_err_to_name(ret));
	}
	nvs_open("config", NVS_READONLY, &nvs);
	webconfig_set_defaults();
	http_app_set_handler_hook(HTTP_GET, &webconfig_get_handler);
	http_app_set_handler_hook(HTTP_POST, &webconfig_post_handler);

	ESP_LOGI(TAG,"Webconfig started.");

}


int webconfig_get_config_str(const char *key, char *ret, size_t retlen) {
	if (nvs_get_str(nvs, key, ret, &retlen)!=ESP_OK) {
		return 0;
	} else {
		return retlen;
	}
}


