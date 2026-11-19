#include <drivers/sound.h>
#include <drivers/ac97.h>
#include <drivers/hda.h>
#include <drivers/speaker.h>
#include <drivers/sound_data.h>

void sound_init(void) {
    if (!hda_init() && !ac97_init()) {
        // Driver Intel HDA / AC'97 non rilevati: verrà utilizzato PC Speaker
    }
}

bool sound_is_ac97(void) {
    return hda_is_available() || ac97_is_available();
}

const char* sound_get_driver_name(void) {
    if (hda_is_available()) return hda_get_device_name();
    return ac97_get_device_name();
}

void sound_set_volume(uint8_t vol_pct) {
    if (hda_is_available()) hda_set_volume(vol_pct);
    ac97_set_volume(vol_pct);
}

uint8_t sound_get_volume(void) {
    return ac97_get_volume();
}

void sound_play(sound_event_t event) {
    if (hda_is_available()) {
        switch (event) {
            case SOUND_STARTUP:
                hda_play_pcm(sound_startup_pcm, SOUND_STARTUP_SIZE, 44100);
                break;
            case SOUND_DING:
            case SOUND_CHIME:
            default:
                hda_play_pcm(sound_ding_pcm, SOUND_DING_SIZE, 44100);
                break;
        }
    } else if (ac97_is_available()) {
        switch (event) {
            case SOUND_STARTUP:
                ac97_play_pcm(sound_startup_pcm, SOUND_STARTUP_SIZE, 44100);
                break;
            case SOUND_DING:
                ac97_play_pcm(sound_ding_pcm, SOUND_DING_SIZE, 44100);
                break;
            case SOUND_CHIME:
            default:
                ac97_play_pcm(sound_ding_pcm, SOUND_DING_SIZE, 44100);
                break;
        }
    } else {
        // Fallback su PC Speaker
        switch (event) {
            case SOUND_STARTUP:
                speaker_play_startup();
                break;
            case SOUND_DING:
                speaker_tone(1046, 120);
                break;
            case SOUND_CHIME:
            default:
                speaker_play_chime();
                break;
        }
    }
}

bool sound_is_playing(void) {
    if (ac97_is_available()) {
        return ac97_is_playing();
    }
    return false;
}

void sound_stop(void) {
    if (ac97_is_available()) {
        ac97_stop();
    } else {
        speaker_stop();
    }
}
