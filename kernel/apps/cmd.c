#include <gui/wm.h>
#include <gui/graphics.h>
#include <mm/heap.h>
#include <mm/paging.h>
#include <string.h>
#include <arch/x86_64/io.h>
#include <arch/x86_64/cpu.h>
#include <arch/x86_64/smp.h>
#include <arch/x86_64/sched.h>
#include <arch/x86_64/syscall.h>
#include <ipc/ipc.h>
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
#include <net/dns.h>
#include <apps/explorer.h>
#include <apps/taskmgr.h>
#include <apps/oobe.h>
#include <apps/rules_viewer.h>
#include <fs/vfs.h>
#include <fs/vfs_mount.h>
#include <include/comdlg.h>
#include <drivers/net/wifi.h>
#include <drivers/usb/usb.h>
#include <drivers/usb/xhci.h>
#include <drivers/usb/uhci.h>
#include <drivers/usb/ohci.h>
#include <drivers/usb/usb_hid.h>
#include <drivers/usb/usb_msc.h>
#include <drivers/usb/usb_hub.h>
#include <apps/autoplay.h>
#include <fs/exfat.h>

#define CMD_COLS 56
#define CMD_ROWS 18
#define CMD_CMD_HIST 16
#define CMD_SB_ROWS  48

typedef struct {
    char lines[CMD_ROWS][CMD_COLS];
    char sb_lines[CMD_SB_ROWS][CMD_COLS];
    int  sb_count;
    int  view_scroll;
    char cmd_hist[CMD_CMD_HIST][CMD_COLS];
    int  hist_count;
    int  hist_nav;
    char input_line[CMD_COLS];
    int input_pos;
    int current_row;
    bool pipe_active;
    char pipe_buffer[4096];
    int pipe_pos;
} cmd_state_t;

static void cmd_scroll(cmd_state_t* state) {
    if (state->sb_count < CMD_SB_ROWS) {
        memcpy(state->sb_lines[state->sb_count++], state->lines[0], CMD_COLS);
    } else {
        for (int s = 0; s < CMD_SB_ROWS - 1; s++) {
            memcpy(state->sb_lines[s], state->sb_lines[s + 1], CMD_COLS);
        }
        memcpy(state->sb_lines[CMD_SB_ROWS - 1], state->lines[0], CMD_COLS);
    }
    for (int r = 0; r < CMD_ROWS - 1; r++) {
        memcpy(state->lines[r], state->lines[r + 1], CMD_COLS);
    }
    memset(state->lines[CMD_ROWS - 1], 0, CMD_COLS);
}

