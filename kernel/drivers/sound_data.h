#ifndef DRIVERS_SOUND_DATA_H
#define DRIVERS_SOUND_DATA_H

#include <types.h>

#define SOUND_STARTUP_SIZE 2048
extern const uint8_t sound_startup_pcm[SOUND_STARTUP_SIZE];

#define SOUND_DING_SIZE 2048
extern const uint8_t sound_ding_pcm[SOUND_DING_SIZE];

#endif // DRIVERS_SOUND_DATA_H
