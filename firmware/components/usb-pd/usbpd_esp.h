#include "driver/i2c.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct usbpd_esp_t usbpd_esp_t;

esp_err_t usbpd_esp_init(i2c_port_t i2c_port);


#ifdef __cplusplus
}
#endif