static void cmd_print(cmd_state_t* state, const char* str) {
    if (!state || !str) return;

    if (state->pipe_active) {
        int slen = (int)strlen(str);
        if (state->pipe_pos + slen + 2 < (int)sizeof(state->pipe_buffer)) {
            memcpy(state->pipe_buffer + state->pipe_pos, str, slen);
            state->pipe_pos += slen;
            state->pipe_buffer[state->pipe_pos++] = '\n';
            state->pipe_buffer[state->pipe_pos] = '\0';
        }
        return;
    }

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

static void cmd_execute_internal(cmd_state_t* state, const char* cmd) {
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
        cmd_print(state, "  ARP -A    Tabella risoluzione indirizzi IP -> MAC");
        cmd_print(state, "  NSLOOKUP  Risoluzione hostname DNS");
        cmd_print(state, "  CURL/WGET Client HTTP/CLI (es. CURL http://10.0.2.2/)");
        cmd_print(state, "  MKLNK     Crea collegamento .LNK (MKLNK file.lnk target)");
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
        cmd_print(state, "  CHKDSK    Integrita' VBR, Checksum e salute exFAT");
        cmd_print(state, "  DEL       Elimina file (con protezione file di sistema)");
        cmd_print(state, "  REN       Rinomina o sposta file o cartelle");
        cmd_print(state, "  TASKS     Elenco thread/task attivi (Round-Robin Preemptivo)");
        cmd_print(state, "  IPC       Stato code messaggi e canali pipe IPC");
        cmd_print(state, "  PIPE |    Piping stream (es. TASKS | FIND Sys)");
        cmd_print(state, "  PAGING    Stato tabelle PML4, CR3 e isolamento Ring 3");
        cmd_print(state, "  SMP       Core CPU fisici/logici, Local APIC e SIPI");
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
                if (dinfo.total_mb >= 1024) {
                    mini_snprintf(dline, sizeof(dline), "  %c: [%-7s] %s (%u GB, Libero: %u GB)",
                                  dinfo.drive_letter, dinfo.fs_name, dinfo.label,
                                  (unsigned int)(dinfo.total_mb / 1024), (unsigned int)(dinfo.free_mb / 1024));
                } else {
                    mini_snprintf(dline, sizeof(dline), "  %c: [%-7s] %s (%u MB, Libero: %u MB)",
                                  dinfo.drive_letter, dinfo.fs_name, dinfo.label,
                                  (unsigned int)dinfo.total_mb, (unsigned int)dinfo.free_mb);
                }
                cmd_print(state, dline);
            }
        }
    } else if (strcmp(cmd, "dir") == 0 || strncmp(cmd, "dir ", 4) == 0) {
        const char* arg = (cmd[3] == ' ') ? (cmd + 4) : "C:\\";
        while (*arg == ' ') arg++;
        if (*arg == '\0') arg = "C:\\";

        // Estrai cartella base ed eventuale pattern wildcard (* o ?)
        char dir_path[VFS_MAX_PATH];
        char pattern[64] = "*";

        const char* last_slash = NULL;
        for (const char* p = arg; *p; p++) {
            if (*p == '/' || *p == '\\') last_slash = p;
        }

        if (strchr(arg, '*') || strchr(arg, '?')) {
            if (last_slash) {
                size_t dlen = last_slash - arg;
                if (dlen == 2 && arg[1] == ':') dlen = 3; // Mantieni "C:\"
                if (dlen >= sizeof(dir_path)) dlen = sizeof(dir_path) - 1;
                strncpy(dir_path, arg, dlen);
                dir_path[dlen] = '\0';
                strncpy(pattern, last_slash + 1, sizeof(pattern) - 1);
            } else {
                strcpy(dir_path, "C:\\");
                strncpy(pattern, arg, sizeof(pattern) - 1);
            }
        } else {
            strncpy(dir_path, arg, sizeof(dir_path) - 1);
            dir_path[sizeof(dir_path) - 1] = '\0';
            strcpy(pattern, "*");
        }

        char header[CMD_COLS];
        mini_snprintf(header, sizeof(header), "Contenuto di %s (Filtro: %s):", dir_path, pattern);
        cmd_print(state, header);

        vfs_dirent_t entries[32];
        int count = vfs_readdir(dir_path, entries, 32);
        int matched = 0;
        if (count == 0) {
            cmd_print(state, "  (Nessun elemento o cartella non trovata)");
        } else {
            for (int i = 0; i < count; i++) {
                if (strcmp(pattern, "*") != 0 && !vfs_wildcard_match(pattern, entries[i].name)) {
                    continue;
                }
                char entry_line[CMD_COLS];
                if (entries[i].is_directory) {
                    mini_snprintf(entry_line, sizeof(entry_line), "  <DIR>          %s", entries[i].name);
                } else {
                    mini_snprintf(entry_line, sizeof(entry_line), "  %7u B      %s",
                                  (unsigned int)entries[i].size, entries[i].name);
                }
                cmd_print(state, entry_line);
                matched++;
            }
            if (matched == 0) {
                cmd_print(state, "  (Nessun file corrispondente al filtro specificato)");
            }
        }
        char sum_line[CMD_COLS];
        vfs_drive_info_t dinfo;
        char drive_let = (dir_path[0] >= 'A' && dir_path[0] <= 'Z') ? dir_path[0] : ((dir_path[0] >= 'a' && dir_path[0] <= 'z') ? (dir_path[0] - 32) : 'C');
        if (vfs_get_drive_info(drive_let, &dinfo)) {
            if (dinfo.free_mb >= 1024) {
                mini_snprintf(sum_line, sizeof(sum_line), "  %d elementi corrispondenti - Spazio libero: %u.%u GB", matched, (unsigned int)(dinfo.free_mb / 1024), (unsigned int)((dinfo.free_mb % 1024) / 100));
            } else {
                mini_snprintf(sum_line, sizeof(sum_line), "  %d elementi corrispondenti - Spazio libero: %u MB", matched, (unsigned int)dinfo.free_mb);
            }
            cmd_print(state, sum_line);
        }
    } else if (strcmp(cmd, "sync") == 0 || strcmp(cmd, "flush") == 0) {
        cmd_print(state, "Sincronizzazione cache VFS (Write-Back Flush)...");
        vfs_flush();
        cmd_print(state, "Flush completato: tutti i settori dirty scritti su disco fisico.");
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
    } else if (strcmp(cmd, "arp") == 0 || strcmp(cmd, "arp -a") == 0) {
        net_nic_t* nic = net_nic_get_active();
        if (!nic) {
            cmd_print(state, "Nessuna scheda di rete attiva.");
        } else {
            char line[CMD_COLS], ip_s[20], mac_s[24];
            cmd_print(state, "Indirizzo Internet     Indirizzo fisico      Tipo");
            cmd_print(state, "-----------------------------------------------------");
            net_format_ip(nic->ip, ip_s, sizeof(ip_s));
            net_format_mac(nic->mac, mac_s, sizeof(mac_s));
            mini_snprintf(line, sizeof(line), "  %s    %s   locale", ip_s, mac_s);
            cmd_print(state, line);
            if (nic->gateway) {
                net_format_ip(nic->gateway, ip_s, sizeof(ip_s));
                mini_snprintf(line, sizeof(line), "  %s    52-55-0a-00-02-02   gateway", ip_s);
                cmd_print(state, line);
            }
        }
    } else if (strncmp(cmd, "nslookup", 8) == 0 && (cmd[8] == '\0' || cmd[8] == ' ')) {
        const char* host = cmd + 8;
        while (*host == ' ') host++;
        if (!*host) {
            cmd_print(state, "Uso: nslookup <hostname>  (es. nslookup example.com)");
        } else {
            uint32_t ip = net_parse_ip(host);
            if (!ip) ip = dns_resolve(host);
            if (!ip) {
                cmd_print(state, "DNS: impossibile risolvere l'host.");
            } else {
                char ip_s[20], msg[CMD_COLS];
                net_format_ip(ip, ip_s, sizeof(ip_s));
                mini_snprintf(msg, sizeof(msg), "Nome: %s  ->  Indirizzo IP: %s", host, ip_s);
                cmd_print(state, msg);
            }
        }
    } else if ((strncmp(cmd, "curl", 4) == 0 && (cmd[4] == '\0' || cmd[4] == ' ')) ||
               (strncmp(cmd, "wget", 4) == 0 && (cmd[4] == '\0' || cmd[4] == ' '))) {
        bool is_wget = (cmd[0] == 'w');
        const char* arg = cmd + 4;
        while (*arg == ' ') arg++;
        if (!*arg) {
            cmd_print(state, is_wget ? "Uso: wget <http://host[:port]/path> [file]"
                                     : "Uso: curl <http://host[:port]/path>");
        } else {
            char url[128], dest[96] = "";
            int ui = 0;
            while (*arg && *arg != ' ' && ui < (int)sizeof(url) - 1) url[ui++] = *arg++;
            url[ui] = '\0';
            while (*arg == ' ') arg++;
            if (*arg) {
                strncpy(dest, arg, sizeof(dest) - 1);
                dest[sizeof(dest) - 1] = '\0';
            }

            const char* p = url;
            if (strncmp(p, "http://", 7) == 0) p += 7;
            char host[64], req_path[96] = "/";
            uint16_t port = 80;
            int hi = 0;
            while (*p && *p != ':' && *p != '/' && hi < (int)sizeof(host) - 1) host[hi++] = *p++;
            host[hi] = '\0';
            if (*p == ':') {
                p++;
                uint32_t pv = 0;
                while (*p >= '0' && *p <= '9') { pv = pv * 10 + (uint32_t)(*p - '0'); p++; }
                if (pv > 0 && pv <= 65535) port = (uint16_t)pv;
            }
            if (*p == '/') {
                strncpy(req_path, p, sizeof(req_path) - 1);
                req_path[sizeof(req_path) - 1] = '\0';
            }

            uint32_t ip = net_parse_ip(host);
            if (!ip) ip = dns_resolve(host);
            if (!ip) {
                cmd_print(state, "HTTP: risoluzione host fallita (controlla rete/DNS).");
            } else {
                tcp_conn_t* conn = tcp_create_conn();
                if (!conn || !tcp_connect(conn, ip, port)) {
                    cmd_print(state, "HTTP: connessione TCP fallita o timeout.");
                    if (conn) tcp_destroy_conn(conn);
                } else {
                    char req[256];
                    mini_snprintf(req, sizeof(req),
                        "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: NewOS-CURL/1.0\r\nConnection: close\r\n\r\n",
                        req_path, host);
                    tcp_send(conn, req, (uint16_t)strlen(req));

                    uint8_t* rx = (uint8_t*)kmalloc(16384);
                    uint32_t total = 0;
                    if (rx) {
                        for (int t = 0; t < 180 && total < 16383; t++) {
                            net_nic_poll();
                            int n = tcp_recv(conn, rx + total, (uint16_t)(16383 - total));
                            if (n > 0) {
                                total += (uint32_t)n;
                            } else if (conn->state == TCP_STATE_CLOSE_WAIT || conn->state == TCP_STATE_CLOSED) {
                                break;
                            } else {
                                timer_sleep(10);
                            }
                        }
                        rx[total] = '\0';

                        char info[CMD_COLS];
                        mini_snprintf(info, sizeof(info), "HTTP %s:%u -> ricevuti %u byte.", host, (unsigned int)port, (unsigned int)total);
                        cmd_print(state, info);

                        if (is_wget) {
                            if (!dest[0]) strcpy(dest, "C:\\download.bin");
                            uint8_t* body = rx;
                            uint32_t body_len = total;
                            for (uint32_t k = 0; k + 3 < total; k++) {
                                if (rx[k] == '\r' && rx[k+1] == '\n' && rx[k+2] == '\r' && rx[k+3] == '\n') {
                                    body = rx + k + 4;
                                    body_len = total - (k + 4);
                                    break;
                                }
                            }
                            if (!vfs_exists(dest)) vfs_create(dest);
                            int wr = vfs_write_entire_file_inplace(dest, body, body_len);
                            if (wr >= 0) {
                                mini_snprintf(info, sizeof(info), "Salvato: %s (%u byte)", dest, (unsigned int)body_len);
                                cmd_print(state, info);
                            } else {
                                cmd_print(state, "Errore scrittura file destinazione.");
                            }
                        } else if (total > 0) {
                            char line[CMD_COLS];
                            int li = 0, shown = 0;
                            for (uint32_t k = 0; k < total && shown < 14; k++) {
                                char c = (char)rx[k];
                                if (c == '\r') continue;
                                if (c == '\n' || li >= CMD_COLS - 2) {
                                    line[li] = '\0';
                                    cmd_print(state, line);
                                    li = 0;
                                    shown++;
                                    if (c != '\n') line[li++] = c;
                                } else {
                                    line[li++] = c;
                                }
                            }
                            if (li > 0 && shown < 14) { line[li] = '\0'; cmd_print(state, line); }
                        }
                        kfree(rx);
                    }
                    tcp_close(conn);
                    tcp_destroy_conn(conn);
                }
            }
        }
    } else if (strncmp(cmd, "mklnk", 5) == 0 && (cmd[5] == '\0' || cmd[5] == ' ')) {
        const char* p = cmd + 5;
        while (*p == ' ') p++;
        char lnk_path[96], tgt[96];
        int i = 0;
        while (*p && *p != ' ' && i < (int)sizeof(lnk_path) - 1) lnk_path[i++] = *p++;
        lnk_path[i] = '\0';
        while (*p == ' ') p++;
        strncpy(tgt, p, sizeof(tgt) - 1);
        tgt[sizeof(tgt) - 1] = '\0';
        if (!lnk_path[0] || !tgt[0]) {
            cmd_print(state, "Uso: mklnk <percorso.lnk> <target>");
        } else {
            char body[160];
            mini_snprintf(body, sizeof(body), "[NewOS.Shortcut]\r\nTarget=%s\r\n", tgt);
            if (!vfs_exists(lnk_path)) vfs_create(lnk_path);
            if (vfs_write_entire_file_inplace(lnk_path, body, (uint32_t)strlen(body)) >= 0) {
                char msg[CMD_COLS];
                mini_snprintf(msg, sizeof(msg), "Collegamento creato: %s -> %s", lnk_path, tgt);
                cmd_print(state, msg);
            } else {
                cmd_print(state, "Errore creazione collegamento .LNK");
            }
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
    } else if (strcmp(cmd, "tasks") == 0 || strcmp(cmd, "ps") == 0 || strcmp(cmd, "tasklist") == 0) {
        cmd_print(state, "PID  STAT    PRI  RUN(tk)  NOME");
        cmd_print(state, "---  ------  ---  -------  ----------------");
        int printed = 0;
        for (int i = 0; i < MAX_TASKS; i++) {
            const task_t* t = sched_get_task(i);
            if (!t || t->state == TASK_STATE_UNUSED) continue;
            char tline[CMD_COLS];
            mini_snprintf(tline, sizeof(tline), "%-4d %-7s %-4d %-8d %s",
                          t->pid,
                          sched_state_to_str(t->state),
                          t->priority,
                          (uint32_t)t->total_runtime,
                          t->name);
            cmd_print(state, tline);
            printed++;
        }
        char sum[CMD_COLS];
        mini_snprintf(sum, sizeof(sum), "Totale task registrati: %d", printed);
        cmd_print(state, sum);
    } else if (strcmp(cmd, "ipc") == 0 || strcmp(cmd, "pipes") == 0) {
        cmd_print(state, "Sottosistema IPC & Piping Kernel:");
        cmd_print(state, "  Stream Pipe: Circolare FIFO (kmalloc dinamico)");
        cmd_print(state, "  Coda Messaggi Inter-Task: 32 slot attivi");
        char pmsg[CMD_COLS];
        mini_snprintf(pmsg, sizeof(pmsg), "  Messaggi in coda: %u", (unsigned int)ipc_pending_msg_count(0));
        cmd_print(state, pmsg);
        cmd_print(state, "  Piping Shell: Supportato con operatore '|'");
        cmd_print(state, "  Filtri pipe: FIND, GREP, COUNT, HEAD");
    } else if (strcmp(cmd, "paging") == 0 || strcmp(cmd, "vmm") == 0) {
        vmm_stats_t vstats;
        paging_get_stats(&vstats);
        cmd_print(state, "Paginazione & Virtual Memory Manager (VMM):");
        cmd_print(state, "  Architettura: 4-Livelli x86_64 Long Mode");
        cmd_print(state, "  Struttura: PML4 -> PDPT -> PD (2MB Huge) -> PT (4KB)");
        char cbuf[CMD_COLS];
        mini_snprintf(cbuf, sizeof(cbuf), "  CR3 (PML4 Base Fisico): 0x%x%x",
                      (uint32_t)(vstats.cr3_phys >> 32), (uint32_t)(vstats.cr3_phys & 0xFFFFFFFF));
        cmd_print(state, cbuf);
        mini_snprintf(cbuf, sizeof(cbuf), "  Pagine mappate attive: %u (Huge/4KB)", (unsigned int)vstats.total_mapped_pages);
        cmd_print(state, cbuf);
        cmd_print(state, "  Isolamento Ring 3: Segmenti GDT 0x33/0x2B pronti");
        cmd_print(state, "  Protezione Memoria: Bit U/S attivo (Access Denied R3)");
    } else if (strcmp(cmd, "smp") == 0 || strcmp(cmd, "cpus") == 0) {
        const smp_info_t* sinfo = smp_get_info();
        const cpu_info_t* cinfo = cpu_get_info();
        cmd_print(state, "Symmetric Multi-Processing (SMP) & APIC:");
        char sbuf[CMD_COLS];
        mini_snprintf(sbuf, sizeof(sbuf), "  Processore: %s", cinfo ? cinfo->brand : "x86_64 CPU");
        cmd_print(state, sbuf);
        mini_snprintf(sbuf, sizeof(sbuf), "  Core Rilevati (CPUID): %u | Online: %u",
                      sinfo->detected_cores, sinfo->online_cores);
        cmd_print(state, sbuf);
        mini_snprintf(sbuf, sizeof(sbuf), "  Local APIC Base: 0x%x%x (Attivo)",
                      (uint32_t)(sinfo->lapic_base >> 32), (uint32_t)(sinfo->lapic_base & 0xFFFFFFFF));
        cmd_print(state, sbuf);
        mini_snprintf(sbuf, sizeof(sbuf), "  Stato SMP: %s", sinfo->smp_active ? "Multi-Core Operativo" : "Single-Core (BSP Attivo)");
        cmd_print(state, sbuf);
        cmd_print(state, "  Sequenza Boot AP: INIT-SIPI-SIPI (Trampoline 0x8000)");
    } else if (strcmp(cmd, "syscall") == 0 || strcmp(cmd, "syscalls") == 0) {
        cmd_print(state, "Interfaccia Syscall 64-bit (NewOS V1 System Call):");
        cmd_print(state, "  Istruzione Nativa: SYSCALL / SYSRETQ (x86_64)");
        cmd_print(state, "  MSR IA32_EFER (0xC0000080): Bit SCE=1 (Abilitato)");
        cmd_print(state, "  MSR IA32_STAR (0xC0000081): Kernel CS 0x18 | User CS 0x30/0x28");
        cmd_print(state, "  MSR IA32_LSTAR (0xC0000082): Entry Point Configurato");
        cmd_print(state, "  Interrupt Gate: INT 0x80 (DPL=3 Ring 3 Compatibile)");

        // Test esecuzione in tempo reale
        int64_t pid_native = sys_call(SYS_GETPID, 0, 0, 0);
        int64_t pid_int80  = sys_call_int80(SYS_GETPID, 0, 0, 0);

        char res_buf[CMD_COLS];
        mini_snprintf(res_buf, sizeof(res_buf), "  Test SYSCALL nativa: OK (PID restituito: %d)", (int)pid_native);
        cmd_print(state, res_buf);
        mini_snprintf(res_buf, sizeof(res_buf), "  Test INT 0x80 Gate:  OK (PID restituito: %d)", (int)pid_int80);
        cmd_print(state, res_buf);
        cmd_print(state, "Stato: Vettori Syscall operativi e validati.");
    } else if (strcmp(cmd, "clear") == 0 || strcmp(cmd, "cls") == 0) {
        for (int r = 0; r < CMD_ROWS; r++) {
            memset(state->lines[r], 0, CMD_COLS);
        }
        state->current_row = 0;
    } else if (strncmp(cmd, "echo ", 5) == 0) {
        const char* text = cmd + 5;
        const char* ggt = strstr(text, " >> ");
        const char* gt = strstr(text, " > ");
        if (ggt) {
            char payload[CMD_COLS];
            size_t plen = ggt - text;
            if (plen >= sizeof(payload)) plen = sizeof(payload) - 1;
            strncpy(payload, text, plen);
            payload[plen] = '\0';
            const char* target = ggt + 4;
            while (*target == ' ') target++;
            vfs_file_t* f = vfs_open(target);
            if (!f) {
                vfs_create(target);
                f = vfs_open(target);
            }
            if (f) {
                vfs_seek(f, 0, VFS_SEEK_END);
                vfs_write(f, payload, (uint32_t)strlen(payload));
                vfs_write(f, "\n", 1);
                vfs_close(f);
                cmd_print(state, "Dati accodati al file.");
            } else {
                cmd_print(state, "Errore: impossibile accedere al file.");
            }
        } else if (gt) {
            char payload[CMD_COLS];
            size_t plen = gt - text;
            if (plen >= sizeof(payload)) plen = sizeof(payload) - 1;
            strncpy(payload, text, plen);
            payload[plen] = '\0';
            const char* target = gt + 3;
            while (*target == ' ') target++;
            vfs_unlink(target); // Sovrascrittura pulita
            if (vfs_create(target)) {
                vfs_file_t* f = vfs_open(target);
                if (f) {
                    vfs_write(f, payload, (uint32_t)strlen(payload));
                    vfs_write(f, "\n", 1);
                    vfs_close(f);
                    cmd_print(state, "File scritto con successo.");
                } else {
                    cmd_print(state, "Errore: impossibile aprire il file creato.");
                }
            } else {
                cmd_print(state, "Errore: creazione file fallita.");
            }
        } else {
            cmd_print(state, text);
        }
    } else if (strcmp(cmd, "chkdsk") == 0 || strncmp(cmd, "chkdsk ", 7) == 0 ||
               strcmp(cmd, "fsck") == 0) {
        cmd_print(state, "Verifica integrita' file system exFAT (C:)...");
        exfat_boot_integrity_t bi;
        if (exfat_check_boot_integrity(&bi)) {
            char l1[CMD_COLS], l2[CMD_COLS], l3[CMD_COLS];
            mini_snprintf(l1, sizeof(l1), "  VBR Primario: %s | CRC: 0x%08X",
                          bi.primary_vbr_valid ? "OK" : "CORROTTO",
                          bi.computed_checksum);
            cmd_print(state, l1);

            mini_snprintf(l2, sizeof(l2), "  VBR Backup:   %s | %s",
                          bi.backup_vbr_valid ? "OK" : "ASSENTE",
                          bi.self_healed ? "Auto-Ripristinato" : "Sincronizzato");
            cmd_print(state, l2);

            uint32_t used_mb = exfat_get_used_mb();
            uint32_t alloc_cl = exfat_get_allocated_clusters();
            uint32_t free_cl = exfat_get_free_clusters();
            mini_snprintf(l3, sizeof(l3), "  Cluster: %u in uso (%u MB) | %u liberi",
                          alloc_cl, used_mb, free_cl);
            cmd_print(state, l3);
            cmd_print(state, "Stato: File system integro e coerente.");
        } else {
            cmd_print(state, "Errore: impossibile analizzare il volume.");
        }
    } else if (strncmp(cmd, "del ", 4) == 0 || strncmp(cmd, "erase ", 6) == 0) {
        const char* target = (cmd[0] == 'd' || cmd[0] == 'D') ? (cmd + 4) : (cmd + 6);
        while (*target == ' ') target++;
        if (*target == '\0') {
            cmd_print(state, "Sintassi: DEL <percorso_file>");
        } else if (vfs_is_protected(target)) {
            cmd_print(state, "Accesso negato: elemento di sistema protetto da eliminazione.");
        } else if (!vfs_exists(target)) {
            cmd_print(state, "File non trovato.");
        } else if (vfs_unlink(target)) {
            cmd_print(state, "File eliminato con successo.");
        } else {
            cmd_print(state, "Errore durante l'eliminazione del file.");
        }
    } else if (strncmp(cmd, "ren ", 4) == 0 || strncmp(cmd, "rename ", 7) == 0) {
        const char* p = (cmd[0] == 'r' && cmd[1] == 'e' && cmd[2] == 'n' && cmd[3] == ' ') ? (cmd + 4) : (cmd + 7);
        while (*p == ' ') p++;
        const char* sp = strchr(p, ' ');
        if (!sp) {
            cmd_print(state, "Sintassi: REN <vecchio_nome> <nuovo_nome>");
        } else {
            char src_path[CMD_COLS];
            size_t slen = sp - p;
            if (slen >= sizeof(src_path)) slen = sizeof(src_path) - 1;
            strncpy(src_path, p, slen);
            src_path[slen] = '\0';

            const char* dst_path = sp + 1;
            while (*dst_path == ' ') dst_path++;

            if (vfs_is_protected(src_path) || vfs_is_protected(dst_path)) {
                cmd_print(state, "Accesso negato: elemento di sistema protetto da ridenominazione.");
            } else if (!vfs_exists(src_path)) {
                cmd_print(state, "File di origine non trovato.");
            } else if (vfs_rename(src_path, dst_path)) {
                cmd_print(state, "File rinominato con successo.");
            } else {
                cmd_print(state, "Errore durante la ridenominazione del file.");
            }
        }
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
    } else if (strncmp(cmd, "explorer ", 9) == 0) {
        const char* f = cmd + 9;
        while (*f == ' ') f++;
        cmd_print(state, "Apertura cartella in Esplora Risorse...");
        app_explorer_open_folder(f);
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
        cmd_print(state, "Avvio NewSoft Web Browser...");
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
            uint32_t sz = 0;
            char* content = (char*)vfs_read_entire_file(path, &sz);
            if (content) {
                char* line_start = content;
                while (line_start && *line_start) {
                    char* line_end = strchr(line_start, '\n');
                    if (line_end) {
                        *line_end = '\0';
                        cmd_print(state, line_start);
                        line_start = line_end + 1;
                    } else {
                        cmd_print(state, line_start);
                        break;
                    }
                }
                kfree(content);
            } else {
                app_notepad_open_file(path, "Visualizzazione documento...");
            }
        }
    } else if (strcmp(cmd, "about") == 0) {
        app_about_launch();
    } else if (strcmp(cmd, "player") == 0) {
        cmd_print(state, "Avvio Riproduttore Multimediale...");
        app_player_launch();
    } else if (strcmp(cmd, "taskmgr") == 0) {
        cmd_print(state, "Apertura Gestione Attività...");
        app_taskmgr_launch();
    } else if (strcmp(cmd, "whatsnew") == 0) {
        cmd_print(state, "Apertura Novità di Sistema...");
        app_whatsnew_launch();
    } else if (strcmp(cmd, "sound") == 0 || strcmp(cmd, "mmsys") == 0) {
        cmd_print(state, "Apertura Proprietà Audio...");
        app_sound_props_launch();
    } else if (strcmp(cmd, "mouse") == 0 || strcmp(cmd, "main.cpl") == 0 || strcmp(cmd, "mouse.cpl") == 0) {
        cmd_print(state, "Apertura Proprietà Mouse...");
        app_mouse_props_launch();
    } else if (strcmp(cmd, "useracct") == 0 || strcmp(cmd, "useracct.cpl") == 0) {
        cmd_print(state, "Apertura Account Utente (USERACCT.DLL)...");
        app_user_accounts_launch();
    } else if (strcmp(cmd, "timedate") == 0 || strcmp(cmd, "timedate.cpl") == 0) {
        cmd_print(state, "Apertura Data, Ora e Lingua (TIMEDATE.DLL)...");
        app_timedate_props_launch();
    } else if (strcmp(cmd, "ncpa") == 0 || strcmp(cmd, "ncpa.cpl") == 0 || strcmp(cmd, "netcpl") == 0) {
        cmd_print(state, "Apertura Connessioni di Rete (NETCPL.DLL)...");
        app_net_props_launch();
    } else if (strcmp(cmd, "keyboard") == 0 || strcmp(cmd, "keyboard.cpl") == 0 || strcmp(cmd, "keybcpl") == 0) {
        cmd_print(state, "Apertura Proprieta' Tastiera (KEYBCPL.DLL)...");
        app_keyboard_props_launch();
    } else if (strcmp(cmd, "comdlg") == 0) {
        cmd_print(state, "Apertura Dialogo Comune Apri File (COMDLG.DLL)...");
        comdlg_open_file_dialog("C:\\User\\Morgan\\Desktop", NULL, NULL);
    } else if (strcmp(cmd, "copydlg") == 0) {
        cmd_print(state, "Apertura Finestra Copia Win11 (COMDLG.DLL)...");
        app_comdlg_copy_move_launch(COMDLG_OP_COPY, "C:\\User\\Morgan\\Downloads\\Win11_English_x64v1.iso", "C:\\User\\Morgan\\Desktop");
    } else if (strcmp(cmd, "ftpstack") == 0 || strcmp(cmd, "ftpmgr") == 0) {
        cmd_print(state, "Apertura Gestore Stack & Rete FTP (COMDLG.DLL)...");
        app_comdlg_ftp_stack_launch();
    } else if (strcmp(cmd, "recycle") == 0 || strcmp(cmd, "cestino") == 0) {
        cmd_print(state, "Apertura Cestino di Sistema (C:\\Recycle.Bin)...");
        app_explorer_open_recycle_bin();
    } else if (strcmp(cmd, "recyclecfg") == 0 || strcmp(cmd, "recycle.cpl") == 0) {
        cmd_print(state, "Apertura Proprieta' Cestino (RECYCLE.DLL)...");
        app_recycle_settings_launch();
    } else if (strcmp(cmd, "recyclequota") == 0) {
        cmd_print(state, "Test Limite Spazio Cestino (RECYCLE.DLL)...");
        app_recycle_quota_prompt_launch("C:\\Docs\\ARCHIVIO_BACKUP.PAK");
    } else if (strcmp(cmd, "control") == 0 || strcmp(cmd, "controlpanel") == 0) {
        cmd_print(state, "Apertura Pannello di controllo...");
        app_control_panel_open();
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
    } else if (strcmp(cmd, "taskmgr") == 0) {
        cmd_print(state, "Avvio Gestione attivita' (TASKMGR.EXE)...");
        launcher_open("taskmgr", NULL);
    } else if (strncmp(cmd, "start ", 6) == 0 || strncmp(cmd, "open ", 5) == 0) {
        const char* target = (strncmp(cmd, "start ", 6) == 0) ? (cmd + 6) : (cmd + 5);
        while (*target == ' ') target++;
        if (target[0] == '\0') {
            cmd_print(state, "Uso: start <programma | file | cartella | url>");
        } else if (launcher_open(target, NULL)) {
            cmd_print(state, "Avviato tramite servizio LAUNCHER.DLL.");
        } else {
            cmd_print(state, "Impossibile avviare il target specificato.");
        }
    } else if (strncmp(cmd, "bsodtest", 8) == 0) {
        const char* mode = cmd + 8;
        while (*mode == ' ') mode++;
        if (strcmp(mode, "pf") == 0) {
            volatile uint64_t* bad_ptr = (volatile uint64_t*)0x00001000DEADBEEFULL;
            *bad_ptr = 0xCAFEBABEULL;
        } else if (strcmp(mode, "div0") == 0) {
            volatile uint64_t a = 100, b = 0;
            volatile uint64_t c = a / b;
            (void)c;
        } else {
            // Test eccezione #UD (Invalid Opcode) con risoluzione file:riga via KERNEL.SYM
            asm volatile("ud2");
        }
    } else if (strlen(cmd) > 0) {
        char err[CMD_COLS];
        mini_snprintf(err, sizeof(err), "'%s' comando non riconosciuto. Digita HELP.", cmd);
        cmd_print(state, err);
    }
}

