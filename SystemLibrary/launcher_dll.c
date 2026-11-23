#define DLL_NO_LIBC_ALIAS 1
#include <launcher.h>
#include <gui/uistyle_loader.h>
#include <screen_modules.h>
#include "dll_libc.h"
#include "dll_ext_macros.h"

__attribute__((section(".header"), used)) module_header_t g_launcher_header = {
    .magic          = MODULE_MAGIC_NEWOS,
    .header_size    = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type    = MODULE_TYPE_SYSTEM_LIBRARY,
    .code_size      = 0,
    .entry_offset   = 64,
    .checksum       = 0,
    .security_flags = SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name    = "LAUNCHER",
    .signature      = MODULE_AUTH_TAG
};

static int launcher_dispatch(const kernel_api_t* api, launcher_params_t* p);

// Entry point garantito all'offset 64 (subito dopo .header)
__attribute__((section(".text.entry"), used))
int module_main(const kernel_api_t* api, launcher_params_t* p) {
    g_dll_api = api;
    return launcher_dispatch(api, p);
}

static const char* get_ext(const char* path) {
    if (!path) return NULL;
    const char* dot = NULL;
    for (const char* s = path; *s; s++) {
        if (*s == '/' || *s == '\\') dot = NULL;
        else if (*s == '.') dot = s;
    }
    return dot;
}

static const char* get_basename(const char* path) {
    if (!path) return "";
    const char* last = path;
    for (const char* s = path; *s; s++) {
        if (*s == '/' || *s == '\\') last = s + 1;
    }
    return last;
}

static void set_handler(launcher_params_t* p, const char* h) {
    if (!p || !h) return;
    strcpy(p->resolved_handler, h);
}

