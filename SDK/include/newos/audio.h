/**
 * newos/audio.h — Namespace Audio & Multimedia (AC97 / WAV / Eventi Sonori)
 */
#ifndef NEWOS_SDK_AUDIO_H
#define NEWOS_SDK_AUDIO_H

#include "types.h"
#include <nsrt.h>

typedef struct {
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
} ns_audio_format_t;

typedef enum {
    NS_SND_CLICK  = 0,
    NS_SND_NOTIFY = 1,
    NS_SND_ERROR  = 2,
    NS_SND_CHIME  = 3
} ns_sound_event_t;

static inline void ns_audio_beep(const nsrt_api_t* rt, ns_sound_event_t ev) {
    if (rt && rt->play_sound) rt->play_sound((int)ev);
}

#endif /* NEWOS_SDK_AUDIO_H */
