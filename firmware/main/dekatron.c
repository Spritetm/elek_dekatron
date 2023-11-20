/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Jeroen Domburg <jeroen@spritesmods.com> wrote this file. As long as you retain 
 * this notice you can do whatever you want with this stuff. If we meet some day, 
 * and you think this stuff is worth it, you can buy me a beer in return. 
 * ----------------------------------------------------------------------------
 */

#include "dekatron.h"
#include <stdio.h>
#include <stdint.h>
#include <stdatomic.h>
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "usbpd_esp.h"
#include "driver/i2c.h"
#include "driver/gptimer.h"
#include <math.h>
#include <string.h>
#include "esp_timer.h"
#include "driver/gpio.h"
#include "io.h"

static const char *TAG="dekatron";


/*
For Fancy Animations (tm):
The datasheet states 4K PPS max rate. Given that this is probably 1 pulse per 1/10th dekatron, if we
use it as 1/30th we can use 12K PPS max. This means 83uS minimum per cathode. The datasheets specs
a 'double pulse drive duration' fo 60uS, so we're within that. (Note the way with G1/G2 is called
'double pulse'.)

Say we want to have a minimum refresh rate of 100Hz, for half the cathodes 'fully' lit. That means
we need to rotate through 15 cathodes at 100Hz*15=1.5KHz. This means 666 uS max per cathode. This is
flexible, however, given we may want less max cathodes fully lit for a better bright/'off' contrast.
*/

#define PULSE_MIN_US 83
#define PULSE_MAX_US 666

#define TIME_ONE_FRAME_US (1000000/60)

#define NO_FIXED_TARGET -1
static int curr_cathode=0;
static int fixed_target=1;
static int delay_per_cathode_us[30]={0};
static char g1_for[30];
static char g2_for[30];
static int rotation=0;
static int posdet_hit[30]={0};
static int posdet_hit_total=0;
static int posdet_prev=0;
static int curr_pwm=0;
atomic_int rot_corr=0;

typedef struct {
	char c;
	uint32_t lit;
} font_ent_t;

static const font_ent_t font[]={
	{'0', 0x3FFFFFFF},
	{'1', 0xff0},
	{'2', 0x3c7fe0ff},
	{'3', 0x3c47fc7f},
	{'4', 0x3fc08001},
	{'5', 0x3f8fff07},
	{'6', 0x3fffff07},
	{'7', 0x38000fff},
	{'8', 0x3f3ffe7f},
	{'9', 0x3f01fe7f},
	{'.', 0x2000},
	{' ', 0},
	{0, 0}, //default
};

static bool IRAM_ATTR timer_cb(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_data) {
	int delay;
	if (!gpio_get_level(IO_POSDET)) {
		if (!posdet_prev) {
			posdet_hit[curr_cathode]++;
			posdet_hit_total++;
		}
		posdet_prev=1;
	} else {
		posdet_prev=0;
	}
	curr_cathode+=atomic_exchange(&rot_corr, 0);
	if (fixed_target==NO_FIXED_TARGET) {
		//Simply walk through the electrodes, lighting them up for the specified time
		curr_cathode++;
		if (curr_cathode>=30) curr_cathode=0;
		delay=delay_per_cathode_us[curr_cathode];
	} else {
		//Count towards the fixed target
		int pulses_fwd=(fixed_target-curr_cathode);
		if (pulses_fwd<0) pulses_fwd+=30;
		if (pulses_fwd==0) {
			delay=PULSE_MAX_US;
		} else if (pulses_fwd<15) {
			curr_cathode++;
			if (curr_cathode>=30) curr_cathode=0;
			delay=PULSE_MIN_US;
		} else {
			curr_cathode--;
			if (curr_cathode<0) curr_cathode=29;
			delay=PULSE_MIN_US;
		}
	}
	gpio_set_level(IO_G2, g2_for[curr_cathode]);
	gpio_set_level(IO_G1, g1_for[curr_cathode]);

	// reconfigure alarm value
	gptimer_alarm_config_t alarm_config = {
		.alarm_count = edata->alarm_value + delay
	};
	gptimer_set_alarm_action(timer, &alarm_config);
	return true;
}

static void deka_set_pos(int pos) {
	pos=pos%30;
	if (pos<0) pos+=30;
	fixed_target=pos;
}

