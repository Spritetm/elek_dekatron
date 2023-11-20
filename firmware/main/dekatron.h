
void deka_init();

#define DEKA_ANIM_TYPE_SPIN 0 //subtype = cw (0) / ccw (1)
#define DEKA_ANIM_TYPE_CHAR 1 //subtype = ascii char
#define DEKA_ANIM_TYPE_GOOGLE 2 //Google spinner

void deka_queue_anim(int type, int subtype, int speed_us, int duration_ms);
