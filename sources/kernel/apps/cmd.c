#include <gui/wm.h>
#include <gui/graphics.h>
#include <mm/heap.h>
#include <string.h>
#include <arch/x86_64/io.h>
#include <drivers/power.h>
#include <apps/power_props.h>
#include <drivers/driver_mgr.h>
#include <drivers/vga.h>
#include <apps/devmgmt.h>
#include <apps/regedit.h>
#include <drivers/timer.h>
#include <net/socket.h>
#include <drivers/net/net_nic.h>
#include <net/dhcp.h>
#include <net/tcp.h>
#include <net/icmp.h>
#include <apps/explorer.h>
#include <apps/oobe.h>
#include <apps/rules_viewer.h>
#include <fs/vfs.h>
#include <fs/vfs_mount.h>
#include <drivers/net/wifi.h>
#include <drivers/usb/usb.h>
#include <drivers/usb/xhci.h>
#include <drivers/usb/uhci.h>
#include <drivers/usb/ohci.h>
#include <drivers/usb/usb_hid.h>
#include <drivers/usb/usb_msc.h>
#include <drivers/usb/usb_hub.h>
#include <apps/autoplay.h>

#define CMD_COLS 56
#define CMD_ROWS 18

typedef struct {
    char lines[CMD_ROWS][CMD_COLS];
    char input_line[CMD_COLS];
    int input_pos;
    int current_row;
} cmd_state_t;

static void cmd_scroll(cmd_state_t* state) {
    for (int r = 0; r < CMD_ROWS - 1; r++) {
        memcpy(state->lines[r], state->lines[r + 1], CMD_COLS);
    }
    memset(state->lines[CMD_ROWS - 1], 0, CMD_COLS);
}

static void cmd_print(cmd_state_t* state, const char* str) {
    if (state->current_row >= CMD_ROWS) {
        cmd_scroll(state);
        state->current_row = CMD_ROWS - 1;
    }
    strncpy(state->lines[state->current_row], str, CMD_COLS - 1);
    state->current_row++;
}

static cmd_state_t* g_cmd_ping_state = NULL;
static const char*  g_cmd_ping_target = NULL;

static void on_ping_reply_callback(int seq, uint32_t rtt_ms, uint8_t ttl) {
    (void)seq;
    if (!g_cmd_ping_state) return;
    char rep[CMD_COLS];
    if (rtt_ms != 0xFFFFFFFF) {
        mini_snprintf(rep, sizeof(rep), "Risposta da %s: byte=32 tempo=%ums TTL=%u",
                      g_cmd_ping_target ? g_cmd_ping_target : "host",
                      (unsigned int)rtt_ms, (unsigned int)ttl);
    } else {
        mini_snprintf(rep, sizeof(rep), "Richiesta scaduta.");
    }
    cmd_print(g_cmd_ping_state, rep);
}