static bool launch_by_alias(launcher_params_t* p, const char* name, const char* args) {
    if (!name || !name[0]) return false;

    if (strcasecmp(name, "thispc") == 0 || strcasecmp(name, "computer") == 0 ||
        strcasecmp(name, "risorse") == 0) {
        if (!(uistyle_get_active() == UISTYLE_COPENHAGEN && uistyle_open_explorer(NULL))) {
            app_explorer_open_this_pc();
        }
        set_handler(p, "EXPLORER.DLL (Risorse PC)");
        return true;
    }
    if (strcasecmp(name, "explorer") == 0) {
        if (args && args[0]) app_explorer_open_folder(args);
        else if (!(uistyle_get_active() == UISTYLE_COPENHAGEN && uistyle_open_explorer(NULL))) {
            app_explorer_open_this_pc();
        }
        set_handler(p, "EXPLORER.DLL");
        return true;
    }
    if (strcasecmp(name, "recycle") == 0 || strcasecmp(name, "cestino") == 0) {
        app_explorer_open_recycle_bin();
        set_handler(p, "EXPLORER.DLL (Cestino)");
        return true;
    }
    if (strcasecmp(name, "cmd") == 0 || strcasecmp(name, "prompt") == 0) {
        app_cmd_launch();
        set_handler(p, "SYSAPPS.DLL (CMD)");
        return true;
    }
    if (strcasecmp(name, "notepad") == 0 || strcasecmp(name, "blocconote") == 0) {
        if (args && args[0]) app_notepad_open_file(get_basename(args), args);
        else app_notepad_launch();
        set_handler(p, "SYSAPPS.DLL (Notepad)");
        return true;
    }
    if (strcasecmp(name, "calc") == 0 || strcasecmp(name, "calcolatrice") == 0) {
        app_calc_launch();
        set_handler(p, "SYSAPPS.DLL (Calc)");
        return true;
    }
    if (strcasecmp(name, "player") == 0 || strcasecmp(name, "mediaplayer") == 0) {
        app_player_launch();
        set_handler(p, "SYSAPPS.DLL (MediaPlayer)");
        return true;
    }
    if (strcasecmp(name, "browser") == 0 || strcasecmp(name, "webbrowser") == 0 ||
        strcasecmp(name, "iexplore") == 0) {
        if (args && args[0]) app_browser_open_url(args);
        else app_browser_launch();
        set_handler(p, "WEBBROWSER.DLL");
        return true;
    }
    if (strcasecmp(name, "taskmgr") == 0) {
        app_taskmgr_launch();
        set_handler(p, "SYSAPPS.DLL (TaskMgr)");
        return true;
    }
    if (strcasecmp(name, "regedit") == 0) {
        app_regedit_launch();
        set_handler(p, "SYSAPPS.DLL (Regedit)");
        return true;
    }
    if (strcasecmp(name, "control") == 0 || strcasecmp(name, "controlpanel") == 0) {
        app_control_panel_open();
        set_handler(p, "CPLEXT2.DLL (ControlPanel)");
        return true;
    }
    if (strcasecmp(name, "about") == 0 || strcasecmp(name, "infosistema") == 0) {
        app_about_launch();
        set_handler(p, "CPLEXT2.DLL (About)");
        return true;
    }
    if (strcasecmp(name, "rules") == 0 || strcasecmp(name, "rulesviewer") == 0) {
        app_rules_viewer_launch();
        set_handler(p, "RULES.DLL");
        return true;
    }
    if (strcasecmp(name, "installer") == 0 || strcasecmp(name, "setup") == 0) {
        app_installer_launch();
        set_handler(p, "BOOTSETUP.MOD");
        return true;
    }
    if (strcasecmp(name, "display") == 0 || strcasecmp(name, "desk.cpl") == 0) {
        app_display_props_launch();
        set_handler(p, "CPLEXT2.DLL (DESK.CPL)");
        return true;
    }
    if (strcasecmp(name, "sound") == 0 || strcasecmp(name, "mmsys.cpl") == 0) {
        app_sound_props_launch();
        set_handler(p, "CPLEXT2.DLL (MMSYS.CPL)");
        return true;
    }
    if (strcasecmp(name, "mouse") == 0 || strcasecmp(name, "main.cpl") == 0) {
        app_mouse_props_launch();
        set_handler(p, "CPLEXT2.DLL (MAIN.CPL)");
        return true;
    }
    if (strcasecmp(name, "power") == 0 || strcasecmp(name, "powercfg.cpl") == 0) {
        app_power_props_launch();
        set_handler(p, "CPLEXT2.DLL (POWERCFG.CPL)");
        return true;
    }
    if (strcasecmp(name, "devmgmt") == 0 || strcasecmp(name, "devmgmt.cpl") == 0) {
        app_devmgmt_launch();
        set_handler(p, "CPLEXT2.DLL (DEVMGMT.CPL)");
        return true;
    }
    if (strcasecmp(name, "useracct") == 0 || strcasecmp(name, "nusrmgr.cpl") == 0) {
        app_user_accounts_launch();
        set_handler(p, "USERACCT.DLL");
        return true;
    }
    if (strcasecmp(name, "timedate") == 0 || strcasecmp(name, "timedate.cpl") == 0) {
        app_timedate_props_launch();
        set_handler(p, "TIMEDATE.DLL");
        return true;
    }
    if (strcasecmp(name, "netcpl") == 0 || strcasecmp(name, "ncpa.cpl") == 0) {
        app_net_props_launch();
        set_handler(p, "NETCPL.DLL");
        return true;
    }
    if (strcasecmp(name, "keybcpl") == 0) {
        app_keyboard_props_launch();
        set_handler(p, "KEYBCPL.DLL");
        return true;
    }
    if (strcasecmp(name, "fontscpl") == 0 || strcasecmp(name, "fonts.cpl") == 0 ||
        strcasecmp(name, "fonts") == 0 || strcasecmp(name, "font") == 0) {
        app_fonts_props_launch();
        set_handler(p, "FONTSCPL.DLL");
        return true;
    }
    if (strcasecmp(name, "pkgsvc") == 0 || strcasecmp(name, "programs") == 0 ||
        strcasecmp(name, "installapp") == 0) {
        app_pkgsvc_launch();
        set_handler(p, "PKGSVC.DLL");
        return true;
    }
    if (strcasecmp(name, "ports") == 0 || strcasecmp(name, "ports.cpl") == 0 ||
        strcasecmp(name, "com") == 0 || strcasecmp(name, "lpt") == 0) {
        app_ports_props_launch();
        set_handler(p, "PORTS.DLL");
        return true;
    }
    if (strcasecmp(name, "bench") == 0 || strcasecmp(name, "bench.cpl") == 0 ||
        strcasecmp(name, "diag") == 0) {
        app_bench_props_launch();
        set_handler(p, "BENCH.DLL");
        return true;
    }
    if (strcasecmp(name, "imgview") == 0 || strcasecmp(name, "foto") == 0 ||
        strcasecmp(name, "immagini") == 0) {
        app_imgview_props_launch();
        set_handler(p, "IMGVIEW.DLL");
        return true;
    }
    if (strcasecmp(name, "seccpl") == 0 || strcasecmp(name, "secpol.cpl") == 0 ||
        strcasecmp(name, "security") == 0 || strcasecmp(name, "sicurezza") == 0) {
        app_seccpl_launch();
        set_handler(p, "SECCPL.DLL (Centro Sicurezza)");
        return true;
    }
    return false;
}

