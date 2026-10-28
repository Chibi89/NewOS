/**
 * SystemLibrary/cursor_mgr.c
 * Libreria Dinamica Out-of-Kernel per NewOS V1 (64-bit)
 * Gestione, standardizzazione e riconoscimento cursori personalizzati (.CUR / .ANI)
 * Codice 100% rientrante senza variabili globali o dipendenze BSS
 * (C) Newspoint Software - NewSoft Dynamic System Subsystem
 */

#include <screen_modules.h>
#include <gui/cursor.h>

// Forward declaration dell'entry point con marcatura di sezione .text.entry
__attribute__((section(".text.entry"), used))
int cursormgr_entry(const kernel_api_t* api, int cmd, cursor_cmd_params_t* params);

// Header protetto di modulo NewOS (esattamente 64 byte all'inizio di CURSORMGR.DLL)
__attribute__((section(".header"), used))
module_header_t g_cursormgr_header = {
    .magic          = MODULE_MAGIC_NEWOS,
    .header_size    = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type    = MODULE_TYPE_SYSTEM_LIBRARY,
    .code_size      = 0, // Compilatore / build.js inserira la dimensione
    .entry_offset   = 64, // Posizione entry point (immediatamente dopo l'header)
    .checksum       = 0, // Calcolato dal build system
    .security_flags = SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name    = "CURSORMGR",
    .signature      = MODULE_AUTH_TAG
};

// Struttura vfs_dirent interna della DLL conforme al kernel
typedef struct {
    char     name[128];
    uint32_t size;
    uint16_t attributes;
    bool     is_directory;
} dll_vfs_dirent_t;

// --- Implementazioni Freestanding di Memoria e Stringhe ---

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

static char local_tolower(char c) {
    if (c >= 'A' && c <= 'Z') return c + ('a' - 'A');
    return c;
}

static const char* local_stristr(const char* haystack, const char* needle) {
    if (!haystack || !needle) return NULL;
    if (!*needle) return haystack;
    for (; *haystack; haystack++) {
        const char* h = haystack;
        const char* n = needle;
        while (*h && *n && local_tolower(*h) == local_tolower(*n)) {
            h++;
            n++;
        }
        if (!*n) return haystack;
    }
    return NULL;
}

static void log_msg(const kernel_api_t* api, const char* s) {
    if (!s) return;
    if (api && api->serial_puts) {
        api->serial_puts(s);
    } else {
        while (*s) {
            __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)(*s++)), "Nd"((uint16_t)0x3F8));
        }
    }
}

// --- Riconoscimento Euristico Ruolo Cursore (Windows Cursor Roles) ---

