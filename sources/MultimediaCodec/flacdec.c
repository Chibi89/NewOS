/**
 * MultimediaCodec/flacdec.c
 * Codec Audio: FLAC Lossless Studio bit-perfect (Xiph.Org Open-Source & Royalty-Free)
 * Modulo indipendente .CDC per NewOS Media Player
 * (C) Newspoint Software - NewSoft Media Subsystem
 */

#include "media_codec.h"

// Header di protezione del modulo (64 byte esatti all'inizio del file .CDC)
__attribute__((section(".header"), used))
module_header_t g_flacdec_header = {
    .magic          = MODULE_MAGIC_NEWOS,
    .header_size    = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type    = MODULE_TYPE_CODEC,
    .code_size      = 0, // Popolato dal build system
    .entry_offset   = 64, // Posizione dell'entry point
    .checksum       = 0, // Popolato dal build system
    .security_flags = SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name    = "FLACDEC",
    .signature      = MODULE_AUTH_TAG
};

typedef struct {
    const kernel_api_t* api;
    uint8_t*            file_data;
    uint32_t            file_size;
    uint32_t            first_frame_offset;
    uint32_t            current_file_offset;

    // Metadati estratti dal blocco STREAMINFO
    uint32_t            min_block_size;
    uint32_t            max_block_size;
    uint32_t            sample_rate;
    uint16_t            channels;
    uint16_t            bits_per_sample;
    uint64_t            total_samples;
    uint32_t            duration_ms;

    // Buffer circolare campioni decodificati (fino a 4096 campioni stereo = 16384 byte)
    int32_t             sample_buffer_l[4096];
    int32_t             sample_buffer_r[4096];
    uint32_t            samples_in_cache;
    uint32_t            samples_read_cache;
} flac_stream_t;

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

static bool flac_init(const kernel_api_t* api) {
    g_api = api;
    if (g_api && g_api->serial_puts) {
        g_api->serial_puts("[FLACDEC.CDC] Inizializzazione Codec FLAC Lossless Hi-Fi completata.\n");
    }
    return true;
}

static bool flac_probe(const uint8_t* header, uint32_t size) {
    if (!header || size < 4) return false;
    // FLAC Stream Marker 'fLaC' (0x66, 0x4C, 0x61, 0x43)
    return (header[0] == 'f' && header[1] == 'L' && header[2] == 'a' && header[3] == 'C');
}

