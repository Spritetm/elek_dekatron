#include <stddef.h>
#include <esp_netif.h>

void webconfig_start();
//Get a config string
int webconfig_get_config_str(const char *key, char *ret, size_t retlen);