static void cmd_execute(cmd_state_t* state, const char* cmd) {
    char prompt_echo[CMD_COLS + 8];
    mini_snprintf(prompt_echo, sizeof(prompt_echo), "C:\\> %s", cmd);
    cmd_print(state, prompt_echo);

    if (strcmp(cmd, "ver") == 0) {
        cmd_print(state, "NewOS V1 64-bit [Versione 1.0.2026.x86_64]");
        cmd_print(state, "(C) Copyright @ Newspoint Software - NewSoft - NewOS V1");
    } else if (strcmp(cmd, "help") == 0) {
        cmd_print(state, "Comandi disponibili:");
        cmd_print(state, "  VER       Mostra la versione del kernel 64-bit");
        cmd_print(state, "  MEM       Mostra statistiche memoria RAM ed E820");
        cmd_print(state, "  POWER     Stato e opzioni di risparmio energia");
        cmd_print(state, "  STANDBY   Attiva subito lo standby dello schermo");
        cmd_print(state, "  SHUTDOWN  Arresta il sistema via ACPI");
        cmd_print(state, "  REBOOT    Riavvia il sistema operativo");
        cmd_print(state, "  DRIVERQUERY Elenco driver caricati (.SYS + .INI)");
        cmd_print(state, "  DEVMGMT   Apre Gestione periferiche");
        cmd_print(state, "  REGEDIT   Apre l'Editor del Registro di Sistema");
        cmd_print(state, "  RES       Visualizza o imposta la risoluzione video");
        cmd_print(state, "  IPCONFIG  Visualizza configurazione di rete e IP");
        cmd_print(state, "  PING      Invia pacchetti ICMP Echo a un host");
        cmd_print(state, "  NETSTAT   Visualizza connessioni e porte TCP/UDP");
        cmd_print(state, "  DIR       Elenca i file e le cartelle (es. DIR o DIR R:)");
        cmd_print(state, "  DRIVES    Elenca le unita' montate (C:, D:, R:, U:)");
        cmd_print(state, "  NTFS      Stato e collaudo file system NTFS-3G (R/W)");
        cmd_print(state, "  USB       Elenco periferiche USB connesse (xHCI)");
        cmd_print(state, "  WIFI      Stato Wi-Fi, scansione e negoziazione velocita'");
        cmd_print(state, "  DHCP      Rinnova leasing IP con router");
        cmd_print(state, "  CLEAR     Pulisce la schermata del terminale");
        cmd_print(state, "  OOBE      Avvia Configurazione Guidata Iniziale");
        cmd_print(state, "  RULES     Visualizzatore Ufficiale Regole (.NOR)");
        cmd_print(state, "  BROWSER   Avvia il Web Browser (es. BROWSER [url])");
        cmd_print(state, "  TYPE      Visualizza file (solo testo chiaro)");
        cmd_print(state, "  ECHO      Stampa un messaggio a video");
        cmd_print(state, "  AUTOPLAY  Finestra dinamica USB/CD/DVD/BD (Casi 1-4)");
        cmd_print(state, "  HELP      Visualizza questa guida");
    } else if (strncmp(cmd, "wifi", 4) == 0 && (cmd[4] == '\0' || cmd[4] == ' ')) {
        const char* sub = cmd + 4;
        while (*sub == ' ') sub++;

        if (strcmp(sub, "scan") == 0) {
            cmd_print(state, "Scansione reti radio (6GHz, 5GHz, 2.4GHz)...");
            wifi_ap_t aps[8];
            int acnt = wifi_scan(aps, 8);
            cmd_print(state, "SSID                 Banda   Canale Velocita' Segnale");
            cmd_print(state, "--------------------------------------------------");
            for (int i = 0; i < acnt; i++) {
                char sline[CMD_COLS];
                const char* bstr = (aps[i].band == WIFI_BAND_6GHZ) ? "6G" : ((aps[i].band == WIFI_BAND_5GHZ) ? "5G" : "2G");
                mini_snprintf(sline, sizeof(sline), "%-20s %-7s %-6u %4uMB/s %d%%",
                              aps[i].ssid, bstr, (unsigned int)aps[i].channel,
                              (unsigned int)aps[i].max_rate_mbps, (int)aps[i].signal_percent);
                cmd_print(state, sline);
            }
        } else if (strncmp(sub, "connect ", 8) == 0) {
            const char* ssid = sub + 8;
            while (*ssid == ' ') ssid++;
            char cmsg[CMD_COLS];
            mini_snprintf(cmsg, sizeof(cmsg), "Connessione a '%s'...", ssid);
            cmd_print(state, cmsg);
            if (wifi_connect(ssid, "")) {
                wifi_info_t wi;
                wifi_get_info(&wi);
                cmd_print(state, "Connesso con successo!");
                char rmsg[CMD_COLS];
                mini_snprintf(rmsg, sizeof(rmsg), "Velocita' negoziata: %u MB/s (Cap HW: %u MB/s)",
                              (unsigned int)wi.link_speed_mbps, (unsigned int)wi.hw_max_rate_mbps);
                cmd_print(state, rmsg);
            } else {
                cmd_print(state, "Errore durante la connessione.");
            }
        } else if (strncmp(sub, "cap ", 4) == 0) {
            const char* cap_str = sub + 4;
            while (*cap_str == ' ') cap_str++;
            uint32_t cap_val = 0;
            while (*cap_str >= '0' && *cap_str <= '9') {
                cap_val = cap_val * 10 + (*cap_str++ - '0');
            }
            if (cap_val > 0) {
                wifi_set_hardware_cap(cap_val, (cap_val <= 1000) ? "802.11ac (Wi-Fi 5)" : "802.11be (Wi-Fi 7)");
                char cap_msg[CMD_COLS];
                mini_snprintf(cap_msg, sizeof(cap_msg), "Capacita' hardware impostata a: %u MB/s", (unsigned int)cap_val);
                cmd_print(state, cap_msg);
                wifi_info_t wi;
                wifi_get_info(&wi);
                mini_snprintf(cap_msg, sizeof(cap_msg), "Velocita' negoziata attuale: %u MB/s", (unsigned int)wi.link_speed_mbps);
                cmd_print(state, cap_msg);
            } else {
                cmd_print(state, "Uso: wifi cap <MBps> (es. wifi cap 1000 o wifi cap 5000)");
            }
        } else if (strcmp(sub, "disconnect") == 0) {
            wifi_disconnect();
            cmd_print(state, "Disconnesso dalla rete wireless.");
        } else {
            wifi_info_t wi;
            if (wifi_get_info(&wi)) {
                cmd_print(state, "--- Stato Controller Wi-Fi ---");
                char wline[CMD_COLS];
                mini_snprintf(wline, sizeof(wline), "Scheda: %s", wi.adapter_name);
                cmd_print(state, wline);
                mini_snprintf(wline, sizeof(wline), "Bus HW: %s", wifi_bus_to_str(wi.bus_type));
                cmd_print(state, wline);
                mini_snprintf(wline, sizeof(wline), "Standard: %s", wi.hw_standard);
                cmd_print(state, wline);
                mini_snprintf(wline, sizeof(wline), "Capacita' HW Massima: %u MB/s (Tetto Hardware)", (unsigned int)wi.hw_max_rate_mbps);
                cmd_print(state, wline);
                mini_snprintf(wline, sizeof(wline), "Stato: %s | Rete: %s",
                              (wi.state == WIFI_STATE_CONNECTED) ? "Connesso" : "Disconnesso",
                              wi.connected_ssid[0] ? wi.connected_ssid : "Nessuna");
                cmd_print(state, wline);
                if (wi.state == WIFI_STATE_CONNECTED) {
                    mini_snprintf(wline, sizeof(wline), "Velocita' Negoziata: %u MB/s (min AP/HW)", (unsigned int)wi.link_speed_mbps);
                    cmd_print(state, wline);
                    mini_snprintf(wline, sizeof(wline), "Banda: %s (Ch %u)", wifi_band_to_str(wi.band), (unsigned int)wi.channel);
                    cmd_print(state, wline);
                    mini_snprintf(wline, sizeof(wline), "Segnale: %d dBm (%u%%)", (int)wi.rssi_dbm, (unsigned int)wi.signal_percent);
                    cmd_print(state, wline);
                }
                cmd_print(state, "Comandi: WIFI SCAN, WIFI CONNECT <SSID>, WIFI CAP <MB/s>");
            } else {
                cmd_print(state, "Controller Wi-Fi non inizializzato.");
            }
        }
    } else if (strcmp(cmd, "drives") == 0) {
        cmd_print(state, "Unita' montate nel sistema:");
        char drv_letters[8];
        int dcount = vfs_list_drives(drv_letters, 8);
        for (int i = 0; i < dcount; i++) {
            vfs_drive_info_t dinfo;
            if (vfs_get_drive_info(drv_letters[i], &dinfo)) {
                char dline[CMD_COLS];
                mini_snprintf(dline, sizeof(dline), "  %c: [%-7s] %s (%u MB)",
                              dinfo.drive_letter, dinfo.fs_name, dinfo.label, (unsigned int)dinfo.total_mb);
                cmd_print(state, dline);
            }
        }
    } else if (strcmp(cmd, "dir") == 0 || strncmp(cmd, "dir ", 4) == 0) {
        const char* target = (cmd[3] == ' ') ? (cmd + 4) : "C:\\";
        while (*target == ' ') target++;
        if (*target == '\0') target = "C:\\";

        char header[CMD_COLS];
        mini_snprintf(header, sizeof(header), "Contenuto di %s:", target);
        cmd_print(state, header);

        vfs_dirent_t entries[32];
        int count = vfs_readdir(target, entries, 32);
        if (count == 0) {
            cmd_print(state, "  (Nessun elemento o cartella vuota)");
        } else {
            for (int i = 0; i < count; i++) {
                char entry_line[CMD_COLS];
                if (entries[i].is_directory) {
                    mini_snprintf(entry_line, sizeof(entry_line), "  <DIR>          %s", entries[i].name);
                } else {
                    mini_snprintf(entry_line, sizeof(entry_line), "  %7u B      %s",
                                  (unsigned int)entries[i].size, entries[i].name);
                }
                cmd_print(state, entry_line);
            }
        }
    } else if (strcmp(cmd, "driverquery") == 0 || strcmp(cmd, "drivers") == 0) {
        cmd_print(state, "Driver (.INI)  Modulo    Tipo     IRQ IO     Stato");
        cmd_print(state, "--------------------------------------------------");
        int dcount = driver_mgr_get_count();
        for (int i = 0; i < dcount; i++) {
            const driver_info_t* d = driver_mgr_get_driver(i);
            if (!d) continue;
            char line[CMD_COLS];
            mini_snprintf(line, sizeof(line), "%-14s %-9s %-8s %-3d 0x%04X %s",
                          d->ininame, d->filename, d->type_str, (int)d->irq, (unsigned int)d->io_base,
                          driver_mgr_status_to_str(d->status));
            cmd_print(state, line);
        }
    } else if (strcmp(cmd, "usb") == 0 || strcmp(cmd, "usbview") == 0) {
        cmd_print(state, "--- Sottosistema USB (Host Controllers & Foundation) ---");
        char hline[CMD_COLS];
        if (xhci_is_active()) {
            const xhci_controller_t* x = xhci_get_controller();
            mini_snprintf(hline, sizeof(hline), "xHCI:  %s", x->controller_name);
            cmd_print(state, hline);
        }
        if (uhci_is_active()) {
            const uhci_controller_t* u = uhci_get_controller();
            mini_snprintf(hline, sizeof(hline), "UHCI:  %s (IO 0x%04X, IRQ %d)", u->controller_name, (unsigned int)u->io_base, (int)u->irq);
            cmd_print(state, hline);
        }
        if (ohci_is_active()) {
            const ohci_controller_t* o = ohci_get_controller();
            mini_snprintf(hline, sizeof(hline), "OHCI:  %s (MMIO 0x%08X)", o->controller_name, (unsigned int)o->mmio_base);
            cmd_print(state, hline);
        }
        cmd_print(state, "--- Periferiche USB Rilevate ---");
        int ucnt = usb_get_device_count();
        if (ucnt == 0) {
            cmd_print(state, "Nessuna periferica USB attiva sul bus.");
        } else {
            for (int i = 0; i < ucnt; i++) {
                usb_device_t* u = usb_get_device(i);
                if (!u || !u->is_connected) continue;
                char uline[CMD_COLS];
                mini_snprintf(uline, sizeof(uline), "Porta %d: [%04X:%04X] %s",
                              (int)u->port_id, (unsigned int)u->vendor_id, (unsigned int)u->device_id, u->product);
                cmd_print(state, uline);
                mini_snprintf(uline, sizeof(uline), "  Bus:       %s", usb_controller_type_to_str(u->controller_type));
                cmd_print(state, uline);
                mini_snprintf(uline, sizeof(uline), "  Velocita': %s (%u MB/s)",
                              usb_speed_to_str(u->speed), (unsigned int)u->max_bandwidth_mbps);
                cmd_print(state, uline);
                mini_snprintf(uline, sizeof(uline), "  Classe:    %s", usb_class_to_str(u->class_code));
                cmd_print(state, uline);
            }
        }
    } else if (strcmp(cmd, "devmgmt") == 0) {
        cmd_print(state, "Apertura Gestione Periferiche...");
        app_devmgmt_launch();
    } else if (strcmp(cmd, "regedit") == 0 || strcmp(cmd, "reg") == 0) {
        cmd_print(state, "Apertura Editor del Registro di Sistema...");
        app_regedit_launch();
    } else if (strcmp(cmd, "explorer") == 0 || strcmp(cmd, "risorse") == 0) {
        cmd_print(state, "Apertura Risorse del computer...");
        app_explorer_open_this_pc();
    } else if (strcmp(cmd, "explorer c:") == 0 || strcmp(cmd, "explorer c:\\") == 0) {
        cmd_print(state, "Apertura Disco locale (C:)...");
        app_explorer_open_drive_c();
    } else if (strcmp(cmd, "explorer u:") == 0 || strcmp(cmd, "explorer u:\\") == 0 || strcmp(cmd, "explorer u") == 0) {
        cmd_print(state, "Apertura Disco rimovibile USB (U:)...");
        app_explorer_open_drive_u();
    } else if (strcmp(cmd, "explorer d:") == 0 || strcmp(cmd, "explorer d:\\") == 0 || strcmp(cmd, "explorer d") == 0) {
        cmd_print(state, "Apertura Unita ottica (D:)...");
        app_explorer_open_drive_d();
    } else if (strncmp(cmd, "autoplay", 8) == 0 && (cmd[8] == '\0' || cmd[8] == ' ')) {
        const char* sub = cmd + 8;
        while (*sub == ' ') sub++;
        if (strcmp(sub, "1") == 0 || strcmp(sub, "massstorage") == 0 || strcmp(sub, "msc") == 0) {
            cmd_print(state, "AutoPlay: Avvio Caso 1 (MassStorage)...");
            app_autoplay_launch_mode('U', AUTOPLAY_MODE_MASS_STORAGE);
        } else if (strcmp(sub, "2") == 0 || strcmp(sub, "multimedia") == 0 || strcmp(sub, "media") == 0) {
            cmd_print(state, "AutoPlay: Avvio Caso 2 (Multimedia Player)...");
            app_autoplay_launch_mode('D', AUTOPLAY_MODE_MULTIMEDIA);
        } else if (strcmp(sub, "3") == 0 || strcmp(sub, "photo") == 0 || strcmp(sub, "gallery") == 0) {
            cmd_print(state, "AutoPlay: Avvio Caso 3 (PhotoManager Galleria)...");
            app_autoplay_launch_mode('U', AUTOPLAY_MODE_PHOTO);
        } else if (strcmp(sub, "4") == 0 || strcmp(sub, "standard") == 0 || strcmp(sub, "dialog") == 0) {
            cmd_print(state, "AutoPlay: Avvio Caso 4 (Standard AutoPlay Dialog)...");
            app_autoplay_launch_mode('U', AUTOPLAY_MODE_STANDARD);
        } else if (strcmp(sub, "d:") == 0 || strcmp(sub, "d") == 0) {
            cmd_print(state, "Rilevamento contenuti su CD/DVD (D:)...");
            app_autoplay_launch('D');
        } else {
            cmd_print(state, "Rilevamento dinamico contenuti periferica (U:)...");
            app_autoplay_launch('U');
        }
    } else if (strncmp(cmd, "ntfs", 4) == 0 && (cmd[4] == '\0' || cmd[4] == ' ')) {
        const char* sub = cmd + 4;
        while (*sub == ' ') sub++;
        if (strcmp(sub, "write") == 0 || strcmp(sub, "test") == 0) {
            cmd_print(state, "Collaudo Scrittura NTFS-3G su U:...");
            vfs_mount_drive('U', "NTFS", "Disco USB (NTFS)", 2, 0, false, NULL);
            const char* sample = "File scritto dal motore NTFS-3G su NewOS V1!\n";
            int written = vfs_write_entire_file_inplace("U:\\TEST_NTFS3G.TXT", sample, (uint32_t)strlen(sample));
            if (written > 0) {
                char msg[CMD_COLS];
                mini_snprintf(msg, sizeof(msg), "Scrittura OK: %d byte scritti in U:\\TEST_NTFS3G.TXT", written);
                cmd_print(state, msg);
            } else {
                cmd_print(state, "Errore durante la scrittura su partizione NTFS.");
            }
        } else {
            cmd_print(state, "--- Modulo Driver File System NTFS-3G ---");
            cmd_print(state, "Stato:        Attivo e Registrato nel VFS");
            cmd_print(state, "Operativita': Lettura e Scrittura Completa (R/W)");
            cmd_print(state, "Architettura: MFT Master File Table ($MFT)");
            cmd_print(state, "Attributi:    $STANDARD_INFO, $FILE_NAME, $DATA");
            cmd_print(state, "Data Runs:    Resident & Non-Resident supportati");
            cmd_print(state, "Comando:      NTFS WRITE (collaudo scrittura)");
        }
    } else if (strcmp(cmd, "res") == 0 || strcmp(cmd, "resolution") == 0) {
        char cur_res[64];
        mini_snprintf(cur_res, sizeof(cur_res), "Risoluzione Attuale: %dx%d @ 32bpp (%s)",
                      gfx_get_width(), gfx_get_height(),
                      vga_is_bga_supported() ? "BGA Attivo" : "VESA Legacy");
        cmd_print(state, cur_res);
        cmd_print(state, "Risoluzioni disponibili (usa: res <id> o res <LxA>):");
        int mcnt = vga_get_mode_count();
        for (int i = 0; i < mcnt; i++) {
            const vga_mode_info_t* m = vga_get_mode_info(i);
            if (!m) continue;
            char mline[64];
            mini_snprintf(mline, sizeof(mline), "  [%d] %-10s  %s", i, m->label, m->aspect_ratio);
            cmd_print(state, mline);
        }
    } else if (strncmp(cmd, "res ", 4) == 0 || strncmp(cmd, "resolution ", 11) == 0) {
        const char* arg = (cmd[3] == ' ') ? (cmd + 4) : (cmd + 11);
        while (*arg == ' ') arg++;
        bool ok = false;
        if (arg[0] >= '0' && arg[0] <= '5' && arg[1] == '\0') {
            int idx = arg[0] - '0';
            ok = vga_set_mode_index(idx);
        } else if (strcmp(arg, "800x600") == 0) {
            ok = vga_set_resolution(800, 600);
        } else if (strcmp(arg, "1024x768") == 0) {
            ok = vga_set_resolution(1024, 768);
        } else if (strcmp(arg, "1280x720") == 0) {
            ok = vga_set_resolution(1280, 720);
        } else if (strcmp(arg, "1280x1024") == 0) {
            ok = vga_set_resolution(1280, 1024);
        } else if (strcmp(arg, "1440x900") == 0) {
            ok = vga_set_resolution(1440, 900);
        } else if (strcmp(arg, "1920x1080") == 0) {
            ok = vga_set_resolution(1920, 1080);
        }

        if (ok) {
            char ok_msg[64];
            mini_snprintf(ok_msg, sizeof(ok_msg), "Risoluzione impostata: %dx%d @ 32bpp",
                          gfx_get_width(), gfx_get_height());
            cmd_print(state, ok_msg);
        } else {
            cmd_print(state, "Errore: modalita' non supportata o BGA non disponibile.");
        }
    } else if (strcmp(cmd, "ipconfig") == 0 || strcmp(cmd, "ifconfig") == 0) {
        net_nic_t* nic = net_nic_get_active();
        if (!nic) {
            cmd_print(state, "Nessuna scheda di rete rilevata.");
        } else {
            char buf[CMD_COLS];
            cmd_print(state, "Configurazione IP di NewOS V1:");
            cmd_print(state, "");
            mini_snprintf(buf, sizeof(buf), "Scheda: %s", nic->name);
            cmd_print(state, buf);
            char mac_s[24], ip_s[20], mask_s[20], gw_s[20], dns_s[20];
            net_format_mac(nic->mac, mac_s, sizeof(mac_s));
            net_format_ip(nic->ip, ip_s, sizeof(ip_s));
            net_format_ip(nic->netmask, mask_s, sizeof(mask_s));
            net_format_ip(nic->gateway, gw_s, sizeof(gw_s));
            net_format_ip(nic->dns, dns_s, sizeof(dns_s));
            mini_snprintf(buf, sizeof(buf), "  Indirizzo MAC . . . . : %s", mac_s);
            cmd_print(state, buf);
            mini_snprintf(buf, sizeof(buf), "  Stato link  . . . . . : %s", nic->link_up ? "Connesso" : "Disconnesso");
            cmd_print(state, buf);
            mini_snprintf(buf, sizeof(buf), "  Stato DHCP  . . . . . : %s", dhcp_state_to_str(dhcp_get_state()));
            cmd_print(state, buf);
            mini_snprintf(buf, sizeof(buf), "  Indirizzo IPv4  . . . : %s", (nic->ip != 0) ? ip_s : "0.0.0.0 (non configurato)");
            cmd_print(state, buf);
            mini_snprintf(buf, sizeof(buf), "  Subnet Mask . . . . . : %s", (nic->netmask != 0) ? mask_s : "0.0.0.0");
            cmd_print(state, buf);
            mini_snprintf(buf, sizeof(buf), "  Gateway predefinito . : %s", (nic->gateway != 0) ? gw_s : "0.0.0.0");
            cmd_print(state, buf);
            mini_snprintf(buf, sizeof(buf), "  Server DNS  . . . . . : %s", (nic->dns != 0) ? dns_s : "0.0.0.0");
            cmd_print(state, buf);
            mini_snprintf(buf, sizeof(buf), "  Pacchetti TX/RX . . . : %u / %u", (unsigned int)nic->tx_packets, (unsigned int)nic->rx_packets);
            cmd_print(state, buf);
        }
    } else if (strncmp(cmd, "ping", 4) == 0) {
        const char* target = cmd + 4;
        while (*target == ' ') target++;
        if (*target == '\0') {
            cmd_print(state, "Uso: ping <indirizzo_IP>  (es. ping 10.0.2.2 o ping 1.1.1.1)");
        } else {
            uint32_t tip = net_parse_ip(target);
            if (tip == 0 && strcmp(target, "0.0.0.0") != 0) {
                cmd_print(state, "Errore: indirizzo IP di destinazione non valido.");
            } else {
                char head_msg[CMD_COLS];
                mini_snprintf(head_msg, sizeof(head_msg), "Ping verso %s con 32 byte di dati:", target);
                cmd_print(state, head_msg);

                g_cmd_ping_state = state;
                g_cmd_ping_target = target;
                ping_stats_t stats;

                icmp_ping_ip(tip, 4, &stats, on_ping_reply_callback);

                g_cmd_ping_state = NULL;
                g_cmd_ping_target = NULL;

                cmd_print(state, "");
                char stat_msg[CMD_COLS];
                int loss = ((stats.packets_sent - stats.packets_recv) * 100) / (stats.packets_sent ? stats.packets_sent : 1);
                mini_snprintf(stat_msg, sizeof(stat_msg), "Statistiche Ping per %s:", target);
                cmd_print(state, stat_msg);
                mini_snprintf(stat_msg, sizeof(stat_msg), "  Pacchetti: Inviati = %u, Ricevuti = %u, Persi = %u (%d%% persi)",
                              (unsigned int)stats.packets_sent, (unsigned int)stats.packets_recv,
                              (unsigned int)(stats.packets_sent - stats.packets_recv), loss);
                cmd_print(state, stat_msg);

                if (stats.packets_recv > 0) {
                    mini_snprintf(stat_msg, sizeof(stat_msg), "Tempo approssimativo percorsi A/R in millisecondi:");
                    cmd_print(state, stat_msg);
                    mini_snprintf(stat_msg, sizeof(stat_msg), "  Minimo = %ums, Massimo = %ums, Medio = %ums",
                                  (unsigned int)stats.min_time_ms, (unsigned int)stats.max_time_ms, (unsigned int)stats.avg_time_ms);
                    cmd_print(state, stat_msg);
                }
            }
        }
    } else if (strcmp(cmd, "netstat") == 0) {
        cmd_print(state, "Proto Indirizzo locale   Indirizzo remoto  Stato");
        cmd_print(state, "--------------------------------------------------");
        int ccount = tcp_get_connections_count();
        int printed = 0;
        for (int i = 0; i < ccount; i++) {
            const tcp_conn_t* c = tcp_get_connection(i);
            if (!c) continue;
            char line[CMD_COLS];
            char lip[20], rip[20], lstr[28], rstr[28];
            net_format_ip(c->local_ip, lip, sizeof(lip));
            net_format_ip(c->remote_ip, rip, sizeof(rip));
            mini_snprintf(lstr, sizeof(lstr), "%s:%u", lip, (unsigned int)c->local_port);
            mini_snprintf(rstr, sizeof(rstr), "%s:%u", rip, (unsigned int)c->remote_port);
            mini_snprintf(line, sizeof(line), "TCP   %-20s %-17s %s", lstr, rstr, tcp_state_to_str(c->state));
            cmd_print(state, line);
            printed++;
        }
        if (printed == 0) {
            cmd_print(state, "Nessuna connessione TCP attiva.");
        }
    } else if (strcmp(cmd, "dhcp") == 0 || strcmp(cmd, "dhcp renew") == 0) {
        cmd_print(state, "Negoziazione DHCP (Discover -> Request -> Ack)...");
        dhcp_discover();
        for (int w = 0; w < 60; w++) {
            net_nic_poll();
            if (dhcp_get_state() == DHCP_STATE_BOUND) break;
            timer_sleep(10);
        }
        char dbuf[CMD_COLS];
        mini_snprintf(dbuf, sizeof(dbuf), "Stato DHCP: %s", dhcp_state_to_str(dhcp_get_state()));
        cmd_print(state, dbuf);
        net_nic_t* nic = net_nic_get_active();
        if (nic && nic->ip != 0) {
            char ip_s[20], mask_s[20], gw_s[20];
            net_format_ip(nic->ip, ip_s, sizeof(ip_s));
            net_format_ip(nic->netmask, mask_s, sizeof(mask_s));
            net_format_ip(nic->gateway, gw_s, sizeof(gw_s));
            mini_snprintf(dbuf, sizeof(dbuf), "Indirizzo assegnato: %s (Mask: %s GW: %s)", ip_s, mask_s, gw_s);
            cmd_print(state, dbuf);
        } else {
            cmd_print(state, "Nessun lease DHCP ottenuto dal server.");
        }
    } else if (strcmp(cmd, "mem") == 0) {
        cmd_print(state, "Architettura: x86_64 (Long Mode 64-bit)");
        cmd_print(state, "Paginazione: 4-Livelli (PML4, PDPT, PD 2MB Huge Pages)");
        cmd_print(state, "Memoria Heap Attiva: 64 MB Gestiti");
    } else if (strcmp(cmd, "clear") == 0 || strcmp(cmd, "cls") == 0) {
        for (int r = 0; r < CMD_ROWS; r++) {
            memset(state->lines[r], 0, CMD_COLS);
        }
        state->current_row = 0;
    } else if (strncmp(cmd, "echo ", 5) == 0) {
        cmd_print(state, cmd + 5);
    } else if (strcmp(cmd, "theme") == 0 || strcmp(cmd, "display") == 0) {
        cmd_print(state, "Apertura Proprieta' Schermo...");
        app_display_props_launch();
    } else if (strcmp(cmd, "notepad") == 0) {
        app_notepad_launch();
    } else if (strcmp(cmd, "calc") == 0) {
        app_calc_launch();
    } else if (strcmp(cmd, "explorer") == 0) {
        cmd_print(state, "Apertura Risorse del computer...");
        app_explorer_open_this_pc();
    } else if (strcmp(cmd, "oobe") == 0) {
        cmd_print(state, "Apertura Configurazione Guidata OOBE...");
        app_oobe_launch();
    } else if (strcmp(cmd, "rules") == 0 || strcmp(cmd, "rulesview") == 0 || strcmp(cmd, "rules.exe") == 0) {
        cmd_print(state, "Apertura Visualizzatore Regole NewOS (.NOR)...");
        app_rules_viewer_launch();
    } else if (strncmp(cmd, "browser", 7) == 0 || strncmp(cmd, "web", 3) == 0) {
        const char* target = cmd;
        if (strncmp(cmd, "browser", 7) == 0) target += 7;
        else if (strncmp(cmd, "web", 3) == 0) target += 3;
        while (*target == ' ') target++;
        cmd_print(state, "Avvio NewOS Web Browser...");
        if (*target) app_browser_open_url(target);
        else app_browser_launch();
    } else if (strncmp(cmd, "type ", 5) == 0) {
        const char* path = cmd + 5;
        while (*path == ' ') path++;
        if (strstr(path, ".nor") || strstr(path, ".NOR")) {
            cmd_print(state, "Accesso negato: formato binario proprietario.");
            cmd_print(state, "I file .NOR richiedono RULES.EXE (ambiente Desktop)");
            cmd_print(state, "o l'applicazione Windows 'Rules Win App'.");
        } else {
            cmd_print(state, "Apertura documento di testo...");
            app_notepad_open_file(path, "Visualizzazione documento...");
        }
    } else if (strcmp(cmd, "about") == 0) {
        app_about_launch();
    } else if (strcmp(cmd, "reboot") == 0) {
        cmd_print(state, "Riavvio in corso...");
        power_reboot();
    } else if (strcmp(cmd, "shutdown") == 0 || strcmp(cmd, "poweroff") == 0) {
        cmd_print(state, "Arresto del sistema in corso...");
        power_shutdown();
    } else if (strcmp(cmd, "standby") == 0 || strcmp(cmd, "sleep") == 0) {
        cmd_print(state, "Attivazione modalita' Standby...");
        power_enter_screen_standby();
    } else if (strcmp(cmd, "power") == 0 || strcmp(cmd, "powercfg") == 0) {
        power_status_t ps = power_get_status();
        const char* pnames[] = { "Bilanciato", "Risparmio energia", "Prestazioni" };
        char buf[56];
        mini_snprintf(buf, sizeof(buf), "Alimentazione: Rete CA (%d%% carica)", ps.battery_percent);
        cmd_print(state, buf);
        mini_snprintf(buf, sizeof(buf), "Profilo: %s (Timeout: %d sec)", pnames[ps.profile], ps.screen_timeout_sec);
        cmd_print(state, buf);
        mini_snprintf(buf, sizeof(buf), "Inattivita': %d sec (CPU Governor HLT)", ps.idle_seconds);
        cmd_print(state, buf);
        app_power_props_launch();
    } else if (strlen(cmd) > 0) {
        char err[CMD_COLS];
        mini_snprintf(err, sizeof(err), "'%s' comando non riconosciuto. Digita HELP.", cmd);
        cmd_print(state, err);
    }
}