static void deka_set_intens(uint8_t *intens) {
	//This tries to set the timings so one 'frame' (decatron making a full circle) takes
	//up TIME_ONE_FRAME_US time. It does that by trying to maximise the time the 
	//non-zero-intensity cathodes are lit.
	int total_intens=0;
	for (int i=0; i<30; i++) {
		total_intens+=intens[i];
	}

	int time_left=TIME_ONE_FRAME_US-(30*PULSE_MIN_US);
	for (int i=0; i<30; i++) {
		delay_per_cathode_us[i]=PULSE_MIN_US+((intens[i]*time_left)/total_intens);
	}
	fixed_target=NO_FIXED_TARGET;
}



typedef struct {
	int type;
	int subtype;
	int speed; //actually delay in us
	int duration_ms;
} deka_cmd_t;

QueueHandle_t deka_cmd_queue;


static void ledc_init(void) {
	ledc_timer_config_t ledc_timer = {
		.speed_mode		  = LEDC_LOW_SPEED_MODE,
		.timer_num		  = LEDC_TIMER_0,
		.duty_resolution  = LEDC_TIMER_9_BIT,
		.freq_hz		  = 40000,
		.clk_cfg		  = LEDC_AUTO_CLK
	};
	ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

	ledc_channel_config_t ledc_channel = {
		.speed_mode		= LEDC_LOW_SPEED_MODE,
		.channel		= LEDC_CHANNEL_0,
		.timer_sel		= LEDC_TIMER_0,
		.intr_type		= LEDC_INTR_DISABLE,
		.gpio_num		= IO_BOOST,
		.duty			= 256
	};
	ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}


static adc_oneshot_unit_handle_t adc1_handle;
static adc_cali_handle_t adc_cali_handle;

static void deka_power_task(void *arg) {
	int duty=256;	//current duty cycle
	//Note that duty is inverted, as in, duty being high generally results in a low boost
	//output voltage and vice versa.
	int tgt=480;	//equivalent of 400V
	int warned=0;
	int posdet_fix_tmr=0;
	while(1) {
		int adc_raw, voltage;
		ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, IO_ADC_HV, &adc_raw));
		ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc_cali_handle, adc_raw, &voltage));

		//Very slow and simple regulation loop. It's fine, there is a fair amount of tolerance
		//on the dekatron voltages, and the fact that this starts a bit slow generally is
		//acceptable.
		if (voltage<tgt) duty--; else duty++;
		
		//upper and lower bound for duty, so we don't blow up the boost transistor
		//if e.g. the adc reads the wrong value somehow
		if (duty<15) {
			duty=15;
			if (!warned) {
				ESP_LOGW(TAG, "PWM duty cycle bumped against max limit!");
				warned=1;
			}
		}
		if (duty>511) duty=511;
		//Set the duty cycle and wait.
		ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty));
		ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
		curr_pwm=duty;
		vTaskDelay(pdMS_TO_TICKS(50));


		posdet_fix_tmr++;
		if (posdet_fix_tmr>30) {
			posdet_fix_tmr=0;
			int max_hits=0;
			int max_hit_pos=0;
			for (int i=0; i<30; i++) {
				int j=posdet_hit[i];
				posdet_hit[i]=0;
				if (j>max_hits) {
					max_hits=j;
					max_hit_pos=0;
				}
			}
			if (max_hits>5) {
				int c=rotation-max_hit_pos;
				if (c<0) c+=30;
				atomic_fetch_add(&rot_corr, c);
			}
		}
	}
}


static TaskHandle_t deka_anim_task_handle;

void IRAM_ATTR esp_timer_cb(void *arg) {
	int hi_prio_awoken=0;
	vTaskNotifyGiveIndexedFromISR(deka_anim_task_handle, 0, &hi_prio_awoken)
	if (hi_prio_awoken) esp_timer_isr_dispatch_need_yield();
}

