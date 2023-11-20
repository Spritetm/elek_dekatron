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

//Queue an animation of the given type and subtype. speed_us depends on the
//type of animation; it's only used for TYPE_SPIN at this moment where it
//indicates the amount of uS the glow will rest on a cathode before moving to the
//next. duration_ms is the minimum time the animation will play, but if nothing
//is queued up next, it may play longer than that.
void deka_queue_anim(int type, int subtype, int speed_us, int duration_ms);

//Adjust the 'down' position of the dekatron so TYPE_CHAR shows up OK.
void deka_set_rotation(int r);

//Get the HV regulator PWM value. 0-511.
int deka_get_pwm();

//Get an indicator for how well the position detector works... finnicky, that one.
int deka_get_posdet_ct();