static bool launch_file_by_ext(launcher_params_t* p, const char* path, const char* content_hint) {
    const char* ext = get_ext(path);
    const char* bname = get_basename(path);

    // 1. Documenti testuali e configurazioni -> Blocco Note
    if (ext && (strcasecmp(ext, ".txt") == 0 || strcasecmp(ext, ".ini") == 0 ||
                strcasecmp(ext, ".log") == 0 || strcasecmp(ext, ".cfg") == 0 ||
                strcasecmp(ext, ".md") == 0  || strcasecmp(ext, ".c") == 0   ||
                strcasecmp(ext, ".h") == 0)) {
        if (content_hint) {
            app_notepad_open_file(bname, content_hint);
            set_handler(p, "SYSAPPS.DLL (Notepad)");
            return true;
        }
        uint32_t sz = 0;
        char* file_buf = (char*)vfs_read_entire_file(path, &sz);
        if (file_buf) {
            char* text = (char*)kmalloc(sz + 1);
            if (text) {
                memcpy(text, file_buf, sz);
                text[sz] = '\0';
                app_notepad_open_file(bname, text);
                kfree(text);
            }
            kfree(file_buf);
            set_handler(p, "SYSAPPS.DLL (Notepad)");
            return true;
        }
        app_notepad_open_file(bname, "");
        set_handler(p, "SYSAPPS.DLL (Notepad)");
        return true;
    }

    // 2. Documenti ufficiali .NOR -> Rules Viewer
    if (ext && strcasecmp(ext, ".nor") == 0) {
        app_rules_viewer_open_file(path);
        set_handler(p, "RULES.DLL");
        return true;
    }

    // 2a. Archivi .TAR / .ZIP -> Esplora Risorse come cartella virtuale
    if (ext && (strcasecmp(ext, ".tar") == 0 || strcasecmp(ext, ".zip") == 0)) {
        app_explorer_open_folder(path);
        set_handler(p, "EXPLORER.DLL (Archivio)");
        return true;
    }

    // 2b. Collegamenti (.LNK) -> Risoluzione Target= / Args= con protezione loop
    if (ext && strcasecmp(ext, ".lnk") == 0) {
        static int lnk_depth = 0;
        if (lnk_depth >= 3) return false;
        char raw_buf[512];
        const char* src_txt = content_hint;
        char* alloc_buf = NULL;
        if (!src_txt) {
            uint32_t sz = 0;
            alloc_buf = (char*)vfs_read_entire_file(path, &sz);
            if (alloc_buf) {
                uint32_t cpy = (sz < sizeof(raw_buf) - 1) ? sz : (sizeof(raw_buf) - 1);
                memcpy(raw_buf, alloc_buf, cpy);
                raw_buf[cpy] = '\0';
                kfree(alloc_buf);
                src_txt = raw_buf;
            }
        }
        if (src_txt) {
            char target_buf[128] = "";
            char args_buf[128] = "";
            const char* ln = src_txt;
            while (*ln) {
                const char* eol = ln;
                while (*eol && *eol != '\n' && *eol != '\r') eol++;
                int len = (int)(eol - ln);
                if (len > 7 && strncasecmp(ln, "Target=", 7) == 0) {
                    int c = len - 7;
                    if (c > (int)sizeof(target_buf) - 1) c = sizeof(target_buf) - 1;
                    memcpy(target_buf, ln + 7, c);
                    target_buf[c] = '\0';
                } else if (len > 5 && strncasecmp(ln, "Args=", 5) == 0) {
                    int c = len - 5;
                    if (c > (int)sizeof(args_buf) - 1) c = sizeof(args_buf) - 1;
                    memcpy(args_buf, ln + 5, c);
                    args_buf[c] = '\0';
                }
                ln = eol;
                while (*ln == '\n' || *ln == '\r') ln++;
            }
            if (target_buf[0]) {
                lnk_depth++;
                bool ok = launch_by_alias(p, target_buf, args_buf[0] ? args_buf : NULL);
                if (!ok) ok = launch_file_by_ext(p, target_buf, NULL);
                if (!ok && (strchr(target_buf, '\\') || strchr(target_buf, '/'))) {
                    app_explorer_open_folder(target_buf);
                    set_handler(p, "EXPLORER.DLL (Shortcut)");
                    ok = true;
                }
                lnk_depth--;
                return ok;
            }
        }
        return false;
    }

    // 3. Audio (.WAV, .MP3, .FLAC) -> Riproduzione / Media Player
    if (ext && (strcasecmp(ext, ".wav") == 0 || strcasecmp(ext, ".mp3") == 0 ||
                strcasecmp(ext, ".flac") == 0)) {
        app_player_open_file(path);
        set_handler(p, "SYSAPPS.DLL / AUDIO.DLL");
        return true;
    }

    // 4. Pagine Web locali (.HTML, .HTM) -> Web Browser
    if (ext && (strcasecmp(ext, ".html") == 0 || strcasecmp(ext, ".htm") == 0)) {
        app_browser_open_url(path);
        set_handler(p, "WEBBROWSER.DLL");
        return true;
    }

    // 5. Immagini (.BMP, .JPG, .PNG) -> Applica come sfondo / viewer
    if (ext && (strcasecmp(ext, ".bmp") == 0 || strcasecmp(ext, ".jpg") == 0 ||
                strcasecmp(ext, ".jpeg") == 0 || strcasecmp(ext, ".png") == 0)) {
        image_t* img = image_load_from_file(path);
        if (img) {
            gfx_set_custom_wallpaper(img->pixels, img->width, img->height);
            image_free(img);
            set_handler(p, "IMGCODEC / Wallpaper");
            return true;
        }
    }

    // 6. Consulta Registro di Sistema (HKCR\.<ext>\shell\open\command)
    if (ext) {
        char reg_path[128];
        char cmd_val[128];
        mini_snprintf(reg_path, sizeof(reg_path), "HKCR\\%s\\shell\\open\\command", ext);
        if (registry_get_value_sz(reg_path, "Default", cmd_val, sizeof(cmd_val))) {
            if (launch_by_alias(p, cmd_val, path)) return true;
        }
    }

    // 7. Fallback generico per file sconosciuti esistenti: apre in Blocco Note se leggibile
    if (content_hint) {
        app_notepad_open_file(bname, content_hint);
        set_handler(p, "SYSAPPS.DLL (Notepad Fallback)");
        return true;
    }

    return false;
}

