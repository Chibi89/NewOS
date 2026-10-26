/**
 * SystemLibrary/theme_mgr.c
 * Libreria Dinamica Out-of-Kernel per NewOS V1 (64-bit)
 * Scansione, decodifica ed ordinamento automatico temi in C:\NewOS\Resource\Themes
 * (C) Newspoint Software - NewSoft Dynamic System Library
 */

#include <screen_modules.h>
#include <gui/theme_mgr.h>

// Header di protezione del modulo (esattamente 64 byte all'inizio del file .DLL)
__attribute__((section(".header"), used))
module_header_t g_thememgr_header = {
    .magic          = MODULE_MAGIC_NEWOS,
    .header_size    = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type    = MODULE_TYPE_SYSTEM_LIBRARY,
    .code_size      = 0, // Popolato dal build system
    .entry_offset   = 64, // Posizione dell'entry point
    .checksum       = 0, // Popolato dal build system
    .security_flags = SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name    = "THEMEMGR",
    .signature      = MODULE_AUTH_TAG
};

// Struttura voce di directory conforme alla vfs_dirent_t del kernel (128 byte nome)
typedef struct {
    char     name[128];
    uint32_t size;
    uint16_t attributes;
    bool     is_directory;
} dll_vfs_dirent_t;

// --- Funzioni di Memoria Freestanding Richieste da LLVM/Clang Freestanding ---

void* memset(void* s, int c, size_t n) {
    unsigned char* p = (unsigned char*)s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

void* memcpy(void* dest, const void* src, size_t n) {
    char* d = (char*)dest;
    const char* s = (const char*)src;
    while (n--) *d++ = *s++;
    return dest;
}

void* memmove(void* dest, const void* src, size_t n) {
    char* d = (char*)dest;
    const char* s = (const char*)src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        char* lasts = (char*)s + (n - 1);
        char* lastd = d + (n - 1);
        while (n--) *lastd-- = *lasts--;
    }
    return dest;
}

// --- Helper Locali di Stringa (Zero LibC) ---

static size_t local_strlen(const char* s) {
    if (!s) return 0;
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

static char* local_strcpy(char* dest, const char* src) {
    if (!dest) return NULL;
    char* d = dest;
    if (src) {
        while ((*d++ = *src++));
    } else {
        *d = '\0';
    }
    return dest;
}

static char* local_strncpy(char* dest, const char* src, size_t n) {
    if (!dest) return NULL;
    size_t i;
    for (i = 0; i < n && src && src[i]; i++) {
        dest[i] = src[i];
    }
    for (; i < n; i++) {
        dest[i] = '\0';
    }
    return dest;
}

static int local_strcmp(const char* s1, const char* s2) {
    if (!s1 && !s2) return 0;
    if (!s1) return -1;
    if (!s2) return 1;
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static int local_strcasecmp(const char* s1, const char* s2) {
    if (!s1 && !s2) return 0;
    if (!s1) return -1;
    if (!s2) return 1;
    while (*s1 && *s2) {
        char c1 = *s1;
        char c2 = *s2;
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        if (c1 != c2) return (unsigned char)c1 - (unsigned char)c2;
        s1++;
        s2++;
    }
    char c1 = *s1;
    char c2 = *s2;
    if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
    if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
    return (unsigned char)c1 - (unsigned char)c2;
}

static const char* local_strstr(const char* haystack, const char* needle) {
    if (!haystack || !needle) return NULL;
    if (!*needle) return haystack;
    for (; *haystack; haystack++) {
        if (*haystack == *needle) {
            const char* h = haystack;
            const char* n = needle;
            while (*h && *n && *h == *n) {
                h++;
                n++;
            }
            if (!*n) return haystack;
        }
    }
    return NULL;
}

static char* local_strchr(const char* s, int c) {
    if (!s) return NULL;
    while (*s) {
        if (*s == (char)c) return (char*)s;
        s++;
    }
    return (c == 0) ? (char*)s : NULL;
}

static const char* local_strrchr(const char* s, int c) {
    if (!s) return NULL;
    const char* last = NULL;
    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    if (c == '\0') return s;
    return last;
}

static void local_make_path(char* out, size_t max, const char* dir, const char* file) {
    if (!out || max == 0) return;
    out[0] = '\0';
    local_strncpy(out, dir, max - 1);
    size_t len = local_strlen(out);
    if (len > 0 && out[len - 1] != '/' && out[len - 1] != '\\' && len + 1 < max) {
        out[len++] = '/';
        out[len] = '\0';
    }
    local_strncpy(out + len, file, max - len - 1);
    out[max - 1] = '\0';
}

static void log_msg(const kernel_api_t* api, const char* msg) {
    if (api && api->serial_puts && msg) {
        api->serial_puts(msg);
    }
}

// --- Parsing File Temi ---

static uint32_t parse_hex_color(const char* str) {
    if (!str) return 0;
    while (*str == ' ' || *str == '\t') str++;
    if (*str == '#' || (*str == '0' && (str[1] == 'x' || str[1] == 'X'))) {
        if (*str == '#') str++;
        else str += 2;
        uint32_t val = 0;
        while (*str) {
            char c = *str++;
            uint32_t nibble = 0;
            if (c >= '0' && c <= '9') nibble = c - '0';
            else if (c >= 'a' && c <= 'f') nibble = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') nibble = c - 'A' + 10;
            else break;
            val = (val << 4) | nibble;
        }
        return val;
    }
    uint32_t val = 0;
    while (*str >= '0' && *str <= '9') {
        val = val * 10 + (*str++ - '0');
    }
    return val;
}

static int parse_int_val(const char* str) {
    if (!str) return 0;
    while (*str == ' ' || *str == '\t') str++;
    int val = 0;
    while (*str >= '0' && *str <= '9') {
        val = val * 10 + (*str++ - '0');
    }
    return val;
}

static void trim_line(char* line) {
    if (!line) return;
    int len = (int)local_strlen(line);
    while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n' ||
                       line[len - 1] == ' '  || line[len - 1] == '\t')) {
        line[--len] = '\0';
    }
    char* p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (p != line) {
        memmove(line, p, local_strlen(p) + 1);
    }
}

