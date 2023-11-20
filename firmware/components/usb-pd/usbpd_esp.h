#include "driver/i2c.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct usbpd_esp_t usbpd_esp_t;

#define USBPD_ESP_CB_INITIAL 0
#define USBPD_ESP_CB_MORE 1
#define USBPD_ESP_CB_CHOSEN 2
/*
 Callback for when an USB-PD-compliant power supply is attached. The callback will be
 called a number of times, the first call with type=USBPD_ESP_CB_INITIAL and dummy
 parameters, subsequent times with type=USBPD_ESP_CB_MORE and actual values related'
 to the power supply. The callback function needs to return a indicating
 if the given parameters are acceptable to the device (or more acceptable than the 
 previously marked acceptable parameters). After all the power supply options
 have been enumerated, the power supply is set to the last configuration that the
 callback returned as acceptable, and the callback is called one last time with 
 type=USBPD_ESP_CB_CHOSEN to indicate the chosen configuration.
*/
typedef int (*usbpd_esp_cb_t)(int type, int mv, int ma);


/*
 Starts the usbpd subsystem. Takes an (initialized) i2c port the fusb302 is connected
 to, as well as a callback as described above.
*/
esp_err_t usbpd_esp_init(i2c_port_t i2c_port, usbpd_esp_cb_t cb);

#ifdef __cplusplus
}
#endif