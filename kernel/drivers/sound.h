#ifndef DRIVERS_SOUND_H
#define DRIVERS_SOUND_H

#include <types.h>

typedef enum {
    SOUND_NONE = 0,
    SOUND_STARTUP,
    SOUND_DING,
    SOUND_CHIME
} sound_event_t;

void sound_init(void);
void sound_play(sound_event_t event);
bool sound_is_playing(void);
void sound_stop(void);
const char* sound_get_driver_name(void);
bool sound_is_ac97(void);

void sound_set_volume(uint8_t vol_pct);
uint8_t sound_get_volume(void);

#endif // DRIVERS_SOUND_H