static cursor_system_state_t match_cursor_role(const char* filename) {
    if (!filename) return CURSOR_STATE_NORMAL;

    // Normal Select / Arrow
    if (local_stristr(filename, "normal") || 
        local_stristr(filename, "arrow") || 
        local_stristr(filename, "larrow") ||
        local_stristr(filename, "pointer")) {
        return CURSOR_STATE_NORMAL;
    }
    // Help Select
    if (local_stristr(filename, "help")) {
        return CURSOR_STATE_HELP;
    }
    // Working in Background / AppStarting
    if (local_stristr(filename, "working") || 
        local_stristr(filename, "appstrt") || 
        local_stristr(filename, "appstart")) {
        return CURSOR_STATE_WORKING;
    }
    // Busy / Wait
    if (local_stristr(filename, "busy") || 
        local_stristr(filename, "wait") ||
        local_stristr(filename, "lwait")) {
        return CURSOR_STATE_BUSY;
    }
    // Precision Select / Cross
    if (local_stristr(filename, "cross") || 
        local_stristr(filename, "precision") ||
        local_stristr(filename, "lcross")) {
        return CURSOR_STATE_CROSS;
    }
    // Text Select / IBeam
    if (local_stristr(filename, "beam") || 
        local_stristr(filename, "ibeam") || 
        local_stristr(filename, "text") ||
        local_stristr(filename, "libeam")) {
        return CURSOR_STATE_IBEAM;
    }
    // Handwriting / Pen
    if (local_stristr(filename, "pen") || 
        local_stristr(filename, "handwriting") || 
        local_stristr(filename, "write")) {
        return CURSOR_STATE_PEN;
    }
    // Unavailable / No / NoDrop
    if (local_stristr(filename, "unavail") || 
        local_stristr(filename, "no_") || 
        local_stristr(filename, "nodrop") || 
        local_stristr(filename, "lnodrop") ||
        local_stristr(filename, "denied")) {
        return CURSOR_STATE_UNAVAIL;
    }
    // Vertical Resize (Size NS)
    if (local_stristr(filename, "vertical") || 
        local_stristr(filename, "_ns") || 
        local_stristr(filename, "size_ns") ||
        local_stristr(filename, "lns")) {
        return CURSOR_STATE_RESIZE_NS;
    }
    // Horizontal Resize (Size WE / EW)
    if (local_stristr(filename, "horizontal") || 
        local_stristr(filename, "_ew") || 
        local_stristr(filename, "_we") || 
        local_stristr(filename, "size_we") ||
        local_stristr(filename, "lwe")) {
        return CURSOR_STATE_RESIZE_EW;
    }
    // Diagonal Resize 1 (NWSE / UpLeft)
    if (local_stristr(filename, "nwse") || 
        local_stristr(filename, "diagonal 1") || 
        local_stristr(filename, "upleft") ||
        local_stristr(filename, "lnwse") ||
        local_stristr(filename, "size1")) {
        return CURSOR_STATE_RESIZE_NWSE;
    }
    // Diagonal Resize 2 (NESW / UpRight)
    if (local_stristr(filename, "nesw") || 
        local_stristr(filename, "diagonal 2") || 
        local_stristr(filename, "upright") ||
        local_stristr(filename, "lnesw") ||
        local_stristr(filename, "size2")) {
        return CURSOR_STATE_RESIZE_NESW;
    }
    // Move / Move All / Pan
    if (local_stristr(filename, "move") || 
        local_stristr(filename, "lmove") ||
        local_stristr(filename, "sizeall")) {
        return CURSOR_STATE_MOVE;
    }
    // Alternate Select / Special / Up Arrow
    if (local_stristr(filename, "up.") || 
        local_stristr(filename, "up_") || 
        local_stristr(filename, "alternate") ||
        local_stristr(filename, "special")) {
        return CURSOR_STATE_UP;
    }
    // Link Select / Hand
    if (local_stristr(filename, "link") || 
        local_stristr(filename, "hand")) {
        return CURSOR_STATE_LINK;
    }
    // Pin / Location
    if (local_stristr(filename, "pin") || 
        local_stristr(filename, "lpin") ||
        local_stristr(filename, "location")) {
        return CURSOR_STATE_PIN;
    }
    // Person / Contact
    if (local_stristr(filename, "person") || 
        local_stristr(filename, "lperson") ||
        local_stristr(filename, "contact")) {
        return CURSOR_STATE_PERSON;
    }

    return CURSOR_STATE_NORMAL;
}

// --- Decodificatore Windows .CUR ---

