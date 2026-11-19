#include "dll_libc.h"
#include "../autoplay/autoplay.h"
#include <arch/x86_64/cpu.h>
#include <arch/x86_64/io.h>
#include <arch/x86_64/sched.h>
#include <arch/x86_64/smp.h>
#include <clipboard.h>
#include <comdlg.h>
#include <control_panel.h>
#include <drivers/ac97.h>
#include <drivers/ata.h>
#include <drivers/driver_mgr.h>
#include <drivers/net/net_nic.h>
#include <drivers/net/wifi.h>
#include <drivers/power.h>
#include <drivers/rtc.h>
#include <drivers/sound.h>
#include <drivers/sound_wav.h>
#include <drivers/timer.h>
#include <drivers/usb/ohci.h>
#include <drivers/usb/uhci.h>
#include <drivers/usb/usb.h>
#include <drivers/usb/usb_msc.h>
#include <drivers/usb/xhci.h>
#include <drivers/vga.h>
#include <explorer.h>
#include <fs/exfat.h>
#include <fs/registry.h>
#include <fs/vfs.h>
#include <fs/vfs_mount.h>
#include <gui/fontmgr.h>
#include <gui/graphics.h>
#include <gui/icons.h>
#include <gui/wm.h>
#include <ipc/ipc.h>
#include <mm/heap.h>
#include <mm/paging.h>
#include <net/dhcp.h>
#include <net/dns.h>
#include <net/icmp.h>
#include <net/tcp.h>
#include <notepad.h>
#include <oobe.h>
#include <string.h>
#include <user.h>
#include <webbrowser.h>

__attribute__((section(".header"), used)) module_header_t g_sysapps_header = {
    .magic = MODULE_MAGIC_NEWOS,
    .header_size = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type = MODULE_TYPE_SYSTEM_LIBRARY,
    .code_size = 0,
    .entry_offset = 64,
    .checksum = 0,
    .security_flags =
        SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name = "SYSAPPS",
    .signature = MODULE_AUTH_TAG};

static volatile uint32_t g_reloc_info[2] = { 0x52454C4Fu, 0xCAFEBABEu };

static void sysapps_apply_relocs(void) {
    if (g_reloc_info[0] != 0x52454C4Fu || g_reloc_info[1] == 0xCAFEBABEu) return;
    uintptr_t base = (uintptr_t)&g_sysapps_header;
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
    const char *arg1;
    const char *arg2;
    uint32_t uarg1;
    uint32_t uarg2;
} sysapps_params_t;

void app_notepad_launch(void);
void app_notepad_open_file(const char *title, const char *content);
void app_calc_launch(void);
void app_player_launch(void);
void app_player_open_file(const char *filepath);
void app_player_update_tick(void);
void app_player_on_close(void);
void app_autoplay_launch(char drive_letter);
void app_autoplay_launch_mode(char drive_letter, autoplay_mode_t mode);
void app_taskmgr_launch(void);
void app_cmd_launch(void);
void app_regedit_launch(void);
void app_rules_viewer_launch(void);
void app_rules_viewer_open_file(const char *filepath);
void app_whatsnew_launch(void);

__attribute__((force_align_arg_pointer))
int module_main(const kernel_api_t *api, sysapps_params_t *p) {
    sysapps_apply_relocs();
    if (!api || !p) return -1;
    g_dll_api = api;
    switch (p->cmd) {
        case 1: app_notepad_launch(); break;
        case 2: app_notepad_open_file(p->arg1, p->arg2); break;
        case 3: app_calc_launch(); break;
        case 4: app_player_launch(); break;
        case 5: app_player_open_file(p->arg1); break;
        case 6: app_player_update_tick(); break;
        case 7: app_player_on_close(); break;
        case 8: app_autoplay_launch((char)p->uarg1); break;
        case 9: app_autoplay_launch_mode((char)p->uarg1, (autoplay_mode_t)p->uarg2); break;
        case 10: app_taskmgr_launch(); break;
        case 11: app_cmd_launch(); break;
        case 12: app_regedit_launch(); break;
        case 13: app_rules_viewer_launch(); break;
        case 14: app_rules_viewer_open_file(p->arg1); break;
        case 15: app_whatsnew_launch(); break;
        default: return -1;
    }
    return 0;
}

#include "dll_ext_macros.h"

#undef app_notepad_open_file
#undef app_calc_launch
#undef app_cmd_launch
#undef app_notepad_launch
#undef app_player_launch
#undef app_player_open_file
#undef app_regedit_launch
#undef app_rules_viewer_launch
#undef app_rules_viewer_open_file
#undef app_taskmgr_launch

#include "../notepad/notepad.c"
#include "../calculator/calculator.c"
#include "../player/player.c"
#include "../autoplay/autoplay.c"
#include "../taskmgr/taskmgr.c"
#include "../kernel/apps/cmd.c"
#include "../kernel/apps/regedit.c"
#include "../rules_viewer/rules_viewer.c"
#include "../whatsnew/whatsnew.c"
