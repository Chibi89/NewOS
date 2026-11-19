#include "control_panel.h"
#include "cpl_theme.h"
#include <arch/x86_64/cpu.h>
#include <drivers/ac97.h>
#include <drivers/acpi.h>
#include <drivers/ata.h>
#include <drivers/driver_mgr.h>
#include <drivers/net/net_nic.h>
#include <drivers/net/wifi.h>
#include <drivers/nvme.h>
#include <drivers/pci.h>
#include <drivers/sound.h>
#include <drivers/usb/usb.h>
#include <drivers/usb/usb_msc.h>
#include <gpu3d.h>
#include <gui/graphics.h>
#include <gui/hwaccel_loader.h>
#include <gui/icons.h>
#include <gui/wm.h>
#include <mm/heap.h>
#include <string.h>

#define DM_MAX_CATS 14
#define DM_MAX_DEVS 48

typedef struct {
    char      name[64];
    char      vendor[40];
    char      bus_loc[48];
    char      driver_name[32];
    char      resources[64];
    char      extra[64];
    bool      has_driver;
    icon_id_t icon;
} dm_device_t;

typedef struct {
    const char* title;
    icon_id_t   icon;
    bool        expanded;
    int         dev_indices[16];
    int         dev_count;
} dm_category_t;

typedef struct {
    dm_category_t cats[DM_MAX_CATS];
    int           cat_count;
    dm_device_t   devs[DM_MAX_DEVS];
    int           dev_count;
    int           selected_row;
    int           scroll_row;
} devmgmt_state_t;

static const char* dm_pci_vendor_name(uint16_t vid) {
    switch (vid) {
        case 0x8086: return "Intel Corporation";
        case 0x10EC: return "Realtek Semiconductor";
        case 0x10DE: return "NVIDIA Corporation";
        case 0x1002: return "AMD / ATI Technologies";
        case 0x1022: return "Advanced Micro Devices (AMD)";
        case 0x15AD: return "VMware Inc.";
        case 0x1AF4: return "Red Hat, Inc. (VirtIO)";
        case 0x1234: return "QEMU / Bochs Virtual HW";
        case 0x80EE: return "Oracle VirtualBox";
        case 0x1B36: return "Red Hat QEMU PCIe";
        default:     return "Dispositivo PCI Generico";
    }
}

static int dm_add_cat(devmgmt_state_t* st, const char* title, icon_id_t icon, bool expanded) {
    if (st->cat_count >= DM_MAX_CATS) return -1;
    int idx = st->cat_count++;
    st->cats[idx].title = title;
    st->cats[idx].icon = icon;
    st->cats[idx].expanded = expanded;
    st->cats[idx].dev_count = 0;
    return idx;
}

static void dm_add_dev(devmgmt_state_t* st, int cat_idx, const char* name, const char* vendor,
                       const char* bus_loc, const char* drv, const char* res, const char* extra,
                       bool has_driver, icon_id_t icon) {
    if (cat_idx < 0 || cat_idx >= st->cat_count) return;
    if (st->dev_count >= DM_MAX_DEVS) return;
    if (st->cats[cat_idx].dev_count >= 16) return;

    int d_idx = st->dev_count++;
    dm_device_t* d = &st->devs[d_idx];
    memset(d, 0, sizeof(*d));
    strncpy(d->name, name ? name : "Dispositivo", sizeof(d->name) - 1);
    strncpy(d->vendor, vendor ? vendor : "Standard", sizeof(d->vendor) - 1);
    strncpy(d->bus_loc, bus_loc ? bus_loc : "Sistema", sizeof(d->bus_loc) - 1);
    strncpy(d->driver_name, drv ? drv : "Nessuno", sizeof(d->driver_name) - 1);
    strncpy(d->resources, res ? res : "Automatiche", sizeof(d->resources) - 1);
    strncpy(d->extra, extra ? extra : "-", sizeof(d->extra) - 1);
    d->has_driver = has_driver;
    d->icon = icon;

    st->cats[cat_idx].dev_indices[st->cats[cat_idx].dev_count++] = d_idx;
}

