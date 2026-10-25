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
    local_strcpy(out->style_engine, "Modern");
    out->surface_base = 0x202025;
    out->active_title = 0x202025;
    out->accent_color = 0x0078D4;
    out->border_color = 0x404040;
    out->surface_alpha = 225;
    out->corner_radius = 8;
    out->border_width = 1;
    out->drop_shadow = true;
    out->theme_id = 1;

    if (local_strstr(filename, "CLASSIC") || local_strstr(filename, "TEAL")) {
        local_strcpy(out->name, "Windows 2000 Classic");
        local_strcpy(out->description, "Bevel 3D e verde acqua Teal");
        local_strcpy(out->style_engine, "Classic3D");
        out->surface_base = 0xD4D0C8;
        out->active_title = 0x0A246A;
        out->accent_color = 0x0A246A;
        out->border_color = 0x808080;
        out->surface_alpha = 255;
        out->corner_radius = 0;
        out->border_width = 2;
        out->drop_shadow = false;
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
        } else if (local_strcasecmp(key, "StyleEngine") == 0) {
            local_strncpy(out->style_engine, val, sizeof(out->style_engine) - 1);
        } else if (local_strcasecmp(key, "DesktopColor") == 0 || local_strcasecmp(key, "Desktop") == 0) {
            out->desktop_color = parse_hex_color(val);
        } else if (local_strcasecmp(key, "SurfaceBase") == 0 || local_strcasecmp(key, "BtnFace") == 0) {
            out->surface_base = parse_hex_color(val);
        } else if (local_strcasecmp(key, "ActiveTitle") == 0) {
            out->active_title = parse_hex_color(val);
        } else if (local_strcasecmp(key, "AccentColor") == 0) {
            out->accent_color = parse_hex_color(val);
        } else if (local_strcasecmp(key, "BorderColor") == 0) {
            out->border_color = parse_hex_color(val);
        } else if (local_strcasecmp(key, "SurfaceAlpha") == 0) {
            out->surface_alpha = (uint8_t)parse_int_val(val);
        } else if (local_strcasecmp(key, "CornerRadius") == 0) {
            out->corner_radius = (uint8_t)parse_int_val(val);
        } else if (local_strcasecmp(key, "BorderWidth") == 0) {
            out->border_width = (uint8_t)parse_int_val(val);
        } else if (local_strcasecmp(key, "DropShadow") == 0) {
            out->drop_shadow = (parse_int_val(val) != 0);
        } else if (local_strcasecmp(key, "ThemeId") == 0) {
            out->theme_id = parse_int_val(val);
        } else if (local_strcasecmp(key, "File") == 0 || local_strcasecmp(key, "Wallpaper") == 0) {
            local_strncpy(out->wallpaper_file, val, sizeof(out->wallpaper_file) - 1);
        }
    }

    if (api->kfree) {
        api->kfree(content);
    }

    // Determina il theme_id per il Window Manager
    if (out->corner_radius == 0 || local_strcasecmp(out->style_engine, "Classic3D") == 0 ||
        local_strstr(out->style_engine, "Classic") || local_strstr(out->name, "Classic")) {
        out->theme_id = 0; // THEME_CLASSIC
    } else {
        out->theme_id = 1; // THEME_LIQUID_GLASS / Modern
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

// Entry point unico del modulo all'offset 64 (.text.entry)
__attribute__((section(".text.entry"), used))
int thememgr_dll_entry(const kernel_api_t* api, int cmd, theme_mgr_cmd_params_t* params) {
    if (!api || !params) return 0;

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
