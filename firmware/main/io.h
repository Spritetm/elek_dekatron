#define IO_BOOST 20
#define IO_G1 7
#define IO_G2 8
#define IO_POSDET 6
#define IO_ADC_HV ADC_CHANNEL_2
#define IO_BTN 9
#define IO_LEDR 0
#define IO_LEDG 1


#define LED_RED 0
#define LED_GREEN 1

#define BLINK_OFF 0
#define BLINK_SLOW 1
#define BLINK_FAST 2
#define BLINK_ON 3

void io_led_blink_set(int led, int blink);
void io_init();
int io_btn_pressed();