static bool decode_cur_buffer(const uint8_t* buf, uint32_t size, cursor_resource_t* out_res) {
    if (!buf || size < 22 || !out_res) return false;

    // Header ICONDIR (6 byte): idReserved=0, idType=2 (CUR) o 1 (ICO), idCount>=1
    uint16_t id_reserved = buf[0] | ((uint16_t)buf[1] << 8);
    uint16_t id_type     = buf[2] | ((uint16_t)buf[3] << 8);
    uint16_t id_count    = buf[4] | ((uint16_t)buf[5] << 8);

    if (id_reserved != 0 || (id_type != 2 && id_type != 1) || id_count == 0) {
        return false;
    }

    // Cerca l'immagine con risoluzione preferita 32x32 (o la più vicina <= 32)
    int best_score = 9999;
    uint32_t best_offset = 0;
    uint16_t best_hot_x = 0;
    uint16_t best_hot_y = 0;

    for (int i = 0; i < (int)id_count && (6 + (i + 1) * 16) <= (int)size; i++) {
        int off = 6 + i * 16;
        uint8_t bw = buf[off];
        uint8_t bh = buf[off + 1];
        int w = bw == 0 ? 256 : bw;
        int h = bh == 0 ? 256 : bh;
        uint16_t hx = buf[off + 4] | ((uint16_t)buf[off + 5] << 8);
        uint16_t hy = buf[off + 6] | ((uint16_t)buf[off + 7] << 8);
        uint32_t img_offset = buf[off + 12] | ((uint32_t)buf[off + 13] << 8) |
                              ((uint32_t)buf[off + 14] << 16) | ((uint32_t)buf[off + 15] << 24);

        int score = (w >= 32 && h >= 32) ? (w - 32 + h - 32) : (64 - w - h + 50);
        if (score < best_score) {
            best_score = score;
            best_offset = img_offset;
            best_hot_x = hx;
            best_hot_y = hy;
        }
    }

    if (best_offset == 0 || best_offset + 40 > size) {
        return false;
    }

    const uint8_t* dib = buf + best_offset;
    uint32_t dib_header_size = dib[0] | ((uint32_t)dib[1] << 8) |
                               ((uint32_t)dib[2] << 16) | ((uint32_t)dib[3] << 24);
    if (dib_header_size < 40) return false;

    int32_t dib_w = (int32_t)(dib[4] | ((uint32_t)dib[5] << 8) | ((uint32_t)dib[6] << 16) | ((uint32_t)dib[7] << 24));
    int32_t dib_h2 = (int32_t)(dib[8] | ((uint32_t)dib[9] << 8) | ((uint32_t)dib[10] << 16) | ((uint32_t)dib[11] << 24));
    int32_t dib_h = dib_h2 / 2;
    uint16_t bpp = dib[14] | ((uint16_t)dib[15] << 8);

    if (dib_w <= 0 || dib_h <= 0) return false;

    out_res->width = CURSOR_BMP_SIZE;
    out_res->height = CURSOR_BMP_SIZE;
    out_res->hotspot_x = (best_hot_x * CURSOR_BMP_SIZE) / dib_w;
    out_res->hotspot_y = (best_hot_y * CURSOR_BMP_SIZE) / dib_h;
    if (out_res->hotspot_x >= CURSOR_BMP_SIZE) out_res->hotspot_x = 0;
    if (out_res->hotspot_y >= CURSOR_BMP_SIZE) out_res->hotspot_y = 0;

    memset(out_res->pixels, 0, sizeof(out_res->pixels));

    const uint8_t* pixel_src = dib + dib_header_size;

    if (bpp == 32) {
        for (int y = 0; y < CURSOR_BMP_SIZE; y++) {
            int src_y = ((CURSOR_BMP_SIZE - 1 - y) * dib_h) / CURSOR_BMP_SIZE;
            for (int x = 0; x < CURSOR_BMP_SIZE; x++) {
                int src_x = (x * dib_w) / CURSOR_BMP_SIZE;
                uint32_t pix_idx = (src_y * dib_w + src_x) * 4;
                if ((best_offset + dib_header_size + pix_idx + 4) <= size) {
                    uint8_t b = pixel_src[pix_idx];
                    uint8_t g = pixel_src[pix_idx + 1];
                    uint8_t r = pixel_src[pix_idx + 2];
                    uint8_t a = pixel_src[pix_idx + 3];

                    if (a == 0 && (r != 0 || g != 0 || b != 0)) {
                        a = 255;
                    }
                    out_res->pixels[y * CURSOR_BMP_SIZE + x] = ((uint32_t)a << 24) |
                                                               ((uint32_t)r << 16) |
                                                               ((uint32_t)g << 8)  | b;
                }
            }
        }
        out_res->is_valid = true;
        return true;
    } else if (bpp == 1) {
        const uint8_t* palette = pixel_src;
        uint32_t palette_bytes = 8;
        const uint8_t* xor_mask = pixel_src + palette_bytes;
        uint32_t xor_pitch = ((dib_w + 31) / 32) * 4;
        const uint8_t* and_mask = xor_mask + (xor_pitch * dib_h);

        uint32_t col0 = palette[0] | ((uint32_t)palette[1] << 8) | ((uint32_t)palette[2] << 16);
        uint32_t col1 = palette[4] | ((uint32_t)palette[5] << 8) | ((uint32_t)palette[6] << 16);

        for (int y = 0; y < CURSOR_BMP_SIZE; y++) {
            int src_y = ((CURSOR_BMP_SIZE - 1 - y) * dib_h) / CURSOR_BMP_SIZE;
            for (int x = 0; x < CURSOR_BMP_SIZE; x++) {
                int src_x = (x * dib_w) / CURSOR_BMP_SIZE;

                uint32_t byte_idx = src_y * xor_pitch + (src_x / 8);
                uint8_t bit_mask = 0x80 >> (src_x % 8);

                uint8_t xor_bit = (xor_mask[byte_idx] & bit_mask) ? 1 : 0;
                uint8_t and_bit = (and_mask[byte_idx] & bit_mask) ? 1 : 0;

                if (and_bit == 0) {
                    uint32_t rgb = (xor_bit == 0) ? col0 : col1;
                    out_res->pixels[y * CURSOR_BMP_SIZE + x] = 0xFF000000 | rgb;
                } else {
                    if (xor_bit == 1) {
                        out_res->pixels[y * CURSOR_BMP_SIZE + x] = 0xFFFFFFFF;
                    } else {
                        out_res->pixels[y * CURSOR_BMP_SIZE + x] = 0x00000000;
                    }
                }
            }
        }
        out_res->is_valid = true;
        return true;
    }

    return false;
}

