#include <gui/fontmgr.h>
#include <fs/vfs.h>
#include <mm/heap.h>
#include <string.h>

typedef int (*fontmgr_entry_fn)(const kernel_api_t *api, fontmgr_params_t *p);

static fontmgr_entry_fn g_fontmgr_entry = NULL;
static uint8_t *g_fontmgr_buf = NULL;
static bool g_fontmgr_tried = false;

// Cache diretta da 64 slot per evitare chiamate ripetute sui glifi piu' frequenti
typedef struct {
    uint32_t cp;
    uint8_t  glyph[16];
    bool     valid;
} glyph_cache_entry_t;

static glyph_cache_entry_t g_glyph_cache[64];

bool fontmgr_init(void) {
    if (g_fontmgr_entry) return true;
    if (g_fontmgr_tried) return false;
    g_fontmgr_tried = true;

    const char *paths[] = {
        "C:\\NewOS\\SystemLibrary\\FONTMGR.DLL",
        "C:\\NewOS\\Boot\\FONTMGR.DLL",
        "/NewOS/SystemLibrary/FONTMGR.DLL",
        "FONTMGR.DLL"
    };

    uint32_t sz = 0;
    for (int i = 0; i < 4 && !g_fontmgr_entry; i++) {
        uint8_t *buf = (uint8_t *)vfs_read_entire_file(paths[i], &sz);
        if (buf && sz >= sizeof(module_header_t)) {
            if (screen_mgr_verify_module(buf, sz, MODULE_TYPE_SYSTEM_LIBRARY, NULL)) {
                uint8_t *exec_buf = (uint8_t *)kmalloc(sz + 32768);
                if (exec_buf) {
                    memcpy(exec_buf, buf, sz);
                    memset(exec_buf + sz, 0, 32768);
                    const module_header_t *h = (const module_header_t *)exec_buf;
                    g_fontmgr_entry = (fontmgr_entry_fn)(exec_buf + h->entry_offset);
                    g_fontmgr_buf = exec_buf;
                }
            }
        }
        if (buf) kfree(buf);
    }

    if (!g_fontmgr_entry) {
        g_fontmgr_tried = false; // Riprova se il VFS non era ancora montato
        return false;
    }

    fontmgr_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = FONTMGR_CMD_INIT;
    return (g_fontmgr_entry(screen_mgr_get_api(), &p) == 0);
}

bool fontmgr_get_glyph(uint32_t codepoint, uint8_t out_glyph[16]) {
    if (!out_glyph) return false;
    uint32_t slot = codepoint & 63;
    if (g_glyph_cache[slot].valid && g_glyph_cache[slot].cp == codepoint) {
        memcpy(out_glyph, g_glyph_cache[slot].glyph, 16);
        return true;
    }

    if (!fontmgr_init()) return false;

    fontmgr_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = FONTMGR_CMD_GET_GLYPH;
    p.codepoint = codepoint;
    if (g_fontmgr_entry(screen_mgr_get_api(), &p) == 0 && p.found) {
        memcpy(out_glyph, p.glyph, 16);
        g_glyph_cache[slot].cp = codepoint;
        memcpy(g_glyph_cache[slot].glyph, p.glyph, 16);
        g_glyph_cache[slot].valid = true;
        return true;
    }
    return false;
}

bool fontmgr_get_ttf_glyph(uint32_t codepoint, int pixel_height,
                           uint8_t *out_alpha, int max_bytes,
                           int *w, int *h, int *xoff, int *yoff, int *advance) {
    if (!fontmgr_init()) return false;
    fontmgr_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = FONTMGR_CMD_GET_TTF_GLYPH;
    p.codepoint = codepoint;
    p.pixel_height = pixel_height;
    p.ttf_bitmap = out_alpha;
    p.ttf_cap = max_bytes;
    if (g_fontmgr_entry(screen_mgr_get_api(), &p) == 0 && p.found) {
        if (w) *w = p.ttf_w;
        if (h) *h = p.ttf_h;
        if (xoff) *xoff = p.ttf_xoff;
        if (yoff) *yoff = p.ttf_yoff;
        if (advance) *advance = p.ttf_advance;
        return true;
    }
    return false;
}

bool fontmgr_set_ttf(const char *path) {
    if (!path || !fontmgr_init()) return false;
    fontmgr_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = FONTMGR_CMD_SET_TTF;
    p.ttf_path = path;
    return (g_fontmgr_entry(screen_mgr_get_api(), &p) == 0);
}