static void devmgmt_scan_hardware(devmgmt_state_t* st) {
    memset(st, 0, sizeof(*st));

    int c_comp    = dm_add_cat(st, "Computer", ICON_DESKTOP, true);
    int c_cpu     = dm_add_cat(st, "Processori", ICON_MAINTENANCE, true);
    int c_gpu     = dm_add_cat(st, "Schede video", ICON_DESKTOP, true);
    int c_mon     = dm_add_cat(st, "Monitor", ICON_DESKTOP, false);
    int c_stor    = dm_add_cat(st, "Controller di archiviazione", ICON_HD, false);
    int c_ide     = dm_add_cat(st, "Controller IDE ATA/ATAPI", ICON_HD, false);
    int c_disk    = dm_add_cat(st, "Unita disco", ICON_HD, true);
    int c_audio   = dm_add_cat(st, "Controller audio, video e giochi", ICON_SOUND, true);
    int c_net     = dm_add_cat(st, "Schede di rete", ICON_NET, true);
    int c_usbc    = dm_add_cat(st, "Controller USB", ICON_MAINTENANCE, false);
    int c_usbd    = dm_add_cat(st, "Dispositivi USB", ICON_MAINTENANCE, false);
    int c_kbd     = dm_add_cat(st, "Tastiere", ICON_KEYBOARD, false);
    int c_mouse   = dm_add_cat(st, "Mouse e altri dispositivi di puntamento", ICON_MOUSE, false);
    int c_sys     = dm_add_cat(st, "Dispositivi di sistema", ICON_INFO, false);

    const acpi_info_t* acpi = acpi_get_info();
    const cpu_info_t* cpu = cpu_get_info();
    const hwaccel_info_t* hwa = hwaccel_get_info();
    const gpu3d_info_t* g3d = gpu3d_get_info();

    // 1. Computer
    if (acpi && acpi->available) {
        char extra[64];
        mini_snprintf(extra, sizeof(extra), "ACPI Rev %u | LAPIC 0x%X", (unsigned int)acpi->revision, (unsigned int)acpi->lapic_addr);
        dm_add_dev(st, c_comp, "PC basato su ACPI x64", "ACPI / UEFI-BIOS", "Root System", "ACPI.SYS + POWER.SYS", "PM1a / FADT / MADT", extra, true, ICON_DESKTOP);
    } else {
        dm_add_dev(st, c_comp, "PC Standard x86_64", "IBM/PC Compatibile", "Root System", "POWER.SYS", "Legacy I/O", "Modalita compatibile", true, ICON_DESKTOP);
    }

    // 2. Processori
    int n_cpus = (acpi && acpi->cpu_count > 0) ? acpi->cpu_count : (int)(cpu ? cpu->logical_cores : 1);
    if (n_cpus < 1) n_cpus = 1;
    if (n_cpus > 8) n_cpus = 8;
    const char* cpu_brand = (cpu && cpu->brand[0]) ? cpu->brand : "Processore x86_64";
    for (int i = 0; i < n_cpus; i++) {
        char loc[48], extra[64];
        mini_snprintf(loc, sizeof(loc), "APIC ID %d (Core logico %d)", (acpi && i < acpi->cpu_count) ? acpi->cpu_apic_ids[i] : i, i);
        mini_snprintf(extra, sizeof(extra), "SSE2: %s | AVX: %s", (cpu && cpu->has_sse2) ? "Si" : "No", (cpu && cpu->has_avx) ? "Si" : "No");
        dm_add_dev(st, c_cpu, cpu_brand, (cpu && cpu->vendor[0]) ? cpu->vendor : "x86_64", loc, "CPU.SYS (64-bit)", "MSR / LAPIC", extra, true, ICON_MAINTENANCE);
    }

    // 3. Monitor (EDID)
    if (hwa) {
        char res[64], extra[64];
        mini_snprintf(res, sizeof(res), "%ux%ux32 @ %u DPI", (unsigned int)hwa->lfb_base ? 1024U : 1024U, 768U, (unsigned int)hwa->detected_dpi);
        mini_snprintf(extra, sizeof(extra), "EDID: %s | Scala: %u%%", hwa->edid_valid ? "Valido" : "Generico", (unsigned int)hwa->scale_percent);
        dm_add_dev(st, c_mon, hwa->monitor_name[0] ? hwa->monitor_name : "Generic PnP Monitor", "VESA DDC/EDID", "Display Output 0", "HWACCEL.DLL", res, extra, true, ICON_DESKTOP);
    }

    // 4. Scansione reale Bus PCI/PCIe (0..255)
    int pci_cnt = pci_get_device_count();
    for (int i = 0; i < pci_cnt; i++) {
        const pci_device_t* p = pci_get_device(i);
        if (!p) continue;
        char loc[48], res[64], extra[64];
        uint64_t bar0 = pci_get_bar_addr(p, 0);
        mini_snprintf(loc, sizeof(loc), "PCI Bus %u, Slot %u, Func %u", (unsigned int)p->bus, (unsigned int)p->slot, (unsigned int)p->func);
        mini_snprintf(res, sizeof(res), "IRQ: %u | BAR0: 0x%X", (unsigned int)p->irq, (unsigned int)(bar0 & 0xFFFFFFFFULL));
        mini_snprintf(extra, sizeof(extra), "VID:0x%04X DID:0x%04X Class:%02X.%02X", p->vendor_id, p->device_id, p->class_id, p->subclass_id);
        const char* vname = dm_pci_vendor_name(p->vendor_id);

        if (p->class_id == 0x03) {
            const char* gname = (hwa && hwa->gpu_name[0]) ? hwa->gpu_name : "Scheda Video PCI";
            const char* gdrv = (g3d && g3d->initialized) ? "GPU3D.DLL + HWACCEL.DLL" : "VGA.SYS + HWACCEL.DLL";
            dm_add_dev(st, c_gpu, gname, vname, loc, gdrv, res, extra, true, ICON_DESKTOP);
        } else if (p->class_id == 0x01 && p->subclass_id == 0x08) {
            dm_add_dev(st, c_stor, nvme_is_present() ? nvme_get_model() : "Controller NVMe PCIe", vname, loc, "NVME.DLL", res, extra, true, ICON_HD);
        } else if (p->class_id == 0x01 && p->subclass_id == 0x06) {
            dm_add_dev(st, c_stor, "Controller SATA AHCI 1.0", vname, loc, "AHCI.SYS", res, extra, true, ICON_HD);
        } else if (p->class_id == 0x01 && p->subclass_id == 0x01) {
            dm_add_dev(st, c_ide, "Controller IDE ATA/ATAPI PCI", vname, loc, "ATA.SYS", res, extra, true, ICON_HD);
        } else if (p->class_id == 0x04) {
            const char* aname = (p->subclass_id == 0x03) ? "Intel High Definition Audio (HDA)" :
                                ((p->subclass_id == 0x01) ? "Controller Audio AC'97" : "Controller Multimediale PCI");
            dm_add_dev(st, c_audio, aname, vname, loc, "AUDIO.DLL", res, extra, (p->subclass_id == 0x03 || p->subclass_id == 0x01), ICON_SOUND);
        } else if (p->class_id == 0x02) {
            const net_nic_t* nic = net_nic_get_active();
            const char* nname = (p->subclass_id == 0x80) ? "Controller Wi-Fi PCIe" :
                                ((nic && nic->name[0]) ? nic->name : "Scheda Ethernet PCI");
            dm_add_dev(st, c_net, nname, vname, loc, "NETSTACK.DLL", res, extra, true, ICON_NET);
        } else if (p->class_id == 0x0C && p->subclass_id == 0x03) {
            const char* uname = (p->prog_if == 0x30) ? "Controller Host xHCI (USB 3.x)" :
                                ((p->prog_if == 0x20) ? "Controller Host EHCI (USB 2.0)" :
                                ((p->prog_if == 0x10) ? "Controller Host OHCI (USB 1.1)" : "Controller Host UHCI (USB 1.1)"));
            dm_add_dev(st, c_usbc, uname, vname, loc, "USB Stack (Kernel)", res, extra, p->prog_if != 0x20, ICON_MAINTENANCE);
        } else if (p->class_id == 0x06 || p->class_id == 0x0C || p->class_id == 0x08) {
            const char* sname = (p->class_id == 0x06 && p->subclass_id == 0x00) ? "Host Bridge / Root Complex PCI" :
                                ((p->class_id == 0x06 && p->subclass_id == 0x01) ? "Bridge PCI-to-ISA / LPC" :
                                ((p->class_id == 0x06 && p->subclass_id == 0x04) ? "PCI-to-PCI / PCIe Root Port" :
                                ((p->class_id == 0x0C && p->subclass_id == 0x05) ? "Controller SMBus" : "Dispositivo di Sistema PCI")));
            dm_add_dev(st, c_sys, sname, vname, loc, "PCI.SYS", res, extra, true, ICON_INFO);
        }
    }

    // 5. Unita disco fisiche (ATA/ATAPI + NVMe + USB MSC)
    int ata_cnt = ata_get_device_count();
    for (int i = 0; i < ata_cnt; i++) {
        const ata_device_t* d = ata_get_device(i);
        if (!d || !d->present) continue;
        char extra[64], res[64];
        mini_snprintf(res, sizeof(res), "Capacita: %u MB | FS: %s", (unsigned int)d->size_mb, d->part_fs[0] ? d->part_fs : "RAW");
        mini_snprintf(extra, sizeof(extra), "%s", d->is_atapi ? atapi_get_disc_type_name(d) : (d->is_ahci ? "SATA AHCI" : "IDE/ATA"));
        dm_add_dev(st, c_disk, d->model[0] ? d->model : "Disco ATA/ATAPI", "Storage Fisico", d->is_atapi ? "Canale ATAPI" : "Canale ATA/SATA", "ATA.SYS / AHCI.SYS", res, extra, true, ICON_HD);
    }
    if (nvme_is_present()) {
        char res[64];
        uint64_t mb = (nvme_get_capacity_sectors() * 512ULL) / (1024ULL * 1024ULL);
        mini_snprintf(res, sizeof(res), "Capacita: %u MB", (unsigned int)mb);
        dm_add_dev(st, c_disk, nvme_get_model(), "NVMe Namespace 1", "PCIe M.2 / NVMe", "NVME.DLL", res, "Queue DMA 64-bit", true, ICON_HD);
    }
    if (usb_msc_is_present()) {
        const usb_msc_device_t* u = usb_msc_get_device();
        char res[64];
        uint64_t umb = u ? ((uint64_t)u->total_sectors * 512ULL) / (1024ULL * 1024ULL) : 0ULL;
        mini_snprintf(res, sizeof(res), "Capacita: %u MB", (unsigned int)umb);
        dm_add_dev(st, c_disk, (u && u->product[0]) ? u->product : "Unita Flash USB", "USB Mass Storage", "Bus USB", "USBMSC.SYS", res, "Bulk-Only Transport", true, ICON_HD);
    }

    // 6. Dispositivi USB enumerati
    int ucnt = usb_get_device_count();
    for (int i = 0; i < ucnt; i++) {
        const usb_device_t* ud = usb_get_device(i);
        if (!ud) continue;
        char extra[64], loc[48];
        mini_snprintf(loc, sizeof(loc), "USB Addr %u (%s)", (unsigned int)ud->address, usb_controller_type_to_str(ud->controller_type));
        mini_snprintf(extra, sizeof(extra), "Class: %s | Speed: %s", usb_class_to_str(ud->class_code), usb_speed_to_str(ud->speed));
        dm_add_dev(st, c_usbd, ud->product[0] ? ud->product : "Dispositivo USB", ud->manufacturer[0] ? ud->manufacturer : "USB Implementers", loc, "USB Stack", "Endpoint Control/Bulk", extra, true, ICON_MAINTENANCE);
    }

    // 7. Tastiere, Mouse e Dispositivi di sistema base
    dm_add_dev(st, c_kbd, "Tastiera standard PS/2", "Standard 101/102 tasti", "Porta 0x60 / IRQ 1", "PS2KBD.SYS", "IRQ: 1 | I/O: 0x0060", "Layout IT attivo", true, ICON_KEYBOARD);
    dm_add_dev(st, c_mouse, "Mouse compatibile PS/2", "Microsoft / Generic PS/2", "Porta 0x60 / IRQ 12", "PS2MOUSE.SYS", "IRQ: 12 | I/O: 0x0060", "Risoluzione 1024x768", true, ICON_MOUSE);
    dm_add_dev(st, c_sys, "Orologio di sistema CMOS / Real-Time", "Standard AT/ACPI", "I/O 0x70 / IRQ 8", "RTC.SYS", "IRQ: 8 | I/O: 0x0070", "Formato 24h", true, ICON_INFO);
}

