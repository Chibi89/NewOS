#include "dll_libc.h"
#include "explorer.h"
#include <apps/devmgmt.h>
#include <apps/explorer.h>
#include <apps/oobe.h>
#include <apps/regedit.h>
#include <apps/rules_viewer.h>
#include <apps/taskmgr.h>
#include <arch/x86_64/cpu.h>
#include <arch/x86_64/io.h>
#include <arch/x86_64/sched.h>
#include <arch/x86_64/smp.h>
#include <clipboard.h>
#include <comdlg.h>
#include <control_panel.h>
#include <drivers/ata.h>
#include <drivers/keyboard.h>
#include <drivers/net/net_nic.h>
#include <drivers/power.h>
#include <drivers/rtc.h>
#include <drivers/sound.h>
#include <drivers/sound_wav.h>
#include <drivers/timer.h>
#include <drivers/usb/usb.h>
#include <drivers/usb/usb_msc.h>
#include <fs/exfat.h>
#include <fs/registry.h>
#include <fs/vfs.h>
#include <fs/vfs_mount.h>
#include <gui/fontmgr.h>
#include <gui/context_menu.h>
#include <gui/graphics.h>
#include <gui/hwaccel_loader.h>
#include <gui/icons.h>
#include <gui/image.h>
#include <gui/screen_mgr.h>
#include <gui/theme_mgr.h>
#include <gui/uistyle_loader.h>
#include <gui/wm.h>
#include <mm/heap.h>
#include <notepad.h>
#include <oobe.h>
#include <settings.h>
#include <string.h>
#include <user.h>
#include <webbrowser.h>

__attribute__((section(".header"), used)) module_header_t g_explorer_header = {
    .magic = MODULE_MAGIC_NEWOS,
    .header_size = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type = MODULE_TYPE_SYSTEM_LIBRARY,
    .code_size = 0,
    .entry_offset = 64,
    .checksum = 0,
    .security_flags =
        SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name = "EXPLORER",
    .signature = MODULE_AUTH_TAG};

static volatile uint32_t g_reloc_info[2] = { 0x52454C4Fu, 0xCAFEBABEu };

static void explorer_apply_relocs(void) {
    if (g_reloc_info[0] != 0x52454C4Fu || g_reloc_info[1] == 0xCAFEBABEu) return;
    uintptr_t base = (uintptr_t)&g_explorer_header;
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
    const char *arg;
} explorer_dll_params_t;

void app_explorer_open_this_pc(void);
void app_explorer_open_drive_c(void);
void app_explorer_open_drive_d(void);
void app_explorer_open_drive_a(void);
void app_explorer_open_drive_u(void);
void app_explorer_open_folder(const char *folder_name);
void app_explorer_open_recycle_bin(void);
void app_control_panel_open(void);
void app_settings_open(void);

void context_menu_init(void);

__attribute__((force_align_arg_pointer))
int module_main(const kernel_api_t *api, explorer_dll_params_t *p) {
    explorer_apply_relocs();
    if (!api || !p) return -1;
    g_dll_api = api;
    static bool s_ctx_init = false;
    if (!s_ctx_init) {
        s_ctx_init = true;
        context_menu_init();
    }
    switch (p->cmd) {
        case 1: app_explorer_open_this_pc(); break;
        case 2: app_explorer_open_drive_c(); break;
        case 3: app_explorer_open_drive_d(); break;
        case 4: app_explorer_open_drive_a(); break;
        case 5: app_explorer_open_drive_u(); break;
        case 6: app_explorer_open_folder(p->arg); break;
        case 7: app_explorer_open_recycle_bin(); break;
        case 8: app_control_panel_open(); break;
        case 9: app_settings_open(); break;
        default: return -1;
    }
    return 0;
}

#include "dll_ext_macros.h"

#undef app_explorer_open_this_pc
#undef app_explorer_open_drive_c
#undef app_explorer_open_drive_d
#undef app_explorer_open_drive_a
#undef app_explorer_open_drive_u
#undef app_explorer_open_folder
#undef app_explorer_open_recycle_bin
#undef app_control_panel_open
#undef app_settings_open
#undef context_menu_build_for_empty
#undef context_menu_build_for_item
#undef context_menu_draw
#undef context_menu_execute_primary
#undef context_menu_handle_click
#undef context_menu_show

#include "../kernel/gui/context_menu.c"
#include "../explorer/explorer.c"