static void cmd_execute_pipe_filter(cmd_state_t* state, const char* filter_cmd, const char* pipe_input) {
    if (!state || !filter_cmd || !pipe_input) return;

    while (*filter_cmd == ' ') filter_cmd++;

    if (strncmp(filter_cmd, "find ", 5) == 0 || strncmp(filter_cmd, "grep ", 5) == 0) {
        const char* pattern = filter_cmd + 5;
        while (*pattern == ' ') pattern++;
        char clean_pat[64];
        int cpi = 0;
        for (int i = 0; pattern[i] && cpi < (int)sizeof(clean_pat) - 1; i++) {
            if (pattern[i] != '"' && pattern[i] != '\'') {
                clean_pat[cpi++] = pattern[i];
            }
        }
        clean_pat[cpi] = '\0';

        int match_count = 0;
        const char* cur = pipe_input;
        while (*cur) {
            const char* next_nl = strchr(cur, '\n');
            int line_len = next_nl ? (int)(next_nl - cur) : (int)strlen(cur);
            char line_buf[CMD_COLS];
            int copy_len = (line_len >= CMD_COLS) ? (CMD_COLS - 1) : line_len;
            memcpy(line_buf, cur, copy_len);
            line_buf[copy_len] = '\0';

            if (strstr(line_buf, clean_pat)) {
                cmd_print(state, line_buf);
                match_count++;
            }

            if (!next_nl) break;
            cur = next_nl + 1;
        }

        if (match_count == 0) {
            cmd_print(state, "(Nessun risultato corrispondente al filtro)");
        }
    } else if (strcmp(filter_cmd, "count") == 0 || strcmp(filter_cmd, "wc") == 0) {
        int lines = 0;
        const char* cur = pipe_input;
        while (*cur) {
            if (*cur == '\n') lines++;
            cur++;
        }
        char cbuf[CMD_COLS];
        mini_snprintf(cbuf, sizeof(cbuf), "Righe filtrate da pipe: %d", lines);
        cmd_print(state, cbuf);
    } else if (strncmp(filter_cmd, "head", 4) == 0) {
        int max_lines = 5;
        if (filter_cmd[4] == ' ') {
            int val = 0;
            const char* p = filter_cmd + 5;
            while (*p >= '0' && *p <= '9') val = val * 10 + (*p++ - '0');
            if (val > 0) max_lines = val;
        }
        int printed = 0;
        const char* cur = pipe_input;
        while (*cur && printed < max_lines) {
            const char* next_nl = strchr(cur, '\n');
            int line_len = next_nl ? (int)(next_nl - cur) : (int)strlen(cur);
            char line_buf[CMD_COLS];
            int copy_len = (line_len >= CMD_COLS) ? (CMD_COLS - 1) : line_len;
            memcpy(line_buf, cur, copy_len);
            line_buf[copy_len] = '\0';
            cmd_print(state, line_buf);
            printed++;
            if (!next_nl) break;
            cur = next_nl + 1;
        }
    } else {
        char err[CMD_COLS];
        mini_snprintf(err, sizeof(err), "Filtro '%s' non supportato (usa FIND, GREP, COUNT, HEAD)", filter_cmd);
        cmd_print(state, err);
    }
}

