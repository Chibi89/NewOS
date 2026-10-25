/**
 * MultimediaCodec/media_codec.h
 * Specifiche dell'architettura e delle interfacce universali per i codec multimediali .CDC
 * Progetto NewOS V1 (64-bit) - Sistema Operativo Ufficiale
 * (C) Newspoint Software - NewSoft Media Subsystem
 */

#ifndef MEDIA_CODEC_H
#define MEDIA_CODEC_H

#include <types.h>
#include <screen_modules.h>

#define CODEC_MAGIC_NCDC        0x4344434E  // Signature 'N','C','D','C' in little-endian
#define CODEC_VERSION_1_0       0x00010000

#define CODEC_TYPE_AUDIO        1
#define CODEC_TYPE_VIDEO        2
#define CODEC_TYPE_DEMUXER      3

// Informazioni descrittive estratte dal file o dallo stream
typedef struct {
    char        title[64];
    char        artist[64];
    char        album[64];
    uint32_t    duration_ms;       // Durata totale in millisecondi

    // Parametri Audio
    bool        has_audio;
    uint32_t    sample_rate;       // Es. 44100, 48000
    uint16_t    channels;          // 1 = Mono, 2 = Stereo
    uint16_t    bits_per_sample;   // 8, 16, 24
    uint32_t    audio_bitrate;     // in bps (es. 192000 per 192 kbps)

    // Parametri Video
    bool        has_video;
    uint32_t    width;             // Larghezza frame (es. 640)
    uint32_t    height;            // Altezza frame (es. 360)
    uint32_t    fps;               // Frame al secondo (es. 30)
    uint32_t    total_frames;      // Numero totale stimato di fotogrammi
    uint32_t    video_bitrate;     // in bps
    uint32_t    aspect_ratio_num;  // Es. 16
    uint32_t    aspect_ratio_den;  // Es. 9
} media_info_t;

// Interfaccia universale esposta da ciascun modulo .CDC
typedef struct media_codec_interface {
    uint32_t magic;              // Magic Signature 'NCDC' (0x4344434E)
    char     name[64];           // Es: "NewSoft MPEG Layer-3 Audio Decoder"
    char     extensions[64];     // Estensioni gestite: ".MP3" o ".WAV" o ".AVI" o ".MP4"
    uint32_t version;            // Versione codec (es: 0x00010000)
    uint32_t codec_type;         // CODEC_TYPE_AUDIO, CODEC_TYPE_VIDEO, CODEC_TYPE_DEMUXER

    // Inizializzazione modulo con la tabella API di sistema
    bool (*init)(const kernel_api_t* api);

    // Probing rapido per verificare se il buffer/file appartiene a questo formato
    bool (*probe)(const uint8_t* header, uint32_t size);

    // Apertura del file/stream multimediale e lettura metadati
    void* (*open_stream)(const char* path, media_info_t* out_info);

    // Lettura campioni audio PCM (standardizzato su 16-bit interleaved stereo per DMA AC97)
    bool (*read_audio_pcm)(void* handle, uint8_t* out_buf, uint32_t bytes_to_read, uint32_t* out_bytes_read);

    // Lettura prossimo frame video (standardizzato su 32bpp 0x00RRGGBB / BGRA)
    bool (*read_video_frame)(void* handle, uint32_t* out_frame_32bpp, uint32_t* out_timestamp_ms);

    // Posizionamento temporale (Seek al millisecondo indicato)
    bool (*seek_ms)(void* handle, uint32_t timestamp_ms);

    // Chiusura dello stream e deallocazione di tutti i buffer associati
    void (*close_stream)(void* handle);
} media_codec_interface_t;

// Tipo di funzione entry-point esportata dal modulo .CDC all'offset 64
typedef media_codec_interface_t* (*codec_entry_fn)(const kernel_api_t* api);

#endif // MEDIA_CODEC_H