static bool parse_theme_file(const kernel_api_t* api, const char* full_path, const char* filename, theme_info_t* out) {
    if (!api || !api->vfs_read_entire_file || !full_path || !out) return false;
    memset(out, 0, sizeof(theme_info_t));
    local_strncpy(out->filename, filename, sizeof(out->filename) - 1);

    local_strncpy(out->name, filename, sizeof(out->name) - 1);
    local_strcpy(out->description, "Tema Visivo di Sistema");
    local_strcpy(out->author, "NewOS Team");
    local_strcpy(out->version, "1.0");
    local_strcpy(out->style_engine, "Modern");

    // Valori predefiniti sicuri e armoniosi
    out->surface_alpha = 235;
    out->taskbar_alpha = 225;
    out->corner_radius = 8;
    out->title_height = 22;
    out->border_width = 1;
    out->drop_shadow = true;
    out->shadow_spread = 8;
    out->specular_rim = true;
    out->modern_controls = true;
    out->control_radius = 4;
    out->control_border = 1;
    out->desktop_color = 0x1E2A38;
    out->surface_base = 0xF8F9FA;
    out->active_title = 0xF8F9FA;
    out->active_title_text = 0x1E1E1E;
    out->inactive_title = 0xEAECEF;
    out->inactive_title_text = 0x757575;
    out->window_bg = 0xFFFFFF;
    out->window_text = 0x1E1E1E;
    out->btn_face = 0xF0F2F5;
    out->btn_text = 0x1E1E1E;
    out->btn_hover = 0xE4E8EC;
    out->border_color = 0xE2E2E2;
    out->border_inactive = 0xD0D0D0;
    out->control_border_color = 0xD4D4D8;
    out->accent_color = 0x0078D4;
    out->accent_text = 0xFFFFFF;
    out->selection_color = 0xCCE8FF;
    out->selection_text = 0x004578;
    out->close_hover_bg = 0xE81123;
    out->taskbar_color = 0xF3F3F7;
    out->taskbar_height = 28;
    out->taskbar_border = 0xE5E5E5;
    out->start_btn_color = 0xEAEAEA;
    out->start_btn_text = 0x1E1E1E;
    out->active_tab_indicator = 0x0078D4;
    out->tray_pill_color = 0xEAEAEA;
    out->theme_id = 1;

    if (local_strstr(filename, "CLASSIC") || local_strstr(filename, "TEAL")) {
        local_strcpy(out->name, "Windows 2000 Classic");
        local_strcpy(out->description, "Bevel 3D e verde acqua Teal");
        local_strcpy(out->style_engine, "Classic3D");
        out->surface_base = 0xD4D0C8;
        out->active_title = 0x0A246A;
        out->active_title_text = 0xFFFFFF;
        out->inactive_title = 0x808080;
        out->inactive_title_text = 0xD4D0C8;
        out->accent_color = 0x0A246A;
        out->border_color = 0x808080;
        out->border_inactive = 0x808080;
        out->btn_face = 0xD4D0C8;
        out->btn_text = 0x000000;
        out->window_bg = 0xFFFFFF;
        out->window_text = 0x000000;
        out->surface_alpha = 255;
        out->taskbar_alpha = 255;
        out->taskbar_color = 0xD4D0C8;
        out->corner_radius = 0;
        out->title_height = 18;
        out->border_width = 2;
        out->drop_shadow = false;
        out->shadow_spread = 0;
        out->specular_rim = false;
        out->modern_controls = false;
        out->control_radius = 0;
        out->button_radius = 0xFF;
        out->tab_radius = 0xFF;
        out->input_radius = 0xFF;
        out->panel_radius = 0xFF;
        out->taskbar_btn_radius = 0xFF;
        out->start_btn_radius = 0xFF;
        out->tray_radius = 0xFF;
        out->border_follows_corner = true;
        out->border_radius = 0xFF;
        out->control_border_follows_radius = true;
        out->button_border_radius = 0xFF;
        out->tab_border_radius = 0xFF;
        out->input_border_radius = 0xFF;
        out->panel_border_radius = 0xFF;
        out->taskbar_border_radius = 0xFF;
        out->start_btn_border_radius = 0xFF;
        out->tray_border_radius = 0xFF;
        out->theme_id = 0;
    }

    uint32_t fsize = 0;
    char* content = (char*)api->vfs_read_entire_file(full_path, &fsize);
    if (!content || fsize == 0) {
        if (content && api->kfree) api->kfree(content);
        return true;
    }

    // Parser INI riga per riga sicuro (copia in line_buf per non alterare content)
    const char* cur = content;
    const char* end = content + fsize;
    char line_buf[128];

    while (cur < end && *cur) {
        int idx = 0;
        while (cur < end && *cur && *cur != '\n' && idx < (int)sizeof(line_buf) - 1) {
            line_buf[idx++] = *cur++;
        }
        if (cur < end && *cur == '\n') cur++;
        line_buf[idx] = '\0';
        trim_line(line_buf);

        if (line_buf[0] == '\0' || line_buf[0] == ';' || line_buf[0] == '#' || line_buf[0] == '[') {
            continue;
        }

        char* eq = local_strchr(line_buf, '=');
        if (!eq) continue;
        *eq = '\0';
        char* key = line_buf;
        char* val = eq + 1;
        trim_line(key);
        trim_line(val);

        if (local_strcasecmp(key, "Name") == 0) {
            local_strncpy(out->name, val, sizeof(out->name) - 1);
        } else if (local_strcasecmp(key, "Description") == 0) {
            local_strncpy(out->description, val, sizeof(out->description) - 1);
        } else if (local_strcasecmp(key, "Author") == 0) {
            local_strncpy(out->author, val, sizeof(out->author) - 1);
        } else if (local_strcasecmp(key, "Version") == 0) {
            local_strncpy(out->version, val, sizeof(out->version) - 1);
        } else if (local_strcasecmp(key, "StyleEngine") == 0) {
            local_strncpy(out->style_engine, val, sizeof(out->style_engine) - 1);
        } else if (local_strcasecmp(key, "CornerRadius") == 0 || local_strcasecmp(key, "WindowRadius") == 0) {
            out->corner_radius = (uint8_t)parse_int_val(val);
        } else if (local_strcasecmp(key, "ButtonRadius") == 0) {
            out->button_radius = (uint8_t)parse_int_val(val);
        } else if (local_strcasecmp(key, "TabRadius") == 0) {
            out->tab_radius = (uint8_t)parse_int_val(val);
        } else if (local_strcasecmp(key, "InputRadius") == 0) {
            out->input_radius = (uint8_t)parse_int_val(val);
        } else if (local_strcasecmp(key, "PanelRadius") == 0) {
            out->panel_radius = (uint8_t)parse_int_val(val);
        } else if (local_strcasecmp(key, "StartBtnRadius") == 0) {
            out->start_btn_radius = (uint8_t)parse_int_val(val);
        } else if (local_strcasecmp(key, "TaskbarBtnRadius") == 0 || local_strcasecmp(key, "TaskbarRadius") == 0) {
            out->taskbar_btn_radius = (uint8_t)parse_int_val(val);
        } else if (local_strcasecmp(key, "TrayRadius") == 0) {
            out->tray_radius = (uint8_t)parse_int_val(val);
        } else if (local_strcasecmp(key, "BorderFollowsCorner") == 0 || local_strcasecmp(key, "BorderCurvature") == 0) {
            out->border_follows_corner = (parse_int_val(val) != 0);
        } else if (local_strcasecmp(key, "BorderRadius") == 0 || local_strcasecmp(key, "WindowBorderRadius") == 0) {
            out->border_radius = (uint8_t)parse_int_val(val);
        } else if (local_strcasecmp(key, "ControlBorderFollowsRadius") == 0 || local_strcasecmp(key, "ControlBorderRounded") == 0) {
            out->control_border_follows_radius = (parse_int_val(val) != 0);
        } else if (local_strcasecmp(key, "ButtonBorderRadius") == 0) {
            out->button_border_radius = (uint8_t)parse_int_val(val);
        } else if (local_strcasecmp(key, "TabBorderRadius") == 0) {
            out->tab_border_radius = (uint8_t)parse_int_val(val);
        } else if (local_strcasecmp(key, "InputBorderRadius") == 0) {
            out->input_border_radius = (uint8_t)parse_int_val(val);
        } else if (local_strcasecmp(key, "PanelBorderRadius") == 0) {
            out->panel_border_radius = (uint8_t)parse_int_val(val);
        } else if (local_strcasecmp(key, "TaskbarBorderRadius") == 0 || local_strcasecmp(key, "TaskbarBtnBorderRadius") == 0) {
            out->taskbar_border_radius = (uint8_t)parse_int_val(val);
        } else if (local_strcasecmp(key, "StartBtnBorderRadius") == 0) {
            out->start_btn_border_radius = (uint8_t)parse_int_val(val);
        } else if (local_strcasecmp(key, "TrayBorderRadius") == 0) {
            out->tray_border_radius = (uint8_t)parse_int_val(val);
        } else if (local_strcasecmp(key, "TitleHeight") == 0) {
            out->title_height = (uint8_t)parse_int_val(val);
        } else if (local_strcasecmp(key, "BorderWidth") == 0) {
            out->border_width = (uint8_t)parse_int_val(val);
        } else if (local_strcasecmp(key, "DropShadow") == 0) {
            out->drop_shadow = (parse_int_val(val) != 0);
        } else if (local_strcasecmp(key, "ShadowSpread") == 0) {
            out->shadow_spread = (uint8_t)parse_int_val(val);
        } else if (local_strcasecmp(key, "SpecularRim") == 0) {
            out->specular_rim = (parse_int_val(val) != 0);
        } else if (local_strcasecmp(key, "ModernControls") == 0) {
            out->modern_controls = (parse_int_val(val) != 0);
        } else if (local_strcasecmp(key, "ControlRadius") == 0) {
            out->control_radius = (uint8_t)parse_int_val(val);
        } else if (local_strcasecmp(key, "ControlBorder") == 0) {
            out->control_border = (uint8_t)parse_int_val(val);
        } else if (local_strcasecmp(key, "DesktopColor") == 0 || local_strcasecmp(key, "Desktop") == 0) {
            out->desktop_color = parse_hex_color(val);
        } else if (local_strcasecmp(key, "SurfaceBase") == 0) {
            out->surface_base = parse_hex_color(val);
        } else if (local_strcasecmp(key, "SurfaceAlpha") == 0) {
            out->surface_alpha = (uint8_t)parse_int_val(val);
        } else if (local_strcasecmp(key, "ActiveTitle") == 0) {
            out->active_title = parse_hex_color(val);
        } else if (local_strcasecmp(key, "ActiveTitleText") == 0) {
            out->active_title_text = parse_hex_color(val);
        } else if (local_strcasecmp(key, "InactiveTitle") == 0) {
            out->inactive_title = parse_hex_color(val);
        } else if (local_strcasecmp(key, "InactiveTitleText") == 0) {
            out->inactive_title_text = parse_hex_color(val);
        } else if (local_strcasecmp(key, "WindowBg") == 0) {
            out->window_bg = parse_hex_color(val);
        } else if (local_strcasecmp(key, "WindowText") == 0) {
            out->window_text = parse_hex_color(val);
        } else if (local_strcasecmp(key, "BtnFace") == 0) {
            out->btn_face = parse_hex_color(val);
        } else if (local_strcasecmp(key, "BtnText") == 0) {
            out->btn_text = parse_hex_color(val);
        } else if (local_strcasecmp(key, "BtnHover") == 0) {
            out->btn_hover = parse_hex_color(val);
        } else if (local_strcasecmp(key, "BorderColor") == 0) {
            out->border_color = parse_hex_color(val);
        } else if (local_strcasecmp(key, "BorderInactive") == 0) {
            out->border_inactive = parse_hex_color(val);
        } else if (local_strcasecmp(key, "ControlBorderColor") == 0) {
            out->control_border_color = parse_hex_color(val);
        } else if (local_strcasecmp(key, "AccentColor") == 0) {
            out->accent_color = parse_hex_color(val);
        } else if (local_strcasecmp(key, "AccentText") == 0) {
            out->accent_text = parse_hex_color(val);
        } else if (local_strcasecmp(key, "SelectionColor") == 0) {
            out->selection_color = parse_hex_color(val);
        } else if (local_strcasecmp(key, "SelectionText") == 0) {
            out->selection_text = parse_hex_color(val);
        } else if (local_strcasecmp(key, "CloseHoverBg") == 0) {
            out->close_hover_bg = parse_hex_color(val);
        } else if (local_strcasecmp(key, "TaskbarColor") == 0) {
            out->taskbar_color = parse_hex_color(val);
        } else if (local_strcasecmp(key, "TaskbarAlpha") == 0) {
            out->taskbar_alpha = (uint8_t)parse_int_val(val);
        } else if (local_strcasecmp(key, "TaskbarHeight") == 0) {
            out->taskbar_height = (uint8_t)parse_int_val(val);
        } else if (local_strcasecmp(key, "TaskbarBorder") == 0) {
            out->taskbar_border = parse_hex_color(val);
        } else if (local_strcasecmp(key, "StartBtnColor") == 0) {
            out->start_btn_color = parse_hex_color(val);
        } else if (local_strcasecmp(key, "StartBtnText") == 0) {
            out->start_btn_text = parse_hex_color(val);
        } else if (local_strcasecmp(key, "ActiveTabIndicator") == 0) {
            out->active_tab_indicator = parse_hex_color(val);
        } else if (local_strcasecmp(key, "TrayPillColor") == 0) {
            out->tray_pill_color = parse_hex_color(val);
        } else if (local_strcasecmp(key, "File") == 0 || local_strcasecmp(key, "Wallpaper") == 0) {
            local_strncpy(out->wallpaper_file, val, sizeof(out->wallpaper_file) - 1);
        } else if (local_strcasecmp(key, "Startup") == 0) {
            local_strncpy(out->sound_startup, val, sizeof(out->sound_startup) - 1);
        } else if (local_strcasecmp(key, "Click") == 0) {
            local_strncpy(out->sound_click, val, sizeof(out->sound_click) - 1);
        } else if (local_strcasecmp(key, "ThemeId") == 0) {
            out->theme_id = parse_int_val(val);
        } else if (local_strcasecmp(key, "RenderScript") == 0) {
            local_strncpy(out->render_script, val, sizeof(out->render_script) - 1);
        } else if (local_strcasecmp(key, "TitlebarScript") == 0 || local_strcasecmp(key, "TitlebarRenderScript") == 0) {
            local_strncpy(out->titlebar_script, val, sizeof(out->titlebar_script) - 1);
        } else if (local_strcasecmp(key, "ButtonScript") == 0 || local_strcasecmp(key, "ButtonRenderScript") == 0) {
            local_strncpy(out->button_script, val, sizeof(out->button_script) - 1);
        } else if (local_strcasecmp(key, "PanelScript") == 0 || local_strcasecmp(key, "PanelRenderScript") == 0) {
            local_strncpy(out->panel_script, val, sizeof(out->panel_script) - 1);
        } else if (local_strcasecmp(key, "ProgressScript") == 0 || local_strcasecmp(key, "ProgressBarScript") == 0) {
            local_strncpy(out->progress_script, val, sizeof(out->progress_script) - 1);
        } else if (local_strcasecmp(key, "SelectionScript") == 0 || local_strcasecmp(key, "SelectionRenderScript") == 0) {
            local_strncpy(out->selection_script, val, sizeof(out->selection_script) - 1);
        } else if (local_strcasecmp(key, "TaskbarScript") == 0 || local_strcasecmp(key, "TaskbarRenderScript") == 0) {
            local_strncpy(out->taskbar_script, val, sizeof(out->taskbar_script) - 1);
        }
    }

    if (api->kfree) {
        api->kfree(content);
    }

    // Risoluzione fallback per i raggi non specificati esplicitamente
    if (out->button_radius == 0xFF) out->button_radius = out->control_radius;
    if (out->tab_radius == 0xFF) out->tab_radius = out->control_radius;
    if (out->input_radius == 0xFF) out->input_radius = out->control_radius;
    if (out->panel_radius == 0xFF) out->panel_radius = (out->control_radius > 0) ? (out->control_radius + 2) : 6;
    if (out->start_btn_radius == 0xFF) out->start_btn_radius = (out->control_radius > 0) ? (out->control_radius + 2) : 8;
    if (out->taskbar_btn_radius == 0xFF) out->taskbar_btn_radius = (out->control_radius > 0) ? out->control_radius : 6;
    if (out->tray_radius == 0xFF) out->tray_radius = (out->control_radius > 0) ? out->control_radius : 6;

    // Risoluzione dei raggi dei bordi conformemente a BorderFollowsCorner e ControlBorderFollowsRadius
    if (out->border_radius == 0xFF) {
        out->border_radius = out->border_follows_corner ? out->corner_radius : 0;
    }
    if (!out->border_follows_corner) {
        out->border_radius = 0;
    }

    if (out->button_border_radius == 0xFF) {
        out->button_border_radius = out->control_border_follows_radius ? out->button_radius : 0;
    }
    if (out->tab_border_radius == 0xFF) {
        out->tab_border_radius = out->control_border_follows_radius ? out->tab_radius : 0;
    }
    if (out->input_border_radius == 0xFF) {
        out->input_border_radius = out->control_border_follows_radius ? out->input_radius : 0;
    }
    if (out->panel_border_radius == 0xFF) {
        out->panel_border_radius = out->control_border_follows_radius ? out->panel_radius : 0;
    }
    if (out->taskbar_border_radius == 0xFF) {
        out->taskbar_border_radius = out->control_border_follows_radius ? out->taskbar_btn_radius : 0;
    }
    if (out->start_btn_border_radius == 0xFF) {
        out->start_btn_border_radius = out->control_border_follows_radius ? out->start_btn_radius : 0;
    }
    if (out->tray_border_radius == 0xFF) {
        out->tray_border_radius = out->control_border_follows_radius ? out->tray_radius : 0;
    }

    if (!out->control_border_follows_radius) {
        out->button_border_radius = 0;
        out->tab_border_radius = 0;
        out->input_border_radius = 0;
        out->panel_border_radius = 0;
        out->taskbar_border_radius = 0;
        out->start_btn_border_radius = 0;
        out->tray_border_radius = 0;
    }

    // Determina il theme_id per il Window Manager se non forzato
    if (out->corner_radius == 0 || local_strcasecmp(out->style_engine, "Classic3D") == 0 ||
        local_strstr(out->style_engine, "Classic") || local_strstr(out->name, "Classic")) {
        out->theme_id = 0; // THEME_CLASSIC
    } else if (local_strstr(out->style_engine, "FluentGlass") || local_strstr(out->name, "Fluent Glass") || local_strstr(out->name, "Glass")) {
        out->theme_id = 1; // THEME_LIQUID_GLASS (Frosted Glass ad alta trasparenza)
    } else if (local_strstr(out->style_engine, "MicaLight") || (local_strstr(out->name, "Mica") && local_strstr(out->name, "Light"))) {
        out->theme_id = 2; // THEME_MICA_LIGHT
    } else if (local_strstr(out->style_engine, "MicaDark") || (local_strstr(out->name, "Mica") && local_strstr(out->name, "Dark")) || local_strstr(out->name, "Dark")) {
        out->theme_id = 3; // THEME_MICA_DARK
    } else {
        out->theme_id = 1; // Modern
    }

    return true;
}

