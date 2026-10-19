#ifndef DRIVERS_SOUND_WAV_H
#define DRIVERS_SOUND_WAV_H

#include <types.h>

typedef struct {
    uint16_t audio_format;    // 1 = PCM
    uint16_t num_channels;    // 1 = mono, 2 = stereo
    uint32_t sample_rate;     // es. 44100, 48000
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample; // 8 o 16
    const uint8_t* pcm_data;
    uint32_t pcm_size;
} wav_info_t;

// Analizza un buffer in memoria contenente un file .WAV
bool wav_parse(const uint8_t* data, uint32_t size, wav_info_t* out_info);

// Carica e riproduce un file .WAV dal file system (es. "C:\SYSTEM\SOUNDS\STARTUP.WAV")
bool sound_play_wav_file(const char* vfs_path);

#endif // DRIVERS_SOUND_WAV_H
