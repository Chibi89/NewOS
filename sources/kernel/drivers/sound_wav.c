#include <drivers/sound_wav.h>
#include <drivers/ac97.h>
#include <drivers/speaker.h>
#include <fs/vfs.h>
#include <mm/heap.h>
#include <string.h>

static uint8_t* g_active_wav_buffer = NULL;

bool wav_parse(const uint8_t* data, uint32_t size, wav_info_t* out_info) {
    if (!data || size < 44 || !out_info) return false;

    // Controllo intestazione "RIFF"
    if (memcmp(data, "RIFF", 4) != 0) return false;
    // Controllo formato "WAVE"
    if (memcmp(data + 8, "WAVE", 4) != 0) return false;

    // Ricerca del chunk "fmt "
    uint32_t offset = 12;
    bool found_fmt = false;

    while (offset + 8 <= size) {
        const char* chunk_id = (const char*)(data + offset);
        uint32_t chunk_size = *(const uint32_t*)(data + offset + 4);

        if (memcmp(chunk_id, "fmt ", 4) == 0 && chunk_size >= 16) {
            out_info->audio_format    = *(const uint16_t*)(data + offset + 8);
            out_info->num_channels    = *(const uint16_t*)(data + offset + 10);
            out_info->sample_rate     = *(const uint32_t*)(data + offset + 12);
            out_info->byte_rate       = *(const uint32_t*)(data + offset + 16);
            out_info->block_align     = *(const uint16_t*)(data + offset + 20);
            out_info->bits_per_sample = *(const uint16_t*)(data + offset + 22);
            found_fmt = true;
            break;
        }

        offset += 8 + chunk_size;
    }

    if (!found_fmt || out_info->audio_format != 1) { // 1 = PCM non compresso
        return false;
    }

    // Ricerca del chunk "data"
    offset = 12;
    while (offset + 8 <= size) {
        const char* chunk_id = (const char*)(data + offset);
        uint32_t chunk_size = *(const uint32_t*)(data + offset + 4);

        if (memcmp(chunk_id, "data", 4) == 0) {
            out_info->pcm_data = data + offset + 8;
            out_info->pcm_size = chunk_size;
            if (offset + 8 + chunk_size > size) {
                out_info->pcm_size = size - (offset + 8);
            }
            return true;
        }

        offset += 8 + chunk_size;
    }

    return false;
}

bool sound_play_wav_file(const char* vfs_path) {
    if (!vfs_path) return false;

    uint32_t file_size = 0;
    uint8_t* wav_data = (uint8_t*)vfs_read_entire_file(vfs_path, &file_size);
    if (!wav_data || file_size < 44) {
        return false;
    }

    wav_info_t info;
    if (!wav_parse(wav_data, file_size, &info)) {
        kfree(wav_data);
        return false;
    }

    // Mantiene in vita il buffer finché non viene caricato un nuovo suono
    if (g_active_wav_buffer) {
        kfree(g_active_wav_buffer);
    }
    g_active_wav_buffer = wav_data;

    if (ac97_is_available()) {
        ac97_play_pcm(info.pcm_data, info.pcm_size, info.sample_rate);
        return true;
    } else {
        speaker_play_startup();
        return true;
    }
}