static int get_style_rank(const char* style_engine) {
    if (!style_engine) return 99;
    if (local_strstr(style_engine, "MicaDark")) return 1;
    if (local_strstr(style_engine, "MicaLight")) return 2;
    if (local_strstr(style_engine, "FluentGlass") || local_strstr(style_engine, "Glass")) return 3;
    if (local_strstr(style_engine, "LiquidGlass")) return 4;
    if (local_strstr(style_engine, "Classic")) return 5;
    return 10;
}

static void sort_themes(theme_info_t* themes, int count, int order) {
    if (!themes || count <= 1) return;

    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - i - 1; j++) {
            bool swap = false;
            theme_info_t* a = &themes[j];
            theme_info_t* b = &themes[j + 1];

            if (order == THEME_SORT_ALPHA) {
                if (local_strcmp(a->name, b->name) > 0) swap = true;
            } else if (order == THEME_SORT_STYLE) {
                int rank_a = get_style_rank(a->style_engine);
                int rank_b = get_style_rank(b->style_engine);
                if (rank_a > rank_b) swap = true;
                else if (rank_a == rank_b && local_strcmp(a->name, b->name) > 0) swap = true;
            } else {
                if (local_strcmp(a->name, b->name) > 0) swap = true;
            }

            if (swap) {
                theme_info_t temp = *a;
                *a = *b;
                *b = temp;
            }
        }
    }
}