static int devmgmt_visible_count(const devmgmt_state_t* st) {
    int rows = 0;
    for (int c = 0; c < st->cat_count; c++) {
        if (st->cats[c].dev_count == 0) continue;
        rows++;
        if (st->cats[c].expanded) rows += st->cats[c].dev_count;
    }
    return rows;
}

static bool devmgmt_get_row(const devmgmt_state_t* st, int target_row, int* out_cat, int* out_dev) {
    int r = 0;
    for (int c = 0; c < st->cat_count; c++) {
        if (st->cats[c].dev_count == 0) continue;
        if (r == target_row) {
            *out_cat = c;
            *out_dev = -1;
            return true;
        }
        r++;
        if (st->cats[c].expanded) {
            for (int j = 0; j < st->cats[c].dev_count; j++) {
                if (r == target_row) {
                    *out_cat = c;
                    *out_dev = st->cats[c].dev_indices[j];
                    return true;
                }
                r++;
            }
        }
    }
    return false;
}

static void devmgmt_draw(window_t* win) {
    devmgmt_state_t* state = (devmgmt_state_t*)win->app_data;
    if (!state) return;

    cpl_theme_t theme;
    cpl_theme_init(&theme);

    int32_t cx = win->x + 4;
    int32_t cy = win->y + 24;
    int32_t cw = win->width - 8;
    int32_t ch = win->height - 28;

    cpl_draw_dialog_bg(win, &theme, cx, cy, cw, ch);

    // 1. Header Banner
    gfx_fill_rect(cx + 8, cy + 8, cw - 16, 42, theme.input_bg);
    if (theme.is_classic) {
        gfx_draw_bevel_rect(cx + 8, cy + 8, cw - 16, 42, true);
    } else {
        gfx_draw_rounded_rect(cx + 8, cy + 8, cw - 16, 42, theme.control_radius, theme.card_border);
    }
    gfx_draw_icon_32(cx + 14, cy + 13, ICON_MAINTENANCE);
    gfx_draw_string(cx + 54, cy + 14, "Gestione dispositivi hardware (PnP)", theme.accent, 0, true);
    {
        char sub[80];
        mini_snprintf(sub, sizeof(sub), "Rilevati %d dispositivi reali su bus PCIe, ACPI, ATA/NVMe e USB", state->dev_count);
        gfx_draw_string(cx + 54, cy + 30, sub, theme.text_secondary, 0, true);
    }

    // 2. Colonna Sinistra: Albero Categorie & Dispositivi
    int32_t list_x = cx + 8;
    int32_t list_y = cy + 56;
    int32_t list_w = 248;
    int32_t list_h = ch - 96;

    gfx_fill_rect(list_x, list_y, list_w, list_h, theme.input_bg);
    if (theme.is_classic) {
        gfx_draw_bevel_rect(list_x, list_y, list_w, list_h, true);
    } else {
        gfx_draw_rounded_rect(list_x, list_y, list_w, list_h, theme.control_radius, theme.card_border);
    }

    int item_h = 20;
    int max_vis = (list_h - 8) / item_h;
    int total_rows = devmgmt_visible_count(state);
    if (state->scroll_row < 0) state->scroll_row = 0;
    if (state->scroll_row > total_rows - max_vis && total_rows > max_vis) {
        state->scroll_row = total_rows - max_vis;
    }

    for (int v = 0; v < max_vis; v++) {
        int r = state->scroll_row + v;
        int cat_idx = -1, dev_idx = -1;
        if (!devmgmt_get_row(state, r, &cat_idx, &dev_idx)) break;

        int32_t iy = list_y + 4 + v * item_h;
        bool is_sel = (r == state->selected_row);

        if (is_sel) {
            if (theme.is_classic) {
                gfx_fill_rect(list_x + 2, iy, list_w - 4, item_h, theme.sel_bg);
            } else {
                gfx_fill_rounded_rect(list_x + 2, iy, list_w - 4, item_h, 3, theme.sel_bg);
            }
        }

        uint32_t fg = is_sel ? theme.sel_text : theme.text_primary;
        if (dev_idx < 0) {
            const dm_category_t* cat = &state->cats[cat_idx];
            gfx_draw_string(list_x + 6, iy + 3, cat->expanded ? "v" : ">", fg, 0, true);
            gfx_draw_icon_16(list_x + 18, iy + 2, cat->icon);
            char lbl[34];
            strncpy(lbl, cat->title, 28);
            lbl[28] = '\0';
            gfx_draw_string(list_x + 38, iy + 3, lbl, fg, 0, true);
        } else {
            const dm_device_t* d = &state->devs[dev_idx];
            gfx_draw_icon_16(list_x + 22, iy + 2, d->icon);
            char lbl[30];
            strncpy(lbl, d->name, 25);
            lbl[25] = '\0';
            gfx_draw_string(list_x + 42, iy + 3, lbl, fg, 0, true);
        }
    }

    // 3. Colonna Destra: Proprieta Hardware
    int32_t det_x = list_x + list_w + 8;
    int32_t det_y = list_y;
    int32_t det_w = cw - list_w - 24;
    int32_t det_h = list_h;

    cpl_draw_groupbox(det_x, det_y, det_w, det_h, " Proprieta Dispositivo ", &theme);

    int sel_cat = -1, sel_dev = -1;
    if (devmgmt_get_row(state, state->selected_row, &sel_cat, &sel_dev) && sel_dev >= 0) {
        const dm_device_t* d = &state->devs[sel_dev];
        gfx_draw_icon_32(det_x + 14, det_y + 18, d->icon);
        char title_clip[42];
        strncpy(title_clip, d->name, 38);
        title_clip[38] = '\0';
        gfx_draw_string(det_x + 54, det_y + 18, title_clip, theme.accent, 0, true);
        gfx_draw_string(det_x + 54, det_y + 34, d->vendor, theme.text_secondary, 0, true);

        gfx_fill_rect(det_x + 14, det_y + 56, det_w - 28, 1, theme.card_border);

        int32_t py = det_y + 66;
        char buf[96];
        mini_snprintf(buf, sizeof(buf), "Categoria:   %s", state->cats[sel_cat].title);
        gfx_draw_string(det_x + 16, py, buf, theme.text_primary, 0, true);
        py += 20;

        mini_snprintf(buf, sizeof(buf), "Posizione:   %s", d->bus_loc);
        gfx_draw_string(det_x + 16, py, buf, theme.text_primary, 0, true);
        py += 20;

        mini_snprintf(buf, sizeof(buf), "Driver:      %s", d->driver_name);
        gfx_draw_string(det_x + 16, py, buf, theme.text_primary, 0, true);
        py += 24;

        cpl_draw_groupbox(det_x + 14, py, det_w - 28, 64, " Risorse & Identificativi ", &theme);
        gfx_draw_string(det_x + 24, py + 16, d->resources, theme.text_primary, 0, true);
        gfx_draw_string(det_x + 24, py + 36, d->extra, theme.text_secondary, 0, true);
        py += 76;

        gfx_draw_string(det_x + 16, py, "Stato:", theme.text_primary, 0, true);
        uint32_t st_col = d->has_driver ? (theme.is_dark ? 0x2ECC71 : 0x008000) : 0xE67E22;
        gfx_draw_string(det_x + 74, py, d->has_driver ? "Il dispositivo funziona correttamente." : "Driver nativo non disponibile (Fallback).", st_col, 0, true);
    } else if (sel_cat >= 0) {
        const dm_category_t* cat = &state->cats[sel_cat];
        gfx_draw_icon_32(det_x + 14, det_y + 18, cat->icon);
        gfx_draw_string(det_x + 54, det_y + 22, cat->title, theme.accent, 0, true);
        char msg[64];
        mini_snprintf(msg, sizeof(msg), "%d dispositivi rilevati in questa categoria.", cat->dev_count);
        gfx_draw_string(det_x + 16, det_y + 66, msg, theme.text_primary, 0, true);
        gfx_draw_string(det_x + 16, det_y + 88, "Clicca su una categoria per espandere/comprimere", theme.text_secondary, 0, true);
        gfx_draw_string(det_x + 16, det_y + 106, "e seleziona una periferica per i dettagli hardware.", theme.text_secondary, 0, true);
    }

    // 4. Barra Pulsanti Inferiori
    int32_t btn_y = cy + ch - 32;
    wm_draw_themed_button(cx + 8, btn_y, 76, 24, "Su", false);
    wm_draw_themed_button(cx + 90, btn_y, 76, 24, "Giu", false);
    wm_draw_themed_button(cx + 172, btn_y, 90, 24, "Ricarica", false);
    wm_draw_themed_button(cx + cw - 88, btn_y, 80, 24, "Chiudi", false);
}

