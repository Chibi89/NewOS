#ifndef DRIVERS_SPEAKER_H
#define DRIVERS_SPEAKER_H

#include <types.h>

void speaker_play_freq(uint32_t freq);
void speaker_stop(void);
void speaker_tone(uint32_t freq, uint32_t duration_ms);
void speaker_play_chime(void);
void speaker_play_startup(void);

#endif // DRIVERS_SPEAKER_H