// --- Decodificatore Windows .ANI (RIFF ACON) ---

static bool decode_ani_buffer(const uint8_t* buf, uint32_t size, cursor_resource_t* out_res) {
    if (!buf || size < 64 || !out_res) return false;

    if (buf[0] != 'R' || buf[1] != 'I' || buf[2] != 'F' || buf[3] != 'F') return false;
    if (buf[8] != 'A' || buf[9] != 'C' || buf[10] != 'O' || buf[11] != 'N') return false;

    uint32_t pos = 12;
    while (pos + 8 <= size) {
        uint32_t clen = buf[pos+4] | ((uint32_t)buf[pos+5] << 8) |
                        ((uint32_t)buf[pos+6] << 16) | ((uint32_t)buf[pos+7] << 24);

        if (buf[pos] == 'L' && buf[pos+1] == 'I' && buf[pos+2] == 'S' && buf[pos+3] == 'T') {
            if (pos + 12 <= size && buf[pos+8] == 'f' && buf[pos+9] == 'r' && buf[pos+10] == 'a' && buf[pos+11] == 'm') {
                uint32_t fpos = pos + 12;
                uint32_t end_list = pos + 8 + clen;
                if (end_list > size) end_list = size;

                while (fpos + 8 <= end_list) {
                    uint32_t flen = buf[fpos+4] | ((uint32_t)buf[fpos+5] << 8) |
                                    ((uint32_t)buf[fpos+6] << 16) | ((uint32_t)buf[fpos+7] << 24);

                    if (buf[fpos] == 'i' && buf[fpos+1] == 'c' && buf[fpos+2] == 'o' && buf[fpos+3] == 'n') {
                        if (fpos + 8 + flen <= size) {
                            if (decode_cur_buffer(buf + fpos + 8, flen, out_res)) {
                                out_res->is_animated = true;
                                out_res->frame_count = 1;
                                return true;
                            }
                        }
                    }
                    if (flen == 0) break;
                    fpos += 8 + flen + (flen % 2);
                }
            }
        }
        if (clen == 0) break;
        pos += 8 + clen + (clen % 2);
    }

    return false;
}

static bool load_cursor_file(const kernel_api_t* api, const char* full_path, cursor_system_state_t role, cursor_resource_t* out_res) {
    if (!api || !api->vfs_read_entire_file || !full_path || !out_res) return false;

    uint32_t fsize = 0;
    uint8_t* fbuf = api->vfs_read_entire_file(full_path, &fsize);
    if (!fbuf || fsize < 22) {
        if (fbuf && api->kfree) api->kfree(fbuf);
        return false;
    }

    out_res->role = role;
    local_strncpy(out_res->fullpath, full_path, sizeof(out_res->fullpath) - 1);

    const char* p = full_path;
    const char* last_slash = p;
    while (*p) {
        if (*p == '\\' || *p == '/') last_slash = p + 1;
        p++;
    }
    local_strncpy(out_res->filename, last_slash, sizeof(out_res->filename) - 1);

    bool ok = false;
    if (local_stristr(out_res->filename, ".ani")) {
        ok = decode_ani_buffer(fbuf, fsize, out_res);
    } else {
        ok = decode_cur_buffer(fbuf, fsize, out_res);
    }

    if (api->kfree) {
        api->kfree(fbuf);
    }

    return ok;
}