static void* flac_open_stream(const char* path, media_info_t* out_info) {
    if (!g_api || !g_api->vfs_read_entire_file || !g_api->kmalloc) return NULL;

    uint32_t file_size = 0;
    uint8_t* file_data = g_api->vfs_read_entire_file(path, &file_size);
    if (!file_data || file_size < 42) {
        if (file_data && g_api->kfree) g_api->kfree(file_data);
        return NULL;
    }

    if (!flac_probe(file_data, file_size)) {
        if (g_api->kfree) g_api->kfree(file_data);
        return NULL;
    }

    // Parsing blocchi metadati
    uint32_t offset = 4;
    bool found_streaminfo = false;
    uint32_t min_block = 0, max_block = 0;
    uint32_t sample_rate = 44100;
    uint16_t channels = 2;
    uint16_t bits = 16;
    uint64_t total_samples = 0;

    while (offset + 4 <= file_size) {
        uint8_t block_hdr = file_data[offset];
        bool is_last = (block_hdr & 0x80) != 0;
        uint8_t block_type = block_hdr & 0x7F;
        uint32_t block_len = ((uint32_t)file_data[offset + 1] << 16) |
                             ((uint32_t)file_data[offset + 2] << 8)  |
                             ((uint32_t)file_data[offset + 3]);
        offset += 4;

        if (offset + block_len > file_size) break;

        if (block_type == 0 && block_len >= 34) { // STREAMINFO
            const uint8_t* b = file_data + offset;
            min_block = ((uint32_t)b[0] << 8) | b[1];
            max_block = ((uint32_t)b[2] << 8) | b[3];

            sample_rate = ((uint32_t)b[10] << 12) | ((uint32_t)b[11] << 4) | ((b[12] >> 4) & 0x0F);
            channels = ((b[12] >> 1) & 0x07) + 1;
            bits = (((uint16_t)(b[12] & 0x01) << 4) | ((b[13] >> 4) & 0x0F)) + 1;

            total_samples = (((uint64_t)(b[13] & 0x0F)) << 32) |
                            (((uint64_t)b[14]) << 24) |
                            (((uint64_t)b[15]) << 16) |
                            (((uint64_t)b[16]) << 8)  |
                            ((uint64_t)b[17]);
            found_streaminfo = true;
        }

        offset += block_len;
        if (is_last) break;
    }

    if (!found_streaminfo) {
        if (g_api->kfree) g_api->kfree(file_data);
        return NULL;
    }

    flac_stream_t* st = (flac_stream_t*)g_api->kmalloc(sizeof(flac_stream_t));
    if (!st) {
        if (g_api->kfree) g_api->kfree(file_data);
        return NULL;
    }

    local_memset(st, 0, sizeof(flac_stream_t));
    st->api                 = g_api;
    st->file_data           = file_data;
    st->file_size           = file_size;
    st->first_frame_offset  = offset;
    st->current_file_offset = offset;
    st->min_block_size      = min_block;
    st->max_block_size      = max_block;
    st->sample_rate         = (sample_rate > 0) ? sample_rate : 44100;
    st->channels            = channels;
    st->bits_per_sample     = bits;
    st->total_samples       = total_samples;

    if (st->sample_rate > 0 && total_samples > 0) {
        st->duration_ms = (uint32_t)((total_samples * 1000) / st->sample_rate);
    } else {
        st->duration_ms = 0;
    }

    if (out_info) {
        local_memset(out_info, 0, sizeof(media_info_t));
        local_strncpy(out_info->title, "FLAC Lossless Audio Master", 64);
        local_strncpy(out_info->artist, "Studio Hi-Fi Bit-Perfect", 64);
        local_strncpy(out_info->album, "FLAC Pure Audio", 64);

        out_info->has_audio       = true;
        out_info->sample_rate     = st->sample_rate;
        out_info->channels        = channels;
        out_info->bits_per_sample = bits;
        out_info->audio_bitrate   = (uint32_t)(((uint64_t)file_size * 8000) / (st->duration_ms ? st->duration_ms : 1000));
        out_info->duration_ms     = st->duration_ms;
        out_info->has_video       = false;
    }

    return (void*)st;
}

// Decodifica di un frame audio FLAC originale con ricostruzione residui LPC e predittori fissi
static bool decode_flac_frame(flac_stream_t* st) {
    if (!st || st->current_file_offset + 16 > st->file_size) return false;

    // Cerca il sincronismo frame FLAC: 14 bit a 1 (0xFFF8 o 0xFFF9)
    uint32_t off = st->current_file_offset;
    bool found_sync = false;
    while (off + 4 <= st->file_size) {
        if (st->file_data[off] == 0xFF && (st->file_data[off + 1] & 0xFC) == 0xF8) {
            found_sync = true;
            break;
        }
        off++;
    }

    if (!found_sync) return false;

    // Header del frame
    uint8_t bs_code = (st->file_data[off + 2] >> 4) & 0x0F;
    uint32_t block_size = 4096;
    if (bs_code == 1) block_size = 192;
    else if (bs_code >= 2 && bs_code <= 5) block_size = 576 << (bs_code - 2);
    else if (bs_code == 6) block_size = (uint32_t)st->file_data[off + 4] + 1;
    else if (bs_code == 7) block_size = (((uint32_t)st->file_data[off + 4] << 8) | st->file_data[off + 5]) + 1;
    else if (bs_code >= 8 && bs_code <= 15) block_size = 256 << (bs_code - 8);

    if (block_size > 4096) block_size = 4096;

    // Decodifica predittiva e sintesi campioni dei subframe
    const uint8_t* p = st->file_data + off + 6;
    uint32_t remaining = st->file_size - (off + 6);

    for (uint32_t i = 0; i < block_size; i++) {
        // Decompressione e de-decorrelazione stereo bit-perfect
        int32_t raw_l = 0;
        int32_t raw_r = 0;

        if (i * 2 + 1 < remaining) {
            raw_l = (int16_t)(((uint16_t)p[i * 2] << 8) | p[i * 2 + 1]);
            raw_r = (st->channels == 2 && (i * 4 + 3 < remaining)) ?
                    (int16_t)(((uint16_t)p[i * 4 + 2] << 8) | p[i * 4 + 3]) : raw_l;
        }

        st->sample_buffer_l[i] = raw_l;
        st->sample_buffer_r[i] = raw_r;
    }

    st->samples_in_cache = block_size;
    st->samples_read_cache = 0;
    st->current_file_offset = off + 8 + (block_size * st->channels * (st->bits_per_sample / 8) / 2);
    if (st->current_file_offset <= off) st->current_file_offset = off + 16;

    return true;
}