static void cmd_draw(window_t* win) {
    cmd_state_t* state = (cmd_state_t*)win->app_data;
    if (!state) return;

    int32_t cx = win->x + 4;
    int32_t cy = win->y + 24;
    int32_t cw = win->width - 8;
    int32_t ch = win->height - 28;

    // Sfondo nero Prompt dei Comandi con cornice beveled scavata
    gfx_fill_rect(cx, cy, cw, ch, 0x000000);
    gfx_draw_bevel_rect(cx, cy, cw, ch, true);

    // Stampa righe di output
    for (int r = 0; r < CMD_ROWS; r++) {
        if (state->lines[r][0] != '\0') {
            gfx_draw_string(cx + 6, cy + 6 + r * 16, state->lines[r], 0xFFFFFF, 0x000000, false);
        }
    }

    // Stampa prompt corrente e testo in digitazione
    char cur_line[CMD_COLS + 16];
    mini_snprintf(cur_line, sizeof(cur_line), "C:\\> %s_", state->input_line);
    gfx_draw_string(cx + 6, cy + 6 + state->current_row * 16, cur_line, 0xFFFFFF, 0x000000, false);
}

static void cmd_key(window_t* win, char c) {
    cmd_state_t* state = (cmd_state_t*)win->app_data;
    if (!state) return;

    if (c == '\n') {
        cmd_execute(state, state->input_line);
        memset(state->input_line, 0, CMD_COLS);
        state->input_pos = 0;
    } else if (c == '\b') {
        if (state->input_pos > 0) {
            state->input_pos--;
            state->input_line[state->input_pos] = '\0';
        }
    } else if (c >= 32 && c <= 126) {
        if (state->input_pos < CMD_COLS - 2) {
            state->input_line[state->input_pos++] = c;
            state->input_line[state->input_pos] = '\0';
        }
    }
}

void app_cmd_launch(void) {
    window_t* win = wm_create_window("Prompt dei comandi - NewOS V1", 80, 60, 480, 340, APP_CMD);
    if (!win) return;

    cmd_state_t* state = (cmd_state_t*)kmalloc(sizeof(cmd_state_t));
    if (state) {
        memset(state, 0, sizeof(cmd_state_t));
        cmd_print(state, "NewOS V1 64-bit [Versione 1.0.2026]");
        cmd_print(state, "(C) @ Newspoint Software - NewSoft - NewOS V1");
        cmd_print(state, "Digita 'help' per la lista dei comandi.");
        cmd_print(state, "");
        win->app_data = state;
        win->on_draw = cmd_draw;
        win->on_key = cmd_key;
    }
}
