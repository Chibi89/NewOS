/**
 * MultimediaCodec/wavdec.c
 * Codec Audio: RIFF WAVE PCM nativo 8/16/24-bit
 * Modulo indipendente .CDC per NewOS Media Player
 * (C) Newspoint Software - NewSoft Media Subsystem
 */

#include "media_codec.h"

// Header di protezione del modulo (64 byte esatti all'inizio del file .CDC)
__attribute__((section(".header"), used))
module_header_t g_wavdec_header = {
    .magic          = MODULE_MAGIC_NEWOS,
    .header_size    = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type    = MODULE_TYPE_CODEC,
    .code_size      = 0, // Popolato dal build system
    .entry_offset   = 64, // Posizione dell'entry point
    .checksum       = 0, // Popolato dal build system
    .security_flags = SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name    = "WAVDEC",
    .signature      = MODULE_AUTH_TAG
};

typedef struct {
    const kernel_api_t* api;
    uint8_t*            file_data;
    uint32_t            file_size;
    const uint8_t*      pcm_data;
    uint32_t            pcm_size;
    uint32_t            pcm_offset;
    uint16_t            channels;
    uint32_t            sample_rate;
    uint16_t            bits_per_sample;
    uint16_t            block_align;
    uint32_t            byte_rate;
} wav_stream_t;

static const kernel_api_t* g_api = NULL;

static void* local_memcpy(void* dest, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    while (n--) *d++ = *s++;
    return dest;
}

static void* local_memset(void* s, int c, size_t n) {
    uint8_t* p = (uint8_t*)s;
    while (n--) *p++ = (uint8_t)c;
    return s;
}

static int local_memcmp(const void* s1, const void* s2, size_t n) {
    const uint8_t* p1 = (const uint8_t*)s1;
    const uint8_t* p2 = (const uint8_t*)s2;
    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) return p1[i] - p2[i];
    }
    return 0;
}