static void devmgmt_mouse(window_t* win, int32_t lx, int32_t ly, bool clicked) {
    if (!clicked) return;
    devmgmt_state_t* state = (devmgmt_state_t*)win->app_data;
    if (!state) return;

    int32_t cx = 4;
    int32_t cy = 24;
    int32_t cw = win->width - 8;
    int32_t ch = win->height - 28;

    int32_t list_x = cx + 8;
    int32_t list_y = cy + 56;
    int32_t list_w = 248;
    int32_t list_h = ch - 96;

    if (lx >= list_x && lx <= list_x + list_w && ly >= list_y && ly <= list_y + list_h) {
        int row = state->scroll_row + (ly - list_y - 4) / 20;
        int cat_idx = -1, dev_idx = -1;
        if (devmgmt_get_row(state, row, &cat_idx, &dev_idx)) {
            state->selected_row = row;
            if (dev_idx < 0 && cat_idx >= 0) {
                state->cats[cat_idx].expanded = !state->cats[cat_idx].expanded;
            }
        }
        return;
    }

    int32_t btn_y = cy + ch - 32;
    if (ly >= btn_y && ly <= btn_y + 24) {
        if (lx >= cx + 8 && lx <= cx + 84) {
            if (state->scroll_row > 0) state->scroll_row -= 3;
            if (state->scroll_row < 0) state->scroll_row = 0;
            return;
        }
        if (lx >= cx + 90 && lx <= cx + 166) {
            state->scroll_row += 3;
            return;
        }
        if (lx >= cx + 172 && lx <= cx + 262) {
            devmgmt_scan_hardware(state);
            return;
        }
        if (lx >= cx + cw - 88 && lx <= cx + cw - 8) {
            wm_destroy_window(win->id);
            return;
        }
    }
}

void app_devmgmt_launch(void) {
    window_t* win = wm_create_window("Gestione dispositivi hardware", 80, 60, 620, 440, APP_DEVMGMT);
    if (!win) return;

    devmgmt_state_t* state = (devmgmt_state_t*)kmalloc(sizeof(devmgmt_state_t));
    if (state) {
        devmgmt_scan_hardware(state);
        win->app_data = state;
        win->on_draw = devmgmt_draw;
        win->on_mouse = devmgmt_mouse;
    }
}