static int launcher_dispatch(const kernel_api_t* api, launcher_params_t* p) {
    if (!api || !p || !p->target || !p->target[0]) return -1;
    if (p->cmd != LAUNCHER_CMD_OPEN) return -1;

    const char* target = p->target;
    uint32_t type = p->type;
    bool ok = false;

    // 1. URL (http://, https://, about:)
    if (type == LAUNCH_TYPE_URL ||
        strncasecmp(target, "http://", 7) == 0 ||
        strncasecmp(target, "https://", 8) == 0 ||
        strncasecmp(target, "about:", 6) == 0) {
        app_browser_open_url(target);
        set_handler(p, "WEBBROWSER.DLL (URL)");
        ok = true;
    }
    // 2. Alias applicativi di sistema / applet CPL
    else if (type == LAUNCH_TYPE_APP || launch_by_alias(p, target, p->args)) {
        if (type == LAUNCH_TYPE_APP) ok = launch_by_alias(p, target, p->args);
        else ok = true;
    }
    // 3. Cartella esplicita o path terminante con '/' o '\'
    else if (type == LAUNCH_TYPE_FOLDER) {
        app_explorer_open_folder(target);
        set_handler(p, "EXPLORER.DLL (Folder)");
        ok = true;
    }
    // 4. File per estensione / contenuto / registro
    else {
        size_t len = strlen(target);
        if (len > 0 && (target[len - 1] == '\\' || target[len - 1] == '/')) {
            app_explorer_open_folder(target);
            set_handler(p, "EXPLORER.DLL (Folder)");
            ok = true;
        } else {
            ok = launch_file_by_ext(p, target, p->content_hint);
        }
    }

    p->result = ok ? 1 : 0;
    return ok ? 0 : -1;
}