static void load_scheme_resources(const kernel_api_t* api, cursor_context_t* ctx, int scheme_idx) {
    if (!api || !ctx || !api->kmalloc || !api->kfree) return;
    if (scheme_idx < 0 || scheme_idx >= ctx->scheme_count) return;

    cursor_scheme_info_t* sc = &ctx->schemes[scheme_idx];
    log_msg(api, "[CURSORMGR] Caricamento schema: ");
    log_msg(api, sc->name);
    log_msg(api, "\n");

    for (int i = 0; i < CURSOR_STATE_COUNT; i++) {
        ctx->active_res_loaded[i] = false;
        ctx->active_resources[i].is_valid = false;
    }

    if (sc->is_directory_scheme) {
        dll_vfs_dirent_t* sub_dirents = (dll_vfs_dirent_t*)api->kmalloc(sizeof(dll_vfs_dirent_t) * 32);
        if (sub_dirents) {
            int32_t count = api->vfs_readdir(sc->path, sub_dirents, 32);
            for (int i = 0; i < count; i++) {
                if (sub_dirents[i].is_directory) continue;
                const char* fn = sub_dirents[i].name;
                if (!local_stristr(fn, ".cur") && !local_stristr(fn, ".ani")) continue;

                cursor_system_state_t role = match_cursor_role(fn);
                // Precarica i ruoli chiave
                if (role == CURSOR_STATE_NORMAL || role == CURSOR_STATE_RESIZE_NS ||
                    role == CURSOR_STATE_RESIZE_EW || role == CURSOR_STATE_RESIZE_NWSE ||
                    role == CURSOR_STATE_RESIZE_NESW || role == CURSOR_STATE_BUSY) {
                    if (!ctx->active_res_loaded[role]) {
                        char full[160];
                        local_strcpy(full, sc->path);
                        local_strcpy(full + local_strlen(full), "/");
                        local_strcpy(full + local_strlen(full), fn);

                        if (load_cursor_file(api, full, role, &ctx->active_resources[role])) {
                            ctx->active_res_loaded[role] = true;
                        }
                    }
                }
            }
            api->kfree(sub_dirents);
        }
    } else {
        // Schema da file singoli: pre-carica ruoli chiave
        for (int i = 0; i < ctx->single_count; i++) {
            cursor_system_state_t role = ctx->single_cursors[i].role;
            if (role == CURSOR_STATE_NORMAL || role == CURSOR_STATE_RESIZE_NS ||
                role == CURSOR_STATE_RESIZE_EW || role == CURSOR_STATE_RESIZE_NWSE ||
                role == CURSOR_STATE_RESIZE_NESW) {
                if (!ctx->active_res_loaded[role]) {
                    if (load_cursor_file(api, ctx->single_cursors[i].fullpath, role, &ctx->active_resources[role])) {
                        ctx->active_res_loaded[role] = true;
                    }
                }
            }
        }
    }

    // Fallback su NORMAL se non caricato
    if (!ctx->active_res_loaded[CURSOR_STATE_NORMAL]) {
        for (int i = 0; i < CURSOR_STATE_COUNT; i++) {
            if (ctx->active_res_loaded[i]) {
                memcpy(&ctx->active_resources[CURSOR_STATE_NORMAL], &ctx->active_resources[i], sizeof(cursor_resource_t));
                ctx->active_resources[CURSOR_STATE_NORMAL].role = CURSOR_STATE_NORMAL;
                ctx->active_res_loaded[CURSOR_STATE_NORMAL] = true;
                break;
            }
        }
    }

    ctx->active_scheme_idx = scheme_idx;
}

