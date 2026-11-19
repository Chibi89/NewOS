#include "dll_libc.h"
#include <arch/x86_64/io.h>
#include <drivers/ac97.h>
#include <drivers/hda.h>
#include <drivers/pci.h>
#include <mm/paging.h>
#include <drivers/sound.h>
#include <drivers/sound_data.h>
#include <drivers/sound_wav.h>
#include <drivers/speaker.h>
#include <drivers/timer.h>
#include <fs/vfs.h>
#include <mm/heap.h>
#include <string.h>

__attribute__((section(".header"), used)) module_header_t g_audio_header = {
    .magic = MODULE_MAGIC_NEWOS,
    .header_size = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type = MODULE_TYPE_SYSTEM_LIBRARY,
    .code_size = 0,
    .entry_offset = 64,
    .checksum = 0,
    .security_flags =
        SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name = "AUDIO",
    .signature = MODULE_AUTH_TAG};

static volatile uint32_t g_reloc_info[2] = { 0x52454C4Fu, 0xCAFEBABEu };

static void audio_apply_relocs(void) {
    if (g_reloc_info[0] != 0x52454C4Fu || g_reloc_info[1] == 0xCAFEBABEu) return;
    uintptr_t base = (uintptr_t)&g_audio_header;
    const uint32_t *tbl = (const uint32_t *)(base + g_reloc_info[1]);
    uint32_t count = tbl[0];
    for (uint32_t i = 0; i < count; i++) {
        uint32_t off = tbl[1 + i];
        *(uint64_t *)(base + off) += (uint64_t)base;
    }
    g_reloc_info[0] = 0;
}

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

__attribute__((force_align_arg_pointer))
int module_main(const kernel_api_t *api, audio_dll_params_t *p) {
    audio_apply_relocs();
    if (!api || !p) return -1;
    g_dll_api = api;
    switch (p->cmd) {
        case 1: sound_init(); break;
        case 2: sound_play((sound_event_t)p->uarg1); break;
        case 3: p->ret_bool = sound_is_playing(); break;
        case 4: sound_stop(); break;
        case 5: p->ret_str = sound_get_driver_name(); break;
        case 6: p->ret_bool = sound_is_ac97(); break;
        case 7: sound_set_volume((uint8_t)p->uarg1); break;
        case 8: p->ret_u32 = sound_get_volume(); break;
        case 9: p->ret_bool = sound_play_wav_file(p->sarg); break;
        case 10: p->ret_bool = ac97_is_available(); break;
        case 11: ac97_set_volume((uint8_t)p->uarg1); break;
        case 12: ac97_stop(); break;
        case 13: p->ret_str = ac97_get_device_name(); break;
        case 14: p->ret_bool = wav_parse((const uint8_t *)p->buf, p->uarg1, (wav_info_t *)p->out_ptr); break;
        default: return -1;
    }
    return 0;
}

#include "dll_ext_macros.h"

#undef sound_play
#undef sound_play_wav_file
#undef sound_stop
#undef sound_set_volume
#undef sound_is_playing
#undef sound_get_volume
#undef sound_get_driver_name
#undef sound_is_ac97
#undef ac97_is_available
#undef ac97_set_volume
#undef ac97_stop
#undef ac97_get_device_name

#include "../kernel/drivers/speaker.c"
#include "../kernel/drivers/ac97.c"
#include "../kernel/drivers/hda.c"
#include "../kernel/drivers/sound_data.c"
#include "../kernel/drivers/sound.c"
#include "../kernel/drivers/sound_wav.c"
