#include <stddef.h>
#include <esp_netif.h>

void webconfig_start();
//Get a config string, as set by the webconfig and stored in nvs
int webconfig_get_config_str(const char *key, char *ret, size_t retlen);
//This sets the voltage and current capability field on the webpage.
void webconfig_set_usbpd(int mv, int ma);