static int scan_cursor_directory(const kernel_api_t* api, cursor_context_t* ctx, const char* root_path) {
    if (!api || !api->vfs_readdir || !ctx || !api->kmalloc || !api->kfree) return 0;

    const char* path = root_path ? root_path : CURSOR_DEFAULT_PATH;
    local_strncpy(ctx->scan_root, path, sizeof(ctx->scan_root) - 1);

    log_msg(api, "[CURSORMGR] Scansione cartella cursori: ");
    log_msg(api, ctx->scan_root);
    log_msg(api, "\n");

    ctx->scheme_count = 0;
    ctx->single_count = 0;

    // Alloca buffer di scansione temporaneo nel kernel heap
    dll_vfs_dirent_t* dirents = (dll_vfs_dirent_t*)api->kmalloc(sizeof(dll_vfs_dirent_t) * 256);
    if (!dirents) {
        log_msg(api, "  [CURSORMGR ERRORE] Impossibile allocare buffer directory.\n");
        return 0;
    }

    int32_t count = api->vfs_readdir(ctx->scan_root, dirents, 256);
    log_msg(api, "[CURSORMGR] vfs_readdir completata. Voci trovate: ");
    {
        char b[16];
        int c = count;
        if (c < 0) { log_msg(api, "-"); c = -c; }
        if (c >= 100) {
            b[0] = '0' + (c / 100);
            b[1] = '0' + ((c / 10) % 10);
            b[2] = '0' + (c % 10);
            b[3] = '\n';
            b[4] = '\0';
        } else {
            b[0] = '0' + (c / 10);
            b[1] = '0' + (c % 10);
            b[2] = '\n';
            b[3] = '\0';
        }
        log_msg(api, b);
    }

    if (count <= 0) {
        log_msg(api, "  [CURSORMGR] Nessun file o cartella rilevata.\n");
        api->kfree(dirents);
        return 0;
    }

    // Registra lo schema radice predefinito
    int root_scheme_idx = -1;
    if (ctx->scheme_count < CURSOR_MAX_SCHEMES) {
        root_scheme_idx = ctx->scheme_count++;
        cursor_scheme_info_t* sc = &ctx->schemes[root_scheme_idx];
        local_strcpy(sc->name, "Windows Aero (Default)");
        local_strcpy(sc->path, ctx->scan_root);
        sc->is_directory_scheme = false;
        sc->recognized_count = 0;
        sc->is_active = true;
    }

    for (int i = 0; i < count; i++) {
        if (dirents[i].name[0] == '.') continue;

        if (dirents[i].is_directory) {
            // Sottocartella -> Gruppo / Schema
            if (ctx->scheme_count < CURSOR_MAX_SCHEMES) {
                cursor_scheme_info_t* sc = &ctx->schemes[ctx->scheme_count++];
                local_strncpy(sc->name, dirents[i].name, sizeof(sc->name) - 1);
                local_strcpy(sc->path, ctx->scan_root);
                local_strcpy(sc->path + local_strlen(sc->path), "/");
                local_strcpy(sc->path + local_strlen(sc->path), dirents[i].name);
                sc->is_directory_scheme = true;
                sc->recognized_count = 15;
                sc->is_active = false;

                log_msg(api, "  [GRUPPO/SCHEMA] ");
                log_msg(api, sc->name);
                log_msg(api, "\n");
            }
        } else {
            // File sciolto nella radice -> Cursore singolo
            const char* fn = dirents[i].name;
            if (local_stristr(fn, ".cur") || local_stristr(fn, ".ani")) {
                if (ctx->single_count < CURSOR_MAX_SINGLES) {
                    cursor_single_info_t* item = &ctx->single_cursors[ctx->single_count++];
                    local_strncpy(item->filename, fn, sizeof(item->filename) - 1);
                    local_strcpy(item->fullpath, ctx->scan_root);
                    local_strcpy(item->fullpath + local_strlen(item->fullpath), "/");
                    local_strcpy(item->fullpath + local_strlen(item->fullpath), fn);
                    item->role = match_cursor_role(fn);
                    item->is_valid = true;

                    if (root_scheme_idx >= 0) {
                        ctx->schemes[root_scheme_idx].recognized_count++;
                    }
                }
            }
        }
    }

    api->kfree(dirents);

    log_msg(api, "  [CURSORMGR] Schemi registrati: ");
    char num_buf[16];
    int n = ctx->scheme_count;
    num_buf[0] = '0' + (n / 10);
    num_buf[1] = '0' + (n % 10);
    num_buf[2] = '\n';
    num_buf[3] = '\0';
    log_msg(api, num_buf);

    // Carica le risorse per lo schema iniziale
    load_scheme_resources(api, ctx, 0);

    return ctx->scheme_count;
}

