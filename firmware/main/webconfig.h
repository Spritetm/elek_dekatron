#include <stddef.h>
#include <esp_netif.h>

//Start the webconfig logic.
void webconfig_start();
//Get a config string, as set by the webconfig and stored in nvs.
int webconfig_get_config_str(const char *key, char *ret, size_t retlen);
//This sets the voltage and current capability field values displayed on the webpage.
void webconfig_set_usbpd(int mv, int ma);