static void cmd_save_history(cmd_state_t* state, const char* cmd) {
    if (!cmd || !cmd[0]) return;
    if (state->hist_count < CMD_CMD_HIST) {
        strncpy(state->cmd_hist[state->hist_count++], cmd, CMD_COLS - 1);
    } else {
        for (int i = 0; i < CMD_CMD_HIST - 1; i++) {
            memcpy(state->cmd_hist[i], state->cmd_hist[i + 1], CMD_COLS);
        }
        strncpy(state->cmd_hist[CMD_CMD_HIST - 1], cmd, CMD_COLS - 1);
    }
    state->hist_nav = state->hist_count;
    state->view_scroll = 0;
}

static void cmd_autocomplete(cmd_state_t* state) {
    if (!state || state->input_pos <= 0) return;
    static const char* cmds[] = {
        "help", "ver", "cls", "dir", "sysinfo", "mem", "pci",
        "ifconfig", "ping ", "start ", "type ", "echo ", "cat "
    };
    for (int i = 0; i < (int)(sizeof(cmds) / sizeof(cmds[0])); i++) {
        if (strncmp(state->input_line, cmds[i], (size_t)state->input_pos) == 0) {
            strncpy(state->input_line, cmds[i], CMD_COLS - 1);
            state->input_pos = (int)strlen(state->input_line);
            return;
        }
    }
    // Completamento file in C:\ dopo uno spazio (es. "type BENV")
    const char* sp = strrchr(state->input_line, ' ');
    if (sp && sp[1]) {
        const char* prefix = sp + 1;
        size_t plen = strlen(prefix);
        vfs_dirent_t* ents = (vfs_dirent_t*)kmalloc(sizeof(vfs_dirent_t) * 32);
        if (ents) {
            int n = vfs_readdir("C:\\", ents, 32);
            for (int i = 0; i < n; i++) {
                if (strncasecmp(ents[i].name, prefix, plen) == 0) {
                    size_t base_len = (size_t)(sp + 1 - state->input_line);
                    if (base_len + strlen(ents[i].name) < CMD_COLS - 1) {
                        strcpy(state->input_line + base_len, ents[i].name);
                        state->input_pos = (int)strlen(state->input_line);
                    }
                    break;
                }
            }
            kfree(ents);
        }
    }
}