static inline void blend_cursor_pixel(uint32_t* dst, uint32_t src) {
    uint8_t a = (src >> 24) & 0xFF;
    if (a == 0) return;
    if (a == 255) {
        *dst = src & 0xFFFFFF;
        return;
    }
    uint32_t d = *dst;
    uint32_t rb = d & 0x00FF00FF;
    uint32_t g  = d & 0x0000FF00;
    uint32_t s_rb = src & 0x00FF00FF;
    uint32_t s_g  = src & 0x0000FF00;
    rb += ((s_rb - rb) * a) >> 8;
    g  += ((s_g - g) * a) >> 8;
    *dst = (rb & 0x00FF00FF) | (g & 0x0000FF00);
}

static bool draw_cursor(const kernel_api_t* api, cursor_context_t* ctx, int32_t x, int32_t y, cursor_system_state_t state) {
    if (!api || !api->gfx_draw_pixel || !ctx) return false;

    cursor_resource_t* res = NULL;
    if (state < CURSOR_STATE_COUNT && ctx->active_res_loaded[state] && ctx->active_resources[state].is_valid) {
        res = &ctx->active_resources[state];
    } else if (ctx->active_res_loaded[CURSOR_STATE_NORMAL] && ctx->active_resources[CURSOR_STATE_NORMAL].is_valid) {
        res = &ctx->active_resources[CURSOR_STATE_NORMAL];
    } else {
        return false;
    }

    int32_t start_x = x - (int32_t)res->hotspot_x;
    int32_t start_y = y - (int32_t)res->hotspot_y;

    for (int32_t cy = 0; cy < CURSOR_BMP_SIZE; cy++) {
        int32_t py = start_y + cy;
        if (py < 0 || py >= 768) continue;

        for (int32_t cx = 0; cx < CURSOR_BMP_SIZE; cx++) {
            int32_t px = start_x + cx;
            if (px < 0 || px >= 1024) continue;

            uint32_t src_col = res->pixels[cy * CURSOR_BMP_SIZE + cx];
            uint8_t a = (src_col >> 24) & 0xFF;
            if (a == 0) continue;

            if (a == 255) {
                api->gfx_draw_pixel(px, py, src_col & 0xFFFFFF);
            } else if (api->gfx_get_pixel) {
                uint32_t bg = api->gfx_get_pixel(px, py);
                blend_cursor_pixel(&bg, src_col);
                api->gfx_draw_pixel(px, py, bg);
            } else {
                api->gfx_draw_pixel(px, py, src_col & 0xFFFFFF);
            }
        }
    }

    return true;
}

// --- Entry Point Unico del Modulo all'offset 64 (.text.entry) ---

__attribute__((section(".text.entry"), used))
int cursormgr_entry(const kernel_api_t* api, int cmd, cursor_cmd_params_t* params) {
    if (!api || !params || !params->ctx) return 0;
    cursor_context_t* ctx = params->ctx;

    switch (cmd) {
        case CURSOR_CMD_INIT: {
            log_msg(api, "[CURSORMGR.DLL] Inizializzazione modulo completata.\n");
            return 1;
        }

        case CURSOR_CMD_SCAN: {
            const char* dir = params->root_dir ? params->root_dir : CURSOR_DEFAULT_PATH;
            int count = scan_cursor_directory(api, ctx, dir);
            if (params->out_count) *params->out_count = count;
            return count > 0 ? 1 : 0;
        }

        case CURSOR_CMD_SET_ACTIVE_SCHEME: {
            if (params->scheme_index < 0 || params->scheme_index >= ctx->scheme_count) return 0;
            for (int i = 0; i < ctx->scheme_count; i++) ctx->schemes[i].is_active = (i == params->scheme_index);
            load_scheme_resources(api, ctx, params->scheme_index);
            return 1;
        }

        case CURSOR_CMD_DRAW: {
            return draw_cursor(api, ctx, params->draw_x, params->draw_y, params->state_role) ? 1 : 0;
        }

        default:
            return 0;
    }
}