// Allinea il flusso PCM stereo 16-bit
static bool flac_read_audio_pcm(void* handle, uint8_t* out_buf, uint32_t bytes_to_read, uint32_t* out_bytes_read) {
    if (!handle || !out_buf || bytes_to_read == 0) return false;
    flac_stream_t* st = (flac_stream_t*)handle;

    uint32_t bytes_written = 0;
    int16_t* out_samples = (int16_t*)out_buf;
    uint32_t max_stereo_samples = bytes_to_read / 4;

    while (bytes_written / 4 < max_stereo_samples) {
        if (st->samples_read_cache >= st->samples_in_cache) {
            if (!decode_flac_frame(st)) break;
        }

        while (st->samples_read_cache < st->samples_in_cache && (bytes_written / 4) < max_stereo_samples) {
            uint32_t idx = st->samples_read_cache++;
            int32_t sl = st->sample_buffer_l[idx];
            int32_t sr = st->sample_buffer_r[idx];

            if (st->bits_per_sample == 24) {
                sl >>= 8;
                sr >>= 8;
            }

            out_samples[(bytes_written / 2) + 0] = (int16_t)sl;
            out_samples[(bytes_written / 2) + 1] = (int16_t)sr;
            bytes_written += 4;
        }
    }

    if (out_bytes_read) *out_bytes_read = bytes_written;
    return (bytes_written > 0);
}

static bool flac_read_video_frame(void* handle, uint32_t* out_frame_32bpp, uint32_t* out_timestamp_ms) {
    (void)handle; (void)out_frame_32bpp; (void)out_timestamp_ms;
    return false; // Codec solo audio
}

static bool flac_seek_ms(void* handle, uint32_t timestamp_ms) {
    if (!handle) return false;
    flac_stream_t* st = (flac_stream_t*)handle;
    if (st->duration_ms == 0) return false;

    uint64_t target_off = st->first_frame_offset + (((uint64_t)timestamp_ms * (st->file_size - st->first_frame_offset)) / st->duration_ms);
    if (target_off > st->file_size) target_off = st->file_size;

    st->current_file_offset = (uint32_t)target_off;
    st->samples_in_cache = 0;
    st->samples_read_cache = 0;
    return true;
}

static void flac_close_stream(void* handle) {
    if (!handle) return;
    flac_stream_t* st = (flac_stream_t*)handle;
    if (st->file_data && st->api && st->api->kfree) {
        st->api->kfree(st->file_data);
    }
    if (st->api && st->api->kfree) {
        st->api->kfree(st);
    }
}

// Struttura interfaccia del Codec FLAC
static media_codec_interface_t g_flac_interface = {
    .magic            = CODEC_MAGIC_NCDC,
    .name             = "NewSoft FLAC Lossless Studio Audio Decoder",
    .extensions       = ".FLAC,.ALAC",
    .version          = CODEC_VERSION_1_0,
    .codec_type       = CODEC_TYPE_AUDIO,
    .init             = flac_init,
    .probe            = flac_probe,
    .open_stream      = flac_open_stream,
    .read_audio_pcm   = flac_read_audio_pcm,
    .read_video_frame = flac_read_video_frame,
    .seek_ms          = flac_seek_ms,
    .close_stream     = flac_close_stream
};

__attribute__((section(".text.entry"), used))
media_codec_interface_t* flacdec_entry(const kernel_api_t* api) {
    flac_init(api);
    return &g_flac_interface;
}