static void local_strncpy(char* dest, const char* src, size_t n) {
    size_t i = 0;
    while (i < n - 1 && src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

static bool wav_init(const kernel_api_t* api) {
    g_api = api;
    if (g_api && g_api->serial_puts) {
        g_api->serial_puts("[WAVDEC.CDC] Inizializzazione Codec RIFF Wave PCM completata.\n");
    }
    return true;
}

static bool wav_probe(const uint8_t* header, uint32_t size) {
    if (!header || size < 44) return false;
    if (local_memcmp(header, "RIFF", 4) != 0) return false;
    if (local_memcmp(header + 8, "WAVE", 4) != 0) return false;
    return true;
}

static void* wav_open_stream(const char* path, media_info_t* out_info) {
    if (!g_api || !g_api->vfs_read_entire_file || !g_api->kmalloc) return NULL;

    uint32_t file_size = 0;
    uint8_t* file_data = g_api->vfs_read_entire_file(path, &file_size);
    if (!file_data || file_size < 44) {
        if (file_data && g_api->kfree) g_api->kfree(file_data);
        return NULL;
    }

    if (!wav_probe(file_data, file_size)) {
        if (g_api->kfree) g_api->kfree(file_data);
        return NULL;
    }

    // Parsing chunk fmt
    uint32_t offset = 12;
    bool found_fmt = false;
    uint16_t audio_format = 0;
    uint16_t num_channels = 0;
    uint32_t sample_rate = 0;
    uint32_t byte_rate = 0;
    uint16_t block_align = 0;
    uint16_t bits_per_sample = 0;

    while (offset + 8 <= file_size) {
        const char* chunk_id = (const char*)(file_data + offset);
        uint32_t chunk_size = *(const uint32_t*)(file_data + offset + 4);

        if (local_memcmp(chunk_id, "fmt ", 4) == 0 && chunk_size >= 16) {
            audio_format    = *(const uint16_t*)(file_data + offset + 8);
            num_channels    = *(const uint16_t*)(file_data + offset + 10);
            sample_rate     = *(const uint32_t*)(file_data + offset + 12);
            byte_rate       = *(const uint32_t*)(file_data + offset + 16);
            block_align     = *(const uint16_t*)(file_data + offset + 20);
            bits_per_sample = *(const uint16_t*)(file_data + offset + 22);
            found_fmt = true;
            break;
        }
        offset += 8 + chunk_size;
    }

    if (!found_fmt || audio_format != 1) { // 1 = PCM lineare non compresso
        if (g_api->kfree) g_api->kfree(file_data);
        return NULL;
    }

    // Ricerca chunk data
    offset = 12;
    const uint8_t* pcm_data = NULL;
    uint32_t pcm_size = 0;

    while (offset + 8 <= file_size) {
        const char* chunk_id = (const char*)(file_data + offset);
        uint32_t chunk_size = *(const uint32_t*)(file_data + offset + 4);

        if (local_memcmp(chunk_id, "data", 4) == 0) {
            pcm_data = file_data + offset + 8;
            pcm_size = chunk_size;
            if (offset + 8 + chunk_size > file_size) {
                pcm_size = file_size - (offset + 8);
            }
            break;
        }
        offset += 8 + chunk_size;
    }

    if (!pcm_data || pcm_size == 0) {
        if (g_api->kfree) g_api->kfree(file_data);
        return NULL;
    }

    wav_stream_t* st = (wav_stream_t*)g_api->kmalloc(sizeof(wav_stream_t));
    if (!st) {
        if (g_api->kfree) g_api->kfree(file_data);
        return NULL;
    }

    local_memset(st, 0, sizeof(wav_stream_t));
    st->api             = g_api;
    st->file_data       = file_data;
    st->file_size       = file_size;
    st->pcm_data        = pcm_data;
    st->pcm_size        = pcm_size;
    st->pcm_offset      = 0;
    st->channels        = num_channels;
    st->sample_rate     = sample_rate;
    st->bits_per_sample = bits_per_sample;
    st->block_align     = block_align;
    st->byte_rate       = byte_rate;

    if (out_info) {
        local_memset(out_info, 0, sizeof(media_info_t));
        local_strncpy(out_info->title, "PCM Wave Audio", 64);
        local_strncpy(out_info->artist, "NewOS Audio Engine", 64);
        local_strncpy(out_info->album, "Standard Wave", 64);

        out_info->has_audio       = true;
        out_info->sample_rate     = sample_rate;
        out_info->channels        = num_channels;
        out_info->bits_per_sample = bits_per_sample;
        out_info->audio_bitrate   = byte_rate * 8;
        if (byte_rate > 0) {
            out_info->duration_ms = (uint32_t)(((uint64_t)pcm_size * 1000) / byte_rate);
        } else {
            out_info->duration_ms = 0;
        }
        out_info->has_video       = false;
    }

    return (void*)st;
}

// Allineamento del flusso audio: converte qualsiasi formato PCM (8/16/24 bit mono/stereo)
// nel formato DMA hardware 16-bit interleaved stereo per il controller AC97
static bool wav_read_audio_pcm(void* handle, uint8_t* out_buf, uint32_t bytes_to_read, uint32_t* out_bytes_read) {
    if (!handle || !out_buf || bytes_to_read == 0) return false;
    wav_stream_t* st = (wav_stream_t*)handle;

    if (st->pcm_offset >= st->pcm_size) {
        if (out_bytes_read) *out_bytes_read = 0;
        return false; // Fine flusso
    }

    uint32_t bytes_written = 0;
    int16_t* out_samples = (int16_t*)out_buf;
    uint32_t max_out_samples = bytes_to_read / 4; // Coppie stereo 16-bit (4 byte = 1 campione stereo)

    while (bytes_written / 4 < max_out_samples && st->pcm_offset < st->pcm_size) {
        int16_t left = 0;
        int16_t right = 0;

        if (st->bits_per_sample == 16) {
            if (st->channels == 2) {
                if (st->pcm_offset + 4 > st->pcm_size) break;
                const int16_t* in = (const int16_t*)(st->pcm_data + st->pcm_offset);
                left  = in[0];
                right = in[1];
                st->pcm_offset += 4;
            } else {
                if (st->pcm_offset + 2 > st->pcm_size) break;
                const int16_t* in = (const int16_t*)(st->pcm_data + st->pcm_offset);
                left = right = in[0];
                st->pcm_offset += 2;
            }
        } else if (st->bits_per_sample == 8) {
            if (st->channels == 2) {
                if (st->pcm_offset + 2 > st->pcm_size) break;
                left  = (int16_t)(((int)st->pcm_data[st->pcm_offset] - 128) << 8);
                right = (int16_t)(((int)st->pcm_data[st->pcm_offset + 1] - 128) << 8);
                st->pcm_offset += 2;
            } else {
                if (st->pcm_offset + 1 > st->pcm_size) break;
                left = right = (int16_t)(((int)st->pcm_data[st->pcm_offset] - 128) << 8);
                st->pcm_offset += 1;
            }
        } else if (st->bits_per_sample == 24) {
            if (st->channels == 2) {
                if (st->pcm_offset + 6 > st->pcm_size) break;
                int32_t l24 = ((int32_t)st->pcm_data[st->pcm_offset + 0]) |
                              ((int32_t)st->pcm_data[st->pcm_offset + 1] << 8) |
                              ((int32_t)(int8_t)st->pcm_data[st->pcm_offset + 2] << 16);
                int32_t r24 = ((int32_t)st->pcm_data[st->pcm_offset + 3]) |
                              ((int32_t)st->pcm_data[st->pcm_offset + 4] << 8) |
                              ((int32_t)(int8_t)st->pcm_data[st->pcm_offset + 5] << 16);
                left  = (int16_t)(l24 >> 8);
                right = (int16_t)(r24 >> 8);
                st->pcm_offset += 6;
            } else {
                if (st->pcm_offset + 3 > st->pcm_size) break;
                int32_t s24 = ((int32_t)st->pcm_data[st->pcm_offset + 0]) |
                              ((int32_t)st->pcm_data[st->pcm_offset + 1] << 8) |
                              ((int32_t)(int8_t)st->pcm_data[st->pcm_offset + 2] << 16);
                left = right = (int16_t)(s24 >> 8);
                st->pcm_offset += 3;
            }
        } else {
            break;
        }

        out_samples[(bytes_written / 2) + 0] = left;
        out_samples[(bytes_written / 2) + 1] = right;
        bytes_written += 4;
    }

    if (out_bytes_read) *out_bytes_read = bytes_written;
    return (bytes_written > 0);
}

static bool wav_read_video_frame(void* handle, uint32_t* out_frame_32bpp, uint32_t* out_timestamp_ms) {
    (void)handle; (void)out_frame_32bpp; (void)out_timestamp_ms;
    return false; // Codec puramente audio
}

static bool wav_seek_ms(void* handle, uint32_t timestamp_ms) {
    if (!handle) return false;
    wav_stream_t* st = (wav_stream_t*)handle;
    if (st->byte_rate == 0) return false;

    uint64_t target_byte = ((uint64_t)timestamp_ms * st->byte_rate) / 1000;
    if (st->block_align > 0) {
        target_byte = (target_byte / st->block_align) * st->block_align;
    }
    if (target_byte > st->pcm_size) target_byte = st->pcm_size;
    st->pcm_offset = (uint32_t)target_byte;
    return true;
}

static void wav_close_stream(void* handle) {
    if (!handle) return;
    wav_stream_t* st = (wav_stream_t*)handle;
    if (st->file_data && st->api && st->api->kfree) {
        st->api->kfree(st->file_data);
    }
    if (st->api && st->api->kfree) {
        st->api->kfree(st);
    }
}

// Struttura interfaccia del Codec WAV
static media_codec_interface_t g_wav_interface = {
    .magic            = CODEC_MAGIC_NCDC,
    .name             = "NewSoft RIFF Wave PCM Audio Engine",
    .extensions       = ".WAV",
    .version          = CODEC_VERSION_1_0,
    .codec_type       = CODEC_TYPE_AUDIO,
    .init             = wav_init,
    .probe            = wav_probe,
    .open_stream      = wav_open_stream,
    .read_audio_pcm   = wav_read_audio_pcm,
    .read_video_frame = wav_read_video_frame,
    .seek_ms          = wav_seek_ms,
    .close_stream     = wav_close_stream
};

__attribute__((section(".text.entry"), used))
media_codec_interface_t* wavdec_entry(const kernel_api_t* api) {
    wav_init(api);
    return &g_wav_interface;
}