void deka_anim_task() {
	deka_cmd_t cur_anim={
		.type=DEKA_ANIM_TYPE_GOOGLE,
		.subtype=0,
		.speed=10*1000,
		.duration_ms=0
	};

	esp_timer_handle_t timerhandle;
	esp_timer_create_args_t timercfg={
		.callback=esp_timer_cb,
		.name="deka_anim_tmr",
		.skip_unhandled_events=1,
		.dispatch_method=ESP_TIMER_ISR
	};
	esp_timer_create(&timercfg, &timerhandle);

	int64_t anim_start=esp_timer_get_time();
	esp_timer_start_periodic(timerhandle, cur_anim.speed);
	unsigned int frame=0;
	uint8_t fb[30];
	while(1) {
		uint32_t timerexpired=0;
		do {
			//wait for timer to expire
			timerexpired=ulTaskNotifyTakeIndexed(0, pdTRUE, pdMS_TO_TICKS(200));
			//see if we need to / can switch to a new animation
			int64_t time_ran_ms=(esp_timer_get_time()-anim_start)/1000;
			if (time_ran_ms>=cur_anim.duration_ms) {
				if (xQueueReceive(deka_cmd_queue, &cur_anim, 0)) {
					esp_timer_restart(timerhandle, cur_anim.speed);
					anim_start=esp_timer_get_time();
				}
			}
		} while (!timerexpired);
		//render a frame of the animation
		if (cur_anim.type==DEKA_ANIM_TYPE_SPIN) {
			if (cur_anim.subtype) fixed_target--; else fixed_target++;
			if (fixed_target<0) fixed_target+=30;
			if (fixed_target>29) fixed_target-=30;
		} else if (cur_anim.type==DEKA_ANIM_TYPE_CHAR) {
			int c=0;
			while (font[c].c!=0 && font[c].c!=cur_anim.subtype) c++;
			for (int i=0; i<30; i++) {
				if (font[c].lit&(1<<i)) fb[i]=255; else fb[i]=0;
			}
			deka_set_intens(fb);
		} else if (cur_anim.type==DEKA_ANIM_TYPE_GOOGLE) {
			int size=(sinf((float)frame/32)*14)+15;
			int startpos=((frame/4)%30)-size/2;
			memset(fb, 0, sizeof(fb));
			for (int i=0; i<size; i++) {
				int p=startpos+i;
				if (p>=30) p-=30;
				if (p<0) p+=30;
				fb[p]=255;
			}
			deka_set_intens(fb);
		}
		frame++;
	}
}

void deka_queue_anim(int type, int subtype, int speed_us, int duration_ms) {
	deka_cmd_t cmd={0};
	cmd.type=type;
	cmd.subtype=subtype;
	cmd.speed=speed_us;
	cmd.duration_ms=duration_ms;
	xQueueSend(deka_cmd_queue, &cmd, portMAX_DELAY);
}

void deka_set_rotation(int r) {
	rotation=r;
}

int deka_get_pwm() {
	return curr_pwm;
}

int deka_get_posdet_ct() {
	return posdet_hit_total;
}

void deka_init() {
	deka_cmd_queue=xQueueCreate(16, sizeof(deka_cmd_t));
	ledc_init();

	gpio_config_t cfg={
		.pin_bit_mask=(1<<IO_G1)|(1<<IO_G2),
		.mode=GPIO_MODE_OUTPUT
	};
	gpio_config(&cfg);
	gpio_config_t cfg_in={
		.pin_bit_mask=(1<<IO_POSDET),
		.mode=GPIO_MODE_INPUT
	};
	gpio_config(&cfg_in);

	adc_oneshot_unit_init_cfg_t init_config1 = {
		.unit_id = ADC_UNIT_1,
	};
	ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

	adc_oneshot_chan_cfg_t config = {
		.bitwidth = ADC_BITWIDTH_DEFAULT,
		.atten = ADC_ATTEN_DB_0,
	};
	ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, IO_ADC_HV, &config));

	adc_cali_curve_fitting_config_t cali_config = {
		.unit_id = ADC_UNIT_1,
//		.chan = IO_ADC_HV,
		.atten = ADC_ATTEN_DB_0,
		.bitwidth = ADC_BITWIDTH_DEFAULT,
	};
	ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_config, &adc_cali_handle));
	
	gptimer_handle_t gptimer = NULL;
	gptimer_config_t timer_config = {
		.clk_src = GPTIMER_CLK_SRC_DEFAULT,
		.direction = GPTIMER_COUNT_UP,
		.resolution_hz = 1000000, // 1MHz, 1 tick=1us
	};
	ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));

	//precalculate g1/g2 values
	for (int x=0; x<30; x++) {
		int t=x%3;
		g1_for[x]=(t==2)?1:0;
		g2_for[x]=(t==1)?1:0;
	}

	gptimer_event_callbacks_t cbs = {
		.on_alarm = timer_cb,
	};
	ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, NULL));
	gptimer_alarm_config_t alarm_config1 = {
		.alarm_count = 1000
	};
	ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config1));
	ESP_ERROR_CHECK(gptimer_enable(gptimer));
	ESP_ERROR_CHECK(gptimer_start(gptimer));
	
	xTaskCreate(deka_anim_task, "deka_anim", 4096, NULL, 5, &deka_anim_task_handle);
}


void deka_start() {
	xTaskCreate(deka_power_task, "deka_pwr", 4096, NULL, 5, NULL);
}