// --- Motore di Rendering Avanzato Script Out-of-Kernel ---

static inline uint32_t local_blend_pixel(uint32_t bg, uint32_t fg, uint8_t alpha) {
    uint32_t rb_bg = bg & 0x00FF00FF;
    uint32_t g_bg  = bg & 0x0000FF00;
    uint32_t rb_fg = fg & 0x00FF00FF;
    uint32_t g_fg  = fg & 0x0000FF00;
    uint32_t inv_a = 256 - alpha;
    uint32_t rb = ((rb_fg * alpha + rb_bg * inv_a) >> 8) & 0x00FF00FF;
    uint32_t g  = ((g_fg  * alpha + g_bg  * inv_a) >> 8) & 0x0000FF00;
    return rb | g;
}

static void parse_next_arg(const char** p_str, char* out_arg, size_t max_len) {
    if (!p_str || !*p_str || !out_arg || max_len == 0) return;
    out_arg[0] = '\0';
    const char* s = *p_str;
    while (*s == ' ' || *s == '\t' || *s == '(' || *s == ',') s++;
    if (*s == ')' || *s == ';' || *s == '\0') {
        *p_str = s;
        return;
    }
    size_t idx = 0;
    while (*s && *s != ',' && *s != ')' && *s != ';' && idx + 1 < max_len) {
        out_arg[idx++] = *s++;
    }
    out_arg[idx] = '\0';
    trim_line(out_arg);
    while (*s == ' ' || *s == '\t') s++;
    if (*s == ',') s++;
    *p_str = s;
}

static int execute_render_script(const kernel_api_t* api, const char* script,
                                 int32_t x, int32_t y, int32_t w, int32_t h,
                                 int32_t radius, uint32_t flags) {
    if (!api || !script || !script[0] || w <= 0 || h <= 0) return 0;

    const char* p = script;
    int commands_executed = 0;

    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ';') p++;
        if (!*p) break;

        char cmd_name[32];
        size_t c_idx = 0;
        while (*p && *p != '(' && *p != ';' && *p != ' ' && c_idx + 1 < sizeof(cmd_name)) {
            cmd_name[c_idx++] = *p++;
        }
        cmd_name[c_idx] = '\0';
        trim_line(cmd_name);

        char arg1[32] = {0}, arg2[32] = {0}, arg3[32] = {0}, arg4[32] = {0};
        if (*p == '(') {
            p++;
            parse_next_arg(&p, arg1, sizeof(arg1));
            parse_next_arg(&p, arg2, sizeof(arg2));
            parse_next_arg(&p, arg3, sizeof(arg3));
            parse_next_arg(&p, arg4, sizeof(arg4));
            while (*p && *p != ')' && *p != ';') p++;
            if (*p == ')') p++;
        }

        // 1. SPECULAR_ARC(alpha, color) oppure SPECULAR_ARC(w_pct, h_pct, alpha, color)
        if (local_strcasecmp(cmd_name, "SPECULAR_ARC") == 0) {
            uint8_t alpha = 40;
            uint32_t color = 0xFFFFFF;
            int arc_h = (h * 45) / 100;
            if (arg3[0] != '\0') {
                int pct_h = parse_int_val(arg2);
                if (pct_h > 0) arc_h = (h * pct_h) / 100;
                alpha = (uint8_t)parse_int_val(arg3);
                if (arg4[0] != '\0') color = parse_hex_color(arg4);
            } else if (arg1[0] != '\0') {
                alpha = (uint8_t)parse_int_val(arg1);
                if (arg2[0] != '\0') color = parse_hex_color(arg2);
            }
            if (arc_h < 4) arc_h = 4;
            if (arc_h > h) arc_h = h;

            if (api->gfx_fill_alpha) {
                for (int r = 0; r < arc_h; r++) {
                    int pad = (r * r * (w / 4)) / (arc_h * arc_h + 1);
                    if (pad * 2 >= w) break;
                    uint8_t line_a = (uint8_t)((alpha * (arc_h - r)) / arc_h);
                    api->gfx_fill_alpha(x + pad, y + r, w - 2 * pad, 1, color, line_a);
                }
            }
            commands_executed++;
        }
        // 2. VERTICAL_GLOSS(split_pct, top_alpha, bottom_alpha)
        else if (local_strcasecmp(cmd_name, "VERTICAL_GLOSS") == 0) {
            int split_pct = arg1[0] ? parse_int_val(arg1) : 50;
            uint8_t top_a = arg2[0] ? (uint8_t)parse_int_val(arg2) : 25;
            uint8_t bot_a = arg3[0] ? (uint8_t)parse_int_val(arg3) : 0;
            int split_y = (h * split_pct) / 100;
            if (split_y > 0 && top_a > 0 && api->gfx_fill_alpha) {
                api->gfx_fill_alpha(x, y, w, split_y, 0xFFFFFF, top_a);
            }
            if (h - split_y > 0 && bot_a > 0 && api->gfx_fill_alpha) {
                api->gfx_fill_alpha(x, y + split_y, w, h - split_y, 0x000000, bot_a);
            }
            commands_executed++;
        }
        // 3. HIGHLIGHT_EDGE(edge, thickness, color, alpha) oppure HIGHLIGHT_LINE(...)
        else if (local_strcasecmp(cmd_name, "HIGHLIGHT_EDGE") == 0 || local_strcasecmp(cmd_name, "HIGHLIGHT_LINE") == 0) {
            char edge = arg1[0] ? arg1[0] : 't';
            int thick = arg2[0] ? parse_int_val(arg2) : 1;
            uint32_t col = arg3[0] ? parse_hex_color(arg3) : 0xFFFFFF;
            uint8_t a = arg4[0] ? (uint8_t)parse_int_val(arg4) : 60;
            if (thick < 1) thick = 1;

            if (api->gfx_fill_alpha) {
                if (edge == 't' || edge == 'T') {
                    api->gfx_fill_alpha(x, y, w, thick, col, a);
                } else if (edge == 'b' || edge == 'B') {
                    api->gfx_fill_alpha(x, y + h - thick, w, thick, col, a);
                } else if (edge == 'l' || edge == 'L') {
                    api->gfx_fill_alpha(x, y, thick, h, col, a);
                } else if (edge == 'r' || edge == 'R') {
                    api->gfx_fill_alpha(x + w - thick, y, thick, h, col, a);
                }
            }
            commands_executed++;
        }
        // 4. INNER_HIGHLIGHT(inset, color, alpha)
        else if (local_strcasecmp(cmd_name, "INNER_HIGHLIGHT") == 0) {
            int inset = arg1[0] ? parse_int_val(arg1) : 1;
            uint32_t col = arg2[0] ? parse_hex_color(arg2) : 0xFFFFFF;
            int inner_rad = radius > inset ? radius - inset : 0;
            if (inner_rad > 0 && api->gfx_draw_rounded_rect) {
                api->gfx_draw_rounded_rect(x + inset, y + inset, w - 2 * inset, h - 2 * inset, inner_rad, col);
            } else if (api->gfx_draw_rect) {
                api->gfx_draw_rect(x + inset, y + inset, w - 2 * inset, h - 2 * inset, col);
            }
            commands_executed++;
        }
        // 5. ETCHED_LINE(side, light_col, shadow_col, alpha)
        else if (local_strcasecmp(cmd_name, "ETCHED_LINE") == 0) {
            char edge = arg1[0] ? arg1[0] : 'r';
            uint32_t l_col = arg2[0] ? parse_hex_color(arg2) : 0xFFFFFF;
            uint32_t s_col = arg3[0] ? parse_hex_color(arg3) : 0x000000;
            uint8_t a = arg4[0] ? (uint8_t)parse_int_val(arg4) : 40;

            if (api->gfx_fill_alpha) {
                if (edge == 'r' || edge == 'R') {
                    api->gfx_fill_alpha(x + w - 2, y, 1, h, s_col, a);
                    api->gfx_fill_alpha(x + w - 1, y, 1, h, l_col, a);
                } else if (edge == 'b' || edge == 'B') {
                    api->gfx_fill_alpha(x, y + h - 2, w, 1, s_col, a);
                    api->gfx_fill_alpha(x, y + h - 1, w, 1, l_col, a);
                }
            }
            commands_executed++;
        }
        // 6. CYLINDER_SHADING(base_col, highlight_col)
        else if (local_strcasecmp(cmd_name, "CYLINDER_SHADING") == 0) {
            uint32_t base_c = arg1[0] ? parse_hex_color(arg1) : 0x2563EB;
            uint32_t hi_c   = arg2[0] ? parse_hex_color(arg2) : 0x60A5FA;
            int mid_y = h / 2;
            if (api->gfx_draw_gradient_v && mid_y > 0) {
                api->gfx_draw_gradient_v(x, y, w, mid_y, base_c, hi_c);
                api->gfx_draw_gradient_v(x, y + mid_y, w, h - mid_y, hi_c, base_c);
            }
            if (api->gfx_fill_alpha) {
                api->gfx_fill_alpha(x, y + mid_y - 1, w, 2, 0xFFFFFF, 70);
            }
            commands_executed++;
        }
        // 7. GLASS_TINT(color, alpha)
        else if (local_strcasecmp(cmd_name, "GLASS_TINT") == 0) {
            uint32_t col = arg1[0] ? parse_hex_color(arg1) : 0xFFFFFF;
            uint8_t a = arg2[0] ? (uint8_t)parse_int_val(arg2) : 30;
            if (api->gfx_fill_alpha) {
                api->gfx_fill_alpha(x, y, w, h, col, a);
            }
            commands_executed++;
        }
        // 8. AURA_BORDER(radius, color, alpha) / GLOW_BORDER
        else if (local_strcasecmp(cmd_name, "AURA_BORDER") == 0 || local_strcasecmp(cmd_name, "GLOW_BORDER") == 0) {
            int aura_r = arg1[0] ? parse_int_val(arg1) : 2;
            uint32_t col = arg2[0] ? parse_hex_color(arg2) : 0x38BDF8;
            uint8_t a = arg3[0] ? (uint8_t)parse_int_val(arg3) : 50;
            if (aura_r < 1) aura_r = 1;
            if (aura_r > 4) aura_r = 4;

            for (int step = 1; step <= aura_r; step++) {
                uint8_t step_a = (uint8_t)((a * (aura_r - step + 1)) / (aura_r + 1));
                if (api->gfx_fill_alpha) {
                    api->gfx_fill_alpha(x - step, y - step, w + 2 * step, 1, col, step_a);
                    api->gfx_fill_alpha(x - step, y + h + step - 1, w + 2 * step, 1, col, step_a);
                    api->gfx_fill_alpha(x - step, y - step, 1, h + 2 * step, col, step_a);
                    api->gfx_fill_alpha(x + w + step - 1, y - step, 1, h + 2 * step, col, step_a);
                }
            }
            commands_executed++;
        }
        // 9. NOISE_TEXTURE(intensity)
        else if (local_strcasecmp(cmd_name, "NOISE_TEXTURE") == 0) {
            int intensity = arg1[0] ? parse_int_val(arg1) : 4;
            if (intensity < 1) intensity = 1;
            if (intensity > 20) intensity = 20;

            if (api->gfx_draw_pixel && api->gfx_get_pixel) {
                for (int py = y + 2; py < y + h - 2; py += 3) {
                    for (int px = x + 2; px < x + w - 2; px += 3) {
                        uint32_t seed = (uint32_t)(px * 374761393 + py * 668265263);
                        uint8_t n = (uint8_t)((seed ^ (seed >> 13)) & 0xFF);
                        if (n < 40) {
                            uint32_t pix = api->gfx_get_pixel(px, py);
                            api->gfx_draw_pixel(px, py, local_blend_pixel(pix, 0xFFFFFF, (uint8_t)(intensity * 3)));
                        } else if (n > 215) {
                            uint32_t pix = api->gfx_get_pixel(px, py);
                            api->gfx_draw_pixel(px, py, local_blend_pixel(pix, 0x000000, (uint8_t)(intensity * 3)));
                        }
                    }
                }
            }
            commands_executed++;
        }
        // 10. GRADIENT(c1, c2, horizontal/vertical)
        else if (local_strcasecmp(cmd_name, "GRADIENT") == 0) {
            uint32_t c1 = arg1[0] ? parse_hex_color(arg1) : 0x1E293B;
            uint32_t c2 = arg2[0] ? parse_hex_color(arg2) : 0x0F172A;
            bool is_v = (!arg3[0] || arg3[0] == 'v' || arg3[0] == 'V');
            if (is_v && api->gfx_draw_gradient_v) {
                api->gfx_draw_gradient_v(x, y, w, h, c1, c2);
            } else if (!is_v && api->gfx_draw_gradient_h) {
                api->gfx_draw_gradient_h(x, y, w, h, c1, c2);
            }
            commands_executed++;
        }

        while (*p && *p != ';') p++;
        if (*p == ';') p++;
    }

    return commands_executed;
}

