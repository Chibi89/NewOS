#include "dll_libc.h"
#include <arch/x86_64/cpu.h>
#include <arch/x86_64/io.h>
#include <arch/x86_64/sched.h>
#include <arch/x86_64/smp.h>
#include <control_panel.h>
#include <drivers/ac97.h>
#include <drivers/acpi.h>
#include <drivers/ata.h>
#include <drivers/driver_mgr.h>
#include <drivers/net/net_nic.h>
#include <drivers/net/wifi.h>
#include <drivers/nvme.h>
#include <drivers/pci.h>
#include <drivers/power.h>
#include <drivers/rtc.h>
#include <drivers/sound.h>
#include <drivers/timer.h>
#include <drivers/usb/ohci.h>
#include <drivers/usb/uhci.h>
#include <drivers/usb/usb.h>
#include <drivers/usb/usb_msc.h>
#include <drivers/usb/xhci.h>
#include <drivers/vga.h>
#include <gpu3d.h>
#include <fs/exfat.h>
#include <fs/registry.h>
#include <fs/vfs.h>
#include <gui/graphics.h>
#include <gui/cursor.h>
#include <gui/cursor_loader.h>
#include <gui/hwaccel_loader.h>
#include <hwaccel.h>
#include <gui/icons.h>
#include <gui/theme_mgr.h>
#include <gui/wm.h>
#include <mm/heap.h>
#include <mm/paging.h>
#include <notepad.h>
#include <string.h>
#include <user.h>

__attribute__((section(".header"), used)) module_header_t g_cplext2_header = {
    .magic = MODULE_MAGIC_NEWOS,
    .header_size = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type = MODULE_TYPE_SYSTEM_LIBRARY,
    .code_size = 0,
    .entry_offset = 64,
    .checksum = 0,
    .security_flags =
        SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name = "CPLEXT2",
    .signature = MODULE_AUTH_TAG};

static volatile uint32_t g_reloc_info[2] = { 0x52454C4Fu, 0xCAFEBABEu };

static void cplext2_apply_relocs(void) {
    if (g_reloc_info[0] != 0x52454C4Fu || g_reloc_info[1] == 0xCAFEBABEu) return;
    uintptr_t base = (uintptr_t)&g_cplext2_header;
    const uint32_t *tbl = (const uint32_t *)(base + g_reloc_info[1]);
    uint32_t count = tbl[0];
    for (uint32_t i = 0; i < count; i++) {
        uint32_t off = tbl[1 + i];
        *(uint64_t *)(base + off) += (uint64_t)base;
    }
    g_reloc_info[0] = 0;
}

typedef struct {
    uint32_t cmd; // 1=about, 2=display, 3=sound, 4=power, 5=shutdown_dlg, 6=mouse, 7=devmgmt
} cplext2_params_t;

void app_about_launch(void);
void app_display_props_launch(void);
void app_sound_props_launch(void);
void app_power_props_launch(void);
void app_shutdown_dialog_launch(void);
void app_mouse_props_launch(void);
void app_devmgmt_launch(void);

__attribute__((force_align_arg_pointer))
int module_main(const kernel_api_t *api, cplext2_params_t *p) {
    cplext2_apply_relocs();
    if (!api || !p) return -1;
    g_dll_api = api;
    switch (p->cmd) {
        case 1: app_about_launch(); break;
        case 2: app_display_props_launch(); break;
        case 3: app_sound_props_launch(); break;
        case 4: app_power_props_launch(); break;
        case 5: app_shutdown_dialog_launch(); break;
        case 6: app_mouse_props_launch(); break;
        case 7: app_devmgmt_launch(); break;
        default: return -1;
    }
    return 0;
}

#include "dll_ext_macros.h"

#undef app_about_launch
#undef app_display_props_launch
#undef app_sound_props_launch
#undef app_power_props_launch
#undef app_mouse_props_launch
#undef app_devmgmt_launch

#include "../control_panel/about.c"
#include "../control_panel/display.c"
#include "../control_panel/sound_props.c"
#include "../control_panel/power_props.c"
#include "../control_panel/mouse_props.c"
#include "../control_panel/devmgmt.c"
