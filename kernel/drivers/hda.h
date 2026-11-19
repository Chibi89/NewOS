#ifndef DRIVERS_HDA_H
#define DRIVERS_HDA_H

#include <types.h>

bool        hda_init(void);
bool        hda_is_available(void);
const char* hda_get_device_name(void);
void        hda_play_pcm(const void* data, uint32_t bytes, uint32_t sample_rate);
bool        hda_is_playing(void);
void        hda_stop(void);
void        hda_set_volume(uint8_t volume_percent);

#endif // DRIVERS_HDA_H