// Entry point unico del modulo all'offset 64 (.text.entry)
__attribute__((section(".text.entry"), used))
int thememgr_dll_entry(const kernel_api_t* api, int cmd, theme_mgr_cmd_params_t* params) {
    if (!api || !params) return 0;

    if (cmd == THEME_CMD_EXEC_SCRIPT) {
        return execute_render_script(api, params->render_script,
                                     params->render_x, params->render_y,
                                     params->render_w, params->render_h,
                                     params->render_radius, params->render_flags);
    }

    if (cmd == THEME_CMD_SORT) {
        sort_themes(params->themes, params->theme_count, params->sort_order);
        return params->theme_count;
    }

    if (cmd == THEME_CMD_SCAN) {
        const char* path_to_scan = params->dir_path ? params->dir_path : "C:/NewOS/Resource/Themes";
        log_msg(api, "[THEMEMGR.DLL] Scansione temi fuori dal kernel in: ");
        log_msg(api, path_to_scan);
        log_msg(api, "\n");

        if (!api->vfs_readdir || !params->themes || params->max_themes <= 0) {
            log_msg(api, "[THEMEMGR.DLL ERRORE] Parametri o API VFS mancanti!\n");
            return 0;
        }

        // Buffer temporaneo per le voci di directory allocato sullo stack
        dll_vfs_dirent_t dir_entries[32];
        int32_t dcount = api->vfs_readdir(path_to_scan, dir_entries, 32);
        if (dcount <= 0 && local_strcmp(path_to_scan, "C:/NewOS/Resource/Themes") == 0) {
            dcount = api->vfs_readdir("C:\\NewOS\\Resource\\Themes", dir_entries, 32);
        }
        if (dcount <= 0 && local_strcmp(path_to_scan, "C:/NewOS/Resource/Themes") == 0) {
            dcount = api->vfs_readdir("/NewOS/Resource/Themes", dir_entries, 32);
        }
        if (dcount <= 0) {
            dcount = api->vfs_readdir("C:/NewOS/Themes", dir_entries, 32);
        }

        int loaded_count = 0;
        for (int i = 0; i < dcount && loaded_count < params->max_themes; i++) {
            if (dir_entries[i].is_directory) continue;
            const char* fname = dir_entries[i].name;
            const char* dot = local_strrchr(fname, '.');
            if (!dot) continue;
            if (local_strcasecmp(dot, ".THEMES") != 0 && local_strcasecmp(dot, ".THM") != 0) continue;

            char full_file_path[160];
            local_make_path(full_file_path, sizeof(full_file_path), path_to_scan, fname);

            theme_info_t item;
            if (parse_theme_file(api, full_file_path, fname, &item)) {
                memcpy(&params->themes[loaded_count], &item, sizeof(theme_info_t));
                loaded_count++;
            }
        }

        // Ordinamento automatico predefinito
        sort_themes(params->themes, loaded_count, params->sort_order);

        log_msg(api, "[THEMEMGR.DLL] Scansione completata con successo.\n");
        return loaded_count;
    }

    return 0;
}
