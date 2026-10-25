#include <drivers/driver_mgr.h>
#include <fs/vfs.h>
#include <mm/heap.h>
#include <string.h>
#include <arch/x86_64/io.h>

static driver_info_t g_drivers[MAX_DRIVERS];
static int           g_driver_count = 0;

static void serial_log(const char* str) {
    if (!str) return;
    while (*str) {
        outb(0x3F8, *str++);
    }
}

static bool ends_with_ci(const char* str, const char* suffix) {
    if (!str || !suffix) return false;
    size_t len_str = strlen(str);
    size_t len_suf = strlen(suffix);
    if (len_str < len_suf) return false;
    return (strcasecmp(str + len_str - len_suf, suffix) == 0);
}

static void trim(char* str) {
    if (!str) return;
    // Trim leading
    char* start = str;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') start++;
    if (start != str) {
        size_t len = strlen(start);
        memmove(str, start, len + 1);
    }
    // Trim trailing
    size_t len = strlen(str);
    while (len > 0 && (str[len - 1] == ' ' || str[len - 1] == '\t' || str[len - 1] == '\r' || str[len - 1] == '\n')) {
        str[--len] = '\0';
    }
}

static uint32_t parse_hex_or_dec(const char* val) {
    if (!val) return 0;
    while (*val == ' ' || *val == '\t') val++;
    if (val[0] == '0' && (val[1] == 'x' || val[1] == 'X')) {
        val += 2;
        uint32_t res = 0;
        while (*val) {
            char c = *val++;
            if (c >= '0' && c <= '9') res = (res << 4) | (c - '0');
            else if (c >= 'a' && c <= 'f') res = (res << 4) | (c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') res = (res << 4) | (c - 'A' + 10);
            else break;
        }
        return res;
    }
    uint32_t res = 0;
    while (*val >= '0' && *val <= '9') {
        res = res * 10 + (*val++ - '0');
    }
    return res;
}

static driver_type_t parse_driver_type(const char* type_str) {
    if (!type_str) return DRIVER_TYPE_UNKNOWN;
    if (strcasecmp(type_str, "Bus") == 0) return DRIVER_TYPE_BUS;
    if (strcasecmp(type_str, "Storage") == 0) return DRIVER_TYPE_STORAGE;
    if (strcasecmp(type_str, "Display") == 0) return DRIVER_TYPE_DISPLAY;
    if (strcasecmp(type_str, "Audio") == 0) return DRIVER_TYPE_AUDIO;
    if (strcasecmp(type_str, "Mouse") == 0) return DRIVER_TYPE_INPUT_MOUSE;
    if (strcasecmp(type_str, "Keyboard") == 0) return DRIVER_TYPE_INPUT_KEYBOARD;
    if (strcasecmp(type_str, "Timer") == 0) return DRIVER_TYPE_TIMER;
    if (strcasecmp(type_str, "Power") == 0) return DRIVER_TYPE_POWER;
    if (strcasecmp(type_str, "Network") == 0) return DRIVER_TYPE_NETWORK;
    return DRIVER_TYPE_UNKNOWN;
}

static void parse_ini_content(const char* ini_text, driver_info_t* drv) {
    if (!ini_text || !drv) return;

    char section[32] = {0};
    const char* p = ini_text;

    while (*p) {
        char line[128];
        size_t li = 0;
        while (*p && *p != '\n' && li < sizeof(line) - 1) {
            line[li++] = *p++;
        }
        line[li] = '\0';
        if (*p == '\n') p++;

        trim(line);
        if (line[0] == '\0' || line[0] == ';' || line[0] == '#') continue;

        // Sezione [Section]
        if (line[0] == '[') {
            char* end = strchr(line, ']');
            if (end) {
                *end = '\0';
                strncpy(section, line + 1, sizeof(section) - 1);
                trim(section);
            }
            continue;
        }

        // Chiave=Valore
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char* key = line;
        char* val = eq + 1;
        trim(key);
        trim(val);

        if (strcasecmp(section, "Driver") == 0) {
            if (strcasecmp(key, "Name") == 0) {
                strncpy(drv->name, val, sizeof(drv->name) - 1);
            } else if (strcasecmp(key, "Version") == 0) {
                strncpy(drv->version, val, sizeof(drv->version) - 1);
            } else if (strcasecmp(key, "Author") == 0) {
                strncpy(drv->author, val, sizeof(drv->author) - 1);
            } else if (strcasecmp(key, "Type") == 0) {
                strncpy(drv->type_str, val, sizeof(drv->type_str) - 1);
                drv->type = parse_driver_type(val);
            } else if (strcasecmp(key, "VendorID") == 0) {
                drv->vendor_id = parse_hex_or_dec(val);
            } else if (strcasecmp(key, "DeviceID") == 0) {
                drv->device_id = parse_hex_or_dec(val);
            } else if (strcasecmp(key, "File") == 0 || strcasecmp(key, "Filename") == 0) {
                strncpy(drv->filename, val, sizeof(drv->filename) - 1);
            } else if (strcasecmp(key, "AutoStart") == 0) {
                drv->autostart = (parse_hex_or_dec(val) != 0);
            }
        } else if (strcasecmp(section, "Hardware") == 0) {
            if (strcasecmp(key, "IRQ") == 0) {
                drv->irq = (uint8_t)parse_hex_or_dec(val);
            } else if (strcasecmp(key, "IO_Base") == 0) {
                drv->io_base = (uint16_t)parse_hex_or_dec(val);
            } else if (strcasecmp(key, "DMA") == 0) {
                drv->dma = (uint8_t)parse_hex_or_dec(val);
            }
        } else if (strcasecmp(section, "Settings") == 0) {
            if (drv->settings_summary[0] == '\0') {
                mini_snprintf(drv->settings_summary, sizeof(drv->settings_summary), "%s: %s", key, val);
            }
        }
    }
}

// Inizializzazione Fallback nel caso in cui il filesystem non sia ancora pronto
static void init_fallback_drivers(void) {
    const struct {
        const char* name;
        const char* fn;
        const char* ini;
        const char* type;
        driver_type_t t;
        uint8_t irq;
        uint16_t io;
        uint8_t dma;
        const char* settings;
    } defs[] = {
        { "NewSoft PCI Bus Enumerator", "PCI.SYS", "PCI.INI", "Bus", DRIVER_TYPE_BUS, 0, 0x0CF8, 0, "ScanMode: Fast, MaxBuses: 256" },
        { "NewSoft Serial ATA AHCI Controller", "AHCI.SYS", "AHCI.INI", "Storage", DRIVER_TYPE_STORAGE, 11, 0x0000, 1, "Mode: AHCI 1.0 DMA, Priority: Primary" },
        { "NewSoft IDE/ATA Storage Controller", "ATA.SYS", "ATA.INI", "Storage", DRIVER_TYPE_STORAGE, 14, 0x01F0, 0, "TransferMode: PIO4, Mode: Legacy Fallback" },
        { "NewSoft VESA/VBE Graphics Subsystem", "VGA.SYS", "VGA.INI", "Display", DRIVER_TYPE_DISPLAY, 0, 0x01CE, 0, "Resolution: 1024x768x32 BGA" },
        { "NewSoft AC97 Audio Subsystem", "AC97.SYS", "AC97.INI", "Audio", DRIVER_TYPE_AUDIO, 11, 0x1000, 1, "SampleRate: 48000Hz Stereo" },
        { "NewSoft PS/2 Mouse Driver", "PS2MOUSE.SYS", "PS2MOUSE.INI", "Mouse", DRIVER_TYPE_INPUT_MOUSE, 12, 0x0060, 0, "Sensitivity: 3, Accel: Normal" },
        { "NewSoft PS/2 Keyboard Driver", "PS2KBD.SYS", "PS2KBD.INI", "Keyboard", DRIVER_TYPE_INPUT_KEYBOARD, 1, 0x0060, 0, "Layout: IT, RepeatRate: 30" },
        { "NewSoft Real-Time Clock & CMOS", "RTC.SYS", "RTC.INI", "Timer", DRIVER_TYPE_TIMER, 8, 0x0070, 0, "Format: 24h, TimeZone: Europe/Rome" },
        { "NewSoft Power Management Subsystem", "POWER.SYS", "POWER.INI", "Power", DRIVER_TYPE_POWER, 9, 0x0604, 0, "APM: Enabled, ACPI: Enabled" },
        { "Intel PRO/1000 Gigabit Adapter", "E1000.SYS", "E1000.INI", "Network", DRIVER_TYPE_NETWORK, 11, 0x0000, 0, "Link: 1000Mbps Full Duplex" },
        { "Realtek RTL8139 Fast Ethernet", "RTL8139.SYS", "RTL8139.INI", "Network", DRIVER_TYPE_NETWORK, 10, 0xC000, 0, "Link: 100Mbps Full Duplex" },
        { "Wi-Fi 7 / 6E Ultra-Gigabit Controller", "WIFI7.SYS", "WIFI7.INI", "Network", DRIVER_TYPE_NETWORK, 11, 0xE000, 0, "Throughput: Fino a 5.0 GB/s (Vincolo HW reale)" },
        { "USB 3.x/USB4 xHCI Host Controller", "USBXHCI.SYS", "USBXHCI.INI", "Bus", DRIVER_TYPE_BUS, 11, 0x0000, 0, "Specs: xHCI 1.2, SuperSpeed+ (40 Gbps)" },
        { "USB 1.1 UHCI Host Controller", "USBUHCI.SYS", "USBUHCI.INI", "Bus", DRIVER_TYPE_BUS, 11, 0xD000, 0, "Specs: UHCI 1.1, Low/Full Speed (12 Mbps)" },
        { "USB 1.1 OHCI Host Controller", "USBOHCI.SYS", "USBOHCI.INI", "Bus", DRIVER_TYPE_BUS, 10, 0x0000, 0, "Specs: OpenHCI 1.0/1.1, MMIO Root Hub" },
        { "USB Generic HID Foundation", "USBHID.SYS", "USBHID.INI", "Keyboard", DRIVER_TYPE_INPUT_KEYBOARD, 0, 0x0000, 0, "Devices: Boot Keyboard & Mouse Foundation" },
        { "USB Generic Mass Storage BOT", "USBMSC.SYS", "USBMSC.INI", "Storage", DRIVER_TYPE_STORAGE, 0, 0x0000, 0, "Protocol: Bulk-Only Transport, SCSI" },
        { "USB Generic Hub Foundation", "USBHUB.SYS", "USBHUB.INI", "Bus", DRIVER_TYPE_BUS, 0, 0x0000, 0, "Specs: USB 1.1 4-Port Hub Engine" },
        { "USB Wireless & Ethernet Dongle", "USBNET.SYS", "USBNET.INI", "Network", DRIVER_TYPE_NETWORK, 11, 0x0000, 0, "Specs: USB 3.x / USB4 WLAN Bridge" },
        { "Ethernet II & ARP Framing Engine", "ETH.SYS.NET", "ETH.INI", "Network", DRIVER_TYPE_NETWORK, 0, 0, 0, "MTU: 1500, Frame: DIX II, ARP: RFC 826" },
        { "IPv4 & ICMP Network Layer", "IP.SYS.NET", "IP.INI", "Network", DRIVER_TYPE_NETWORK, 0, 0, 0, "Version: 4, Checksum: HW/SW" },
        { "UDP Transport Protocol Subsystem", "UDP.SYS.NET", "UDP.INI", "Network", DRIVER_TYPE_NETWORK, 0, 0, 0, "Sockets: Dynamic, Checksum: RFC 768" },
        { "TCP Reliable Stream Engine", "TCP.SYS.NET", "TCP.INI", "Network", DRIVER_TYPE_NETWORK, 0, 0, 0, "Window: 4096, Handshake: 3-Way" },
        { "Dynamic Host Configuration Client", "DHCP.SYS.NET", "DHCP.INI", "Network", DRIVER_TYPE_NETWORK, 0, 0, 0, "ClientPort: 68, ServerPort: 67" },
        { "TLS Security & Crypto Substrate", "TLS.SYS.NET", "TLS.INI", "Network", DRIVER_TYPE_NETWORK, 0, 0, 0, "Cipher: SHA256/HMAC, TLS: 1.2/1.3" }
    };

    g_driver_count = 0;
    for (size_t i = 0; i < sizeof(defs) / sizeof(defs[0]) && g_driver_count < MAX_DRIVERS; i++) {
        driver_info_t* d = &g_drivers[g_driver_count++];
        memset(d, 0, sizeof(driver_info_t));
        strncpy(d->name, defs[i].name, sizeof(d->name) - 1);
        strncpy(d->filename, defs[i].fn, sizeof(d->filename) - 1);
        strncpy(d->ininame, defs[i].ini, sizeof(d->ininame) - 1);
        strcpy(d->version, "1.0.0");
        strcpy(d->author, "Newspoint Software - NewSoft");
        strncpy(d->type_str, defs[i].type, sizeof(d->type_str) - 1);
        d->type = defs[i].t;
        d->irq = defs[i].irq;
        d->io_base = defs[i].io;
        d->dma = defs[i].dma;
        d->autostart = true;
        d->status = DRIVER_STATUS_ACTIVE;
        d->sys_size = 1024;
        strncpy(d->settings_summary, defs[i].settings, sizeof(d->settings_summary) - 1);
    }
}

void driver_mgr_init(void) {
    serial_log("[DRIVER_MGR] Inizializzazione Sottosistema Modulare Driver...\n");
    g_driver_count = 0;

    vfs_dirent_t dirents[32];
    const char* driver_dir = "C:\\NewOS\\Driver";
    int count = vfs_readdir(driver_dir, dirents, 32);
    if (count <= 0) {
        driver_dir = "C:\\SYSTEM\\DRIVERS";
        count = vfs_readdir(driver_dir, dirents, 32);
    }

    if (count <= 0) {
        serial_log("[DRIVER_MGR] Nessun driver trovato su disco. Caricamento driver interni di fallback...\n");
        init_fallback_drivers();
    } else {
        for (int i = 0; i < count && g_driver_count < MAX_DRIVERS; i++) {
            if (dirents[i].is_directory) continue;

            if (ends_with_ci(dirents[i].name, ".INI")) {
                char ini_path[VFS_MAX_PATH];
                mini_snprintf(ini_path, sizeof(ini_path), "%s\\%s", driver_dir, dirents[i].name);

                uint32_t fsize = 0;
                char* ini_content = (char*)vfs_read_entire_file(ini_path, &fsize);
                if (ini_content) {
                    driver_info_t* drv = &g_drivers[g_driver_count];
                    memset(drv, 0, sizeof(driver_info_t));
                    strncpy(drv->ininame, dirents[i].name, sizeof(drv->ininame) - 1);

                    // Nome file .SYS corrispondente
                    strncpy(drv->filename, dirents[i].name, sizeof(drv->filename) - 1);
                    size_t flen = strlen(drv->filename);
                    if (flen >= 4) {
                        drv->filename[flen - 4] = '.';
                        drv->filename[flen - 3] = 'S';
                        drv->filename[flen - 2] = 'Y';
                        drv->filename[flen - 1] = 'S';
                    }

                    // Verifica se il file .SYS esiste fisicamente su disco
                    char sys_path[VFS_MAX_PATH];
                    mini_snprintf(sys_path, sizeof(sys_path), "%s\\%s", driver_dir, drv->filename);
                    if (vfs_exists(sys_path)) {
                        drv->sys_size = vfs_file_size(sys_path);
                    } else {
                        drv->sys_size = 1024; // Fallback resident
                    }

                    parse_ini_content(ini_content, drv);
                    kfree(ini_content);

                    if (drv->autostart) {
                        drv->status = DRIVER_STATUS_ACTIVE;
                    } else {
                        drv->status = DRIVER_STATUS_LOADED;
                    }

                    g_driver_count++;
                }
            }
        }

        // Se non abbiamo trovato .INI su disco, usiamo i predefiniti
        if (g_driver_count == 0) {
            init_fallback_drivers();
        }
    }

    // Stampa riepilogo su seriale
    char buf[128];
    mini_snprintf(buf, sizeof(buf), "[DRIVER_MGR] %d Driver Registrati e Attivi con successo:\n", g_driver_count);
    serial_log(buf);

    for (int i = 0; i < g_driver_count; i++) {
        const driver_info_t* d = &g_drivers[i];
        mini_snprintf(buf, sizeof(buf), "  -> [%s] %s (v%s) IRQ:%d IO:0x%x [%s]\n",
                      d->filename, d->name, d->version, (int)d->irq, (unsigned int)d->io_base,
                      driver_mgr_status_to_str(d->status));
        serial_log(buf);
    }
}

int driver_mgr_get_count(void) {
    return g_driver_count;
}

const driver_info_t* driver_mgr_get_driver(int index) {
    if (index >= 0 && index < g_driver_count) {
        return &g_drivers[index];
    }
    return NULL;
}

const driver_info_t* driver_mgr_find(const char* name) {
    if (!name) return NULL;
    for (int i = 0; i < g_driver_count; i++) {
        if (strcasecmp(g_drivers[i].name, name) == 0 ||
            strcasecmp(g_drivers[i].filename, name) == 0 ||
            strcasecmp(g_drivers[i].ininame, name) == 0) {
            return &g_drivers[i];
        }
    }
    return NULL;
}

bool driver_mgr_set_status(int index, driver_status_t status) {
    if (index >= 0 && index < g_driver_count) {
        g_drivers[index].status = status;
        return true;
    }
    return false;
}

bool driver_mgr_reload(const char* name) {
    const driver_info_t* cur = driver_mgr_find(name);
    if (!cur) return false;

    // Rilettura del file .INI
    char ini_path[VFS_MAX_PATH];
    mini_snprintf(ini_path, sizeof(ini_path), "C:\\NewOS\\Driver\\%s", cur->ininame);
    uint32_t fsize = 0;
    char* ini_content = (char*)vfs_read_entire_file(ini_path, &fsize);
    if (!ini_content) {
        mini_snprintf(ini_path, sizeof(ini_path), "C:\\SYSTEM\\DRIVERS\\%s", cur->ininame);
        ini_content = (char*)vfs_read_entire_file(ini_path, &fsize);
    }
    if (ini_content) {
        int idx = (int)(cur - g_drivers);
        parse_ini_content(ini_content, &g_drivers[idx]);
        kfree(ini_content);
        g_drivers[idx].status = DRIVER_STATUS_ACTIVE;
        serial_log("[DRIVER_MGR] Driver ricaricato con successo: ");
        serial_log(name);
        serial_log("\n");
        return true;
    }
    return false;
}

const char* driver_mgr_status_to_str(driver_status_t status) {
    switch (status) {
        case DRIVER_STATUS_ACTIVE:    return "Attivo";
        case DRIVER_STATUS_LOADED:    return "Caricato";
        case DRIVER_STATUS_SUSPENDED: return "Sospeso";
        case DRIVER_STATUS_ERROR:     return "Errore";
        default:                      return "Non caricato";
    }
}
