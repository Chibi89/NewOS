#ifndef DRIVERS_AC97_H
#define DRIVERS_AC97_H

#include <types.h>

bool ac97_init(void);
bool ac97_is_available(void);
const char* ac97_get_device_name(void);

// Riproduce buffer PCM (16-bit, stereo, sample_rate es. 22050 o 44100 Hz)
void ac97_play_pcm(const void* data, uint32_t bytes, uint32_t sample_rate);
bool ac97_is_playing(void);
void ac97_stop(void);

// Volume master (0 = 100% max, 31 = min/muto)
void ac97_set_volume(uint8_t volume_percent);
uint8_t ac97_get_volume(void);

#endif // DRIVERS_AC97_H
