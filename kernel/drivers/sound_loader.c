#include <drivers/ac97.h>
#include <drivers/sound.h>
#include <drivers/sound_wav.h>
#include <fs/vfs.h>
#include <fs/vfs_mount.h>
#include <gui/screen_mgr.h>
#include <mm/heap.h>
#include <string.h>

typedef struct {
    uint32_t cmd;
    uint32_t uarg1;
    const char *sarg;
    const void *buf;
    void *out_ptr;
    uint32_t ret_u32;
    bool ret_bool;
    const char *ret_str;
} audio_dll_params_t;

typedef int (*audio_entry_fn)(const kernel_api_t *api, audio_dll_params_t *p);

static audio_entry_fn g_audio_entry = NULL;
static bool g_audio_hw_inited = false;

static bool ensure_audio_loaded(void) {
    if (g_audio_entry) {
        if (!g_audio_hw_inited) {
            g_audio_hw_inited = true;
            audio_dll_params_t p;
            memset(&p, 0, sizeof(p));
            p.cmd = 1;
            g_audio_entry(screen_mgr_get_api(), &p);
        }
        return true;
    }
    if (!vfs_is_drive_mounted('C')) return false;

    const char *paths[] = {
        "C:\\NewOS\\SystemLibrary\\AUDIO.DLL",
        "/NewOS/SystemLibrary/AUDIO.DLL"
    };
    for (int i = 0; i < 2 && !g_audio_entry; i++) {
        uint32_t sz = 0;
        uint8_t *buf = (uint8_t *)vfs_read_entire_file(paths[i], &sz);
        if (buf && sz >= sizeof(module_header_t)) {
            if (screen_mgr_verify_module(buf, sz, MODULE_TYPE_SYSTEM_LIBRARY, NULL)) {
                uint8_t *exec_buf = (uint8_t *)kmalloc(sz + 32768);
                if (exec_buf) {
                    memcpy(exec_buf, buf, sz);
                    memset(exec_buf + sz, 0, 32768);
                    const module_header_t *h = (const module_header_t *)exec_buf;
                    g_audio_entry = (audio_entry_fn)(exec_buf + h->entry_offset);
                }
            }
        }
        if (buf) kfree(buf);
    }
    if (g_audio_entry && !g_audio_hw_inited) {
        g_audio_hw_inited = true;
        audio_dll_params_t p;
        memset(&p, 0, sizeof(p));
        p.cmd = 1;
        g_audio_entry(screen_mgr_get_api(), &p);
    }
    return (g_audio_entry != NULL);
}

void sound_init(void) {
    ensure_audio_loaded();
}

void sound_play(sound_event_t event) {
    if (!ensure_audio_loaded()) return;
    audio_dll_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 2;
    p.uarg1 = (uint32_t)event;
    g_audio_entry(screen_mgr_get_api(), &p);
}

bool sound_is_playing(void) {
    if (!ensure_audio_loaded()) return false;
    audio_dll_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 3;
    g_audio_entry(screen_mgr_get_api(), &p);
    return p.ret_bool;
}

void sound_stop(void) {
    if (!ensure_audio_loaded()) return;
    audio_dll_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 4;
    g_audio_entry(screen_mgr_get_api(), &p);
}

const char *sound_get_driver_name(void) {
    if (!ensure_audio_loaded()) return "Audio non caricato";
    audio_dll_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 5;
    g_audio_entry(screen_mgr_get_api(), &p);
    return p.ret_str ? p.ret_str : "PC Speaker";
}

bool sound_is_ac97(void) {
    if (!ensure_audio_loaded()) return false;
    audio_dll_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 6;
    g_audio_entry(screen_mgr_get_api(), &p);
    return p.ret_bool;
}

void sound_set_volume(uint8_t vol_pct) {
    if (!ensure_audio_loaded()) return;
    audio_dll_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 7;
    p.uarg1 = vol_pct;
    g_audio_entry(screen_mgr_get_api(), &p);
}

uint8_t sound_get_volume(void) {
    if (!ensure_audio_loaded()) return 100;
    audio_dll_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 8;
    g_audio_entry(screen_mgr_get_api(), &p);
    return (uint8_t)p.ret_u32;
}

bool sound_play_wav_file(const char *vfs_path) {
    if (!ensure_audio_loaded()) return false;
    audio_dll_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 9;
    p.sarg = vfs_path;
    g_audio_entry(screen_mgr_get_api(), &p);
    return p.ret_bool;
}

bool ac97_is_available(void) {
    if (!ensure_audio_loaded()) return false;
    audio_dll_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 10;
    g_audio_entry(screen_mgr_get_api(), &p);
    return p.ret_bool;
}

void ac97_set_volume(uint8_t volume_percent) {
    if (!ensure_audio_loaded()) return;
    audio_dll_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 11;
    p.uarg1 = volume_percent;
    g_audio_entry(screen_mgr_get_api(), &p);
}

void ac97_stop(void) {
    if (!ensure_audio_loaded()) return;
    audio_dll_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 12;
    g_audio_entry(screen_mgr_get_api(), &p);
}

const char *ac97_get_device_name(void) {
    if (!ensure_audio_loaded()) return "Audio non inizializzato";
    audio_dll_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 13;
    g_audio_entry(screen_mgr_get_api(), &p);
    return p.ret_str ? p.ret_str : "Audio non inizializzato";
}

bool wav_parse(const uint8_t *data, uint32_t size, wav_info_t *out_info) {
    if (!ensure_audio_loaded()) return false;
    audio_dll_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 14;
    p.buf = data;
    p.uarg1 = size;
    p.out_ptr = out_info;
    g_audio_entry(screen_mgr_get_api(), &p);
    return p.ret_bool;
}