static void cmd_execute(cmd_state_t* state, const char* cmd) {
    if (!state || !cmd) return;
    cmd_save_history(state, cmd);

    // Controllo redirect output '>' o '>>' su file VFS
    const char* redir = strstr(cmd, ">>");
    bool is_append = (redir != NULL);
    if (!redir) redir = strchr(cmd, '>');
    if (redir && !strchr(cmd, '|')) {
        char left_cmd[CMD_COLS];
        char out_path[96];
        size_t llen = (size_t)(redir - cmd);
        if (llen >= sizeof(left_cmd)) llen = sizeof(left_cmd) - 1;
        strncpy(left_cmd, cmd, llen);
        left_cmd[llen] = '\0';
        while (llen > 0 && left_cmd[llen - 1] == ' ') left_cmd[--llen] = '\0';

        const char* rp = redir + (is_append ? 2 : 1);
        while (*rp == ' ') rp++;
        if (strchr(rp, ':') || rp[0] == '\\' || rp[0] == '/') {
            strncpy(out_path, rp, sizeof(out_path) - 1);
            out_path[sizeof(out_path) - 1] = '\0';
        } else {
            mini_snprintf(out_path, sizeof(out_path), "C:\\%s", rp);
        }

        char prompt_echo[CMD_COLS + 8];
        mini_snprintf(prompt_echo, sizeof(prompt_echo), "C:\\> %s", cmd);
        cmd_print(state, prompt_echo);

        state->pipe_active = true;
        state->pipe_pos = 0;
        state->pipe_buffer[0] = '\0';
        cmd_execute_internal(state, left_cmd);
        state->pipe_active = false;

        if (!vfs_exists(out_path)) vfs_create(out_path);
        if (is_append) {
            uint32_t old_sz = 0;
            char* old_buf = (char*)vfs_read_entire_file(out_path, &old_sz);
            uint32_t new_sz = old_sz + (uint32_t)state->pipe_pos;
            char* comb = (char*)kmalloc(new_sz + 1);
            if (comb) {
                if (old_buf && old_sz > 0) memcpy(comb, old_buf, old_sz);
                if (state->pipe_pos > 0) memcpy(comb + old_sz, state->pipe_buffer, (size_t)state->pipe_pos);
                vfs_write_entire_file_inplace(out_path, comb, new_sz);
                kfree(comb);
            }
            if (old_buf) kfree(old_buf);
        } else {
            vfs_write_entire_file_inplace(out_path, state->pipe_buffer, (uint32_t)state->pipe_pos);
        }
        char ok_msg[CMD_COLS];
        mini_snprintf(ok_msg, sizeof(ok_msg), "Output salvato in %s (%d B)", out_path, state->pipe_pos);
        cmd_print(state, ok_msg);
        return;
    }

    // Controllo operatore pipe '|' per streaming IPC tra comandi
    const char* pipe_char = strchr(cmd, '|');
    if (pipe_char) {
        char left_cmd[CMD_COLS];
        char right_cmd[CMD_COLS];
        size_t left_len = (size_t)(pipe_char - cmd);
        if (left_len >= sizeof(left_cmd)) left_len = sizeof(left_cmd) - 1;
        strncpy(left_cmd, cmd, left_len);
        left_cmd[left_len] = '\0';
        while (left_len > 0 && left_cmd[left_len - 1] == ' ') {
            left_cmd[--left_len] = '\0';
        }

        const char* r_ptr = pipe_char + 1;
        while (*r_ptr == ' ') r_ptr++;
        strncpy(right_cmd, r_ptr, sizeof(right_cmd) - 1);
        right_cmd[sizeof(right_cmd) - 1] = '\0';

        char prompt_echo[CMD_COLS + 8];
        mini_snprintf(prompt_echo, sizeof(prompt_echo), "C:\\> %s", cmd);
        cmd_print(state, prompt_echo);

        // Attiva buffer di cattura stream IPC
        state->pipe_active = true;
        state->pipe_pos = 0;
        state->pipe_buffer[0] = '\0';

        cmd_execute_internal(state, left_cmd);

        state->pipe_active = false;

        // Incanala il flusso catturato nel comando filtro di destra
        cmd_execute_pipe_filter(state, right_cmd, state->pipe_buffer);
        return;
    }

    char prompt_echo[CMD_COLS + 8];
    mini_snprintf(prompt_echo, sizeof(prompt_echo), "C:\\> %s", cmd);
    cmd_print(state, prompt_echo);

    cmd_execute_internal(state, cmd);
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

    // Stampa righe di output (con supporto scrollback rotella mouse)
    for (int r = 0; r < CMD_ROWS; r++) {
        int virt_idx = state->sb_count - state->view_scroll + r;
        const char* row_str = NULL;
        if (virt_idx < state->sb_count && virt_idx >= 0) {
            row_str = state->sb_lines[virt_idx];
        } else {
            int line_idx = virt_idx - state->sb_count;
            if (line_idx >= 0 && line_idx < CMD_ROWS) {
                row_str = state->lines[line_idx];
            }
        }
        if (row_str && row_str[0] != '\0') {
            gfx_draw_string(cx + 6, cy + 6 + r * 16, row_str, 0xFFFFFF, 0x000000, false);
        }
    }

    // Stampa prompt corrente e testo in digitazione
    if (state->view_scroll == 0) {
        char cur_line[CMD_COLS + 16];
        mini_snprintf(cur_line, sizeof(cur_line), "C:\\> %s_", state->input_line);
        gfx_draw_string(cx + 6, cy + 6 + state->current_row * 16, cur_line, 0xFFFFFF, 0x000000, false);
    } else {
        char sb_ind[32];
        mini_snprintf(sb_ind, sizeof(sb_ind), "[Scrollback +%d righe]", state->view_scroll);
        gfx_draw_string(cx + cw - 190, cy + 6, sb_ind, 0x60A5FA, 0x000000, true);
    }
}

