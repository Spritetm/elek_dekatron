/*
Dekatron driver. This can do Fancy Animations as well, by cycling the Dekatron very
fast and dwelling for variable amount of times on the various cathodes.
*/

//Initialize the dekatron driver. Initializes hardware and starts the task that handles
//all the animations. Note: does not initialize HV power supply.
void deka_init();

//Start the HV power supply.
void deka_start();

#define DEKA_ANIM_TYPE_SPIN 0 //subtype = cw (0) / ccw (1)
#define DEKA_ANIM_TYPE_CHAR 1 //subtype = ascii char
#define DEKA_ANIM_TYPE_GOOGLE 2 //Google spinner

void deka_queue_anim(int type, int subtype, int speed_us, int duration_ms);
void deka_set_rotation(int r);
int deka_get_pwm();
int deka_get_posdet_ct();