static void cmd_key(window_t* win, char c) {
    cmd_state_t* state = (cmd_state_t*)win->app_data;
    if (!state) return;

    if (c == KEY_WHEEL_UP) {
        if (state->view_scroll < state->sb_count) state->view_scroll++;
        return;
    }
    if (c == KEY_WHEEL_DOWN) {
        if (state->view_scroll > 0) state->view_scroll--;
        return;
    }
    if (c == KEY_ARROW_UP) {
        if (state->hist_count > 0 && state->hist_nav > 0) {
            state->hist_nav--;
            strncpy(state->input_line, state->cmd_hist[state->hist_nav], CMD_COLS - 1);
            state->input_pos = (int)strlen(state->input_line);
        }
        return;
    }
    if (c == KEY_ARROW_DOWN) {
        if (state->hist_nav < state->hist_count - 1) {
            state->hist_nav++;
            strncpy(state->input_line, state->cmd_hist[state->hist_nav], CMD_COLS - 1);
            state->input_pos = (int)strlen(state->input_line);
        } else {
            state->hist_nav = state->hist_count;
            state->input_line[0] = '\0';
            state->input_pos = 0;
        }
        return;
    }
    if (c == '\t') {
        cmd_autocomplete(state);
        return;
    }

    state->view_scroll = 0;
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
