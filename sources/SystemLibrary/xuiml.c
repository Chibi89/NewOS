/**
 * SystemLibrary/xuiml.c
 * Motore UI Dichiarativo Out-of-Kernel XUIML.DLL
 * Parsa file .xuiml da C:\NewOS\SystemLibrary\UI\ e gestisce layout,
 * rendering temato (Classic / Liquid Glass / Mica) e input mouse/tastiera.
 * (C) Newspoint Software - NewSoft Dynamic System Library
 */

#include "cpl_dll_ui.h"
#include "xuiml.h"

#ifndef BUILDING_KERNEL_LOADER
__attribute__((section(".header"), used))
module_header_t g_xuiml_header = {
    .magic          = MODULE_MAGIC_NEWOS,
    .header_size    = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type    = MODULE_TYPE_SYSTEM_LIBRARY,
    .code_size      = 0,
    .entry_offset   = 64,
    .checksum       = 0,
    .security_flags = SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name    = "XUIML",
    .signature      = MODULE_AUTH_TAG
};
#endif

static volatile uint32_t g_reloc_info[2] = { 0x52454C4Fu, 0xCAFEBABEu };
static const kernel_api_t* g_api = NULL;

static void xuiml_apply_relocs(void) {
    if (g_reloc_info[0] != 0x52454C4Fu || g_reloc_info[1] == 0xCAFEBABEu) return;
    uintptr_t base = (uintptr_t)&g_xuiml_header;
    const uint32_t* tbl = (const uint32_t*)(base + g_reloc_info[1]);
    uint32_t count = tbl[0];
    for (uint32_t i = 0; i < count; i++) {
        uint32_t off = tbl[1 + i];
        *(uint64_t*)(base + off) += (uint64_t)base;
    }
    g_reloc_info[0] = 0;
}

typedef enum {
    XNODE_NONE = 0,
    XNODE_TAB,
    XNODE_GROUPBOX,
    XNODE_LABEL,
    XNODE_BUTTON,
    XNODE_TEXTBOX,
    XNODE_SLIDER,
    XNODE_LIST
} xnode_type_t;

#define MAX_XUIML_NODES 48
#define MAX_LIST_ITEMS  8

typedef struct {
    xnode_type_t type;
    int          tab_index; // -1: sempre visibile (footer/header), >=0: appartiene a una scheda
    char         id[24];
    char         text[72];
    int32_t      x, y, w, h;
    int          val;
    int          min_val;
    int          max_val;
    bool         password;
    bool         focused;
    uint32_t     color;     // 0 = default tema
    char         list_items[MAX_LIST_ITEMS][56];
    int          list_count;
    int          row_h;
} xuiml_node_t;

struct xuiml_doc {
    xuiml_node_t nodes[MAX_XUIML_NODES];
    int          num_nodes;
    int          num_tabs;
    int          active_tab;
    char         last_clicked_id[24];
};

static int x_strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static int x_atoi(const char* s) {
    int neg = 0, v = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    return neg ? -v : v;
}

static uint32_t x_parse_hex(const char* s) {
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    else if (s[0] == '#') s += 1;
    else return 0;
    uint32_t v = 0;
    while (*s) {
        char c = *s++;
        uint32_t d = 0;
        if (c >= '0' && c <= '9') d = (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = (uint32_t)(c - 'A' + 10);
        else break;
        v = (v << 4) | d;
    }
    return v;
}

static bool extract_attr(const char* tag, const char* attr, char* out, size_t max_out) {
    out[0] = '\0';
    size_t alen = dll_strlen(attr);
    const char* p = tag;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (!*p || *p == '>' || *p == '/') break;
        bool match = true;
        for (size_t i = 0; i < alen; i++) {
            if (p[i] != attr[i]) { match = false; break; }
        }
        if (match && p[alen] == '=') {
            p += alen + 1;
            char q = (*p == '"' || *p == '\'') ? *p++ : 0;
            size_t oi = 0;
            while (*p && (q ? (*p != q) : (*p != ' ' && *p != '>'))) {
                if (oi + 1 < max_out) out[oi++] = *p;
                p++;
            }
            out[oi] = '\0';
            return true;
        }
        while (*p && *p != ' ' && *p != '>' && *p != '"' && *p != '\'') p++;
        if (*p == '"' || *p == '\'') {
            char q = *p++;
            while (*p && *p != q) p++;
            if (*p == q) p++;
        }
    }
    return false;
}

static xuiml_node_t* find_node(xuiml_doc_t* doc, const char* id) {
    if (!doc || !id || !id[0]) return NULL;
    for (int i = 0; i < doc->num_nodes; i++) {
        if (x_strcmp(doc->nodes[i].id, id) == 0) return &doc->nodes[i];
    }
    return NULL;
}

static xuiml_doc_t* xuiml_load(const char* vfs_path) {
    if (!g_api || !vfs_path) return NULL;
    uint32_t sz = 0;
    char* src = (char*)g_api->vfs_read_entire_file(vfs_path, &sz);
    if (!src) return NULL;

    xuiml_doc_t* doc = (xuiml_doc_t*)g_api->kmalloc(sizeof(xuiml_doc_t));
    if (!doc) { g_api->kfree(src); return NULL; }
    for (size_t i = 0; i < sizeof(xuiml_doc_t); i++) ((uint8_t*)doc)[i] = 0;

    int cur_tab = -1;
    const char* p = src;
    const char* end = src + sz;
    char tag_buf[512];
    char val_buf[72];

    while (p < end && doc->num_nodes < MAX_XUIML_NODES) {
        while (p < end && *p != '<') p++;
        if (p >= end) break;
        p++; // skip '<'
        if (*p == '?' || *p == '!') {
            while (p < end && *p != '>') p++;
            if (p < end) p++;
            continue;
        }
        size_t ti = 0;
        while (p < end && *p != '>' && ti + 1 < sizeof(tag_buf)) {
            tag_buf[ti++] = *p++;
        }
        tag_buf[ti] = '\0';
        if (p < end && *p == '>') p++;

        if (tag_buf[0] == '/') {
            if (tag_buf[1] == 'T' && tag_buf[2] == 'a' && tag_buf[3] == 'b') {
                cur_tab = -1;
            }
            continue;
        }

        xnode_type_t type = XNODE_NONE;
        if (tag_buf[0] == 'T' && tag_buf[1] == 'a' && tag_buf[2] == 'b' && (tag_buf[3] == ' ' || tag_buf[3] == '\0')) type = XNODE_TAB;
        else if (tag_buf[0] == 'G' && tag_buf[1] == 'r') type = XNODE_GROUPBOX;
        else if (tag_buf[0] == 'L' && tag_buf[1] == 'a') type = XNODE_LABEL;
        else if (tag_buf[0] == 'B' && tag_buf[1] == 'u') type = XNODE_BUTTON;
        else if (tag_buf[0] == 'T' && tag_buf[1] == 'e') type = XNODE_TEXTBOX;
        else if (tag_buf[0] == 'S' && tag_buf[1] == 'l') type = XNODE_SLIDER;
        else if (tag_buf[0] == 'L' && tag_buf[1] == 'i') type = XNODE_LIST;

        if (type == XNODE_NONE) continue;

        xuiml_node_t* n = &doc->nodes[doc->num_nodes++];
        n->type = type;
        n->tab_index = (type == XNODE_TAB) ? doc->num_tabs : cur_tab;
        n->min_val = 1;
        n->max_val = 10;
        n->val = 5;
        n->row_h = 26;

        if (extract_attr(tag_buf, "id", val_buf, sizeof(val_buf))) dll_strcpy(n->id, val_buf, sizeof(n->id));
        if (extract_attr(tag_buf, "title", val_buf, sizeof(val_buf))) dll_strcpy(n->text, val_buf, sizeof(n->text));
        if (extract_attr(tag_buf, "text", val_buf, sizeof(val_buf))) dll_strcpy(n->text, val_buf, sizeof(n->text));
        if (extract_attr(tag_buf, "x", val_buf, sizeof(val_buf))) n->x = x_atoi(val_buf);
        if (extract_attr(tag_buf, "y", val_buf, sizeof(val_buf))) n->y = x_atoi(val_buf);
        if (extract_attr(tag_buf, "w", val_buf, sizeof(val_buf))) n->w = x_atoi(val_buf);
        if (extract_attr(tag_buf, "h", val_buf, sizeof(val_buf))) n->h = x_atoi(val_buf);
        if (extract_attr(tag_buf, "min", val_buf, sizeof(val_buf))) n->min_val = x_atoi(val_buf);
        if (extract_attr(tag_buf, "max", val_buf, sizeof(val_buf))) n->max_val = x_atoi(val_buf);
        if (extract_attr(tag_buf, "value", val_buf, sizeof(val_buf))) n->val = x_atoi(val_buf);
        if (extract_attr(tag_buf, "row_h", val_buf, sizeof(val_buf))) n->row_h = x_atoi(val_buf);
        if (extract_attr(tag_buf, "password", val_buf, sizeof(val_buf))) n->password = (x_atoi(val_buf) != 0);
        if (extract_attr(tag_buf, "focused", val_buf, sizeof(val_buf))) n->focused = (x_atoi(val_buf) != 0);
        if (extract_attr(tag_buf, "color", val_buf, sizeof(val_buf))) n->color = x_parse_hex(val_buf);

        if (type == XNODE_TAB) {
            cur_tab = doc->num_tabs++;
        }
    }

    g_api->kfree(src);
    return doc;
}

static void xuiml_free(xuiml_doc_t* doc) {
    if (doc && g_api && g_api->kfree) g_api->kfree(doc);
}

static void xuiml_render(xuiml_doc_t* doc, window_t* win) {
    if (!doc || !win || !g_api) return;
    dll_theme_t t;
    dll_theme_init(g_api, &t);

    int32_t cx = win->x + 6;
    int32_t cy = win->y + 28;
    int32_t cw = win->width - 12;
    int32_t ch = win->height - 34;

    dll_draw_dialog_bg(g_api, &t, cx, cy, cw, ch);

    if (doc->num_tabs > 0) {
        int32_t tab_y = cy + 4;
        int32_t tab_h = 22;
        for (int i = 0; i < doc->num_nodes; i++) {
            xuiml_node_t* n = &doc->nodes[i];
            if (n->type == XNODE_TAB) {
                g_api->wm_draw_themed_tab(cx + n->x, tab_y, n->w, tab_h, n->text, doc->active_tab == n->tab_index);
            }
        }
        dll_draw_tabs_divider(g_api, cx, tab_y, tab_h, cw, &t);
    }

    for (int i = 0; i < doc->num_nodes; i++) {
        xuiml_node_t* n = &doc->nodes[i];
        if (n->type == XNODE_TAB) continue;
        if (n->tab_index >= 0 && n->tab_index != doc->active_tab) continue;

        int32_t ax = cx + n->x;
        int32_t ay = cy + n->y;

        if (n->type == XNODE_GROUPBOX) {
            dll_draw_groupbox(g_api, ax, ay, n->w, n->h, n->text, &t);
        } else if (n->type == XNODE_LABEL) {
            if (n->text[0]) {
                uint32_t col = n->color ? n->color : t.text_primary;
                g_api->gfx_draw_string(ax, ay, n->text, col, 0, true);
            }
        } else if (n->type == XNODE_BUTTON) {
            g_api->wm_draw_themed_button(ax, ay, n->w, n->h, n->text, false);
        } else if (n->type == XNODE_TEXTBOX) {
            dll_draw_input_box(g_api, ax, ay, n->w, n->h, n->text, n->focused, n->password, &t);
        } else if (n->type == XNODE_SLIDER) {
            int range = (n->max_val > n->min_val) ? (n->max_val - n->min_val) : 1;
            int pos = n->val - n->min_val;
            if (pos < 0) pos = 0;
            if (pos > range) pos = range;
            g_api->gfx_fill_rounded_rect(ax, ay + 6, n->w, 4, 2, t.card_border);
            int32_t fill_w = (n->w * pos) / range;
            if (fill_w > 0) g_api->gfx_fill_rounded_rect(ax, ay + 6, fill_w, 4, 2, t.accent);
            int32_t kx = ax + fill_w - 5;
            if (kx < ax) kx = ax;
            if (kx > ax + n->w - 10) kx = ax + n->w - 10;
            g_api->gfx_fill_rounded_rect(kx, ay + 1, 10, 14, 3, t.accent);
        } else if (n->type == XNODE_LIST) {
            int32_t rh = n->row_h > 0 ? n->row_h : 26;
            int32_t step = rh + 4;
            for (int r = 0; r < n->list_count; r++) {
                int32_t ry = ay + r * step;
                if (ry + rh > ay + n->h) break;
                bool sel = (n->val == r);
                g_api->gfx_fill_rounded_rect(ax, ry, n->w, rh, 4, sel ? t.sel_bg : t.input_bg);
                g_api->gfx_draw_rounded_rect(ax, ry, n->w, rh, 4, sel ? t.accent : t.card_border);
                g_api->gfx_draw_string(ax + 10, ry + (rh - 12) / 2, n->list_items[r],
                                       sel ? t.sel_text : t.text_primary, 0, true);
            }
        }
    }
}

static const char* xuiml_mouse(xuiml_doc_t* doc, window_t* win, int32_t lx, int32_t ly, bool clicked) {
    if (!doc || !win || !clicked) return NULL;
    int32_t cx = 6, cy = 28;

    if (doc->num_tabs > 0 && ly >= cy + 4 && ly <= cy + 26) {
        for (int i = 0; i < doc->num_nodes; i++) {
            xuiml_node_t* n = &doc->nodes[i];
            if (n->type == XNODE_TAB && lx >= cx + n->x && lx < cx + n->x + n->w) {
                doc->active_tab = n->tab_index;
                dll_strcpy(doc->last_clicked_id, n->id, sizeof(doc->last_clicked_id));
                return doc->last_clicked_id;
            }
        }
    }

    for (int i = 0; i < doc->num_nodes; i++) {
        xuiml_node_t* n = &doc->nodes[i];
        if (n->type == XNODE_TAB) continue;
        if (n->tab_index >= 0 && n->tab_index != doc->active_tab) continue;

        int32_t ax = cx + n->x;
        int32_t ay = cy + n->y;

        if (n->type == XNODE_BUTTON && lx >= ax && lx <= ax + n->w && ly >= ay && ly <= ay + n->h) {
            dll_strcpy(doc->last_clicked_id, n->id, sizeof(doc->last_clicked_id));
            return doc->last_clicked_id;
        }
        if (n->type == XNODE_TEXTBOX && lx >= ax && lx <= ax + n->w && ly >= ay && ly <= ay + n->h) {
            for (int k = 0; k < doc->num_nodes; k++) {
                if (doc->nodes[k].type == XNODE_TEXTBOX) doc->nodes[k].focused = false;
            }
            n->focused = true;
            dll_strcpy(doc->last_clicked_id, n->id, sizeof(doc->last_clicked_id));
            return doc->last_clicked_id;
        }
        if (n->type == XNODE_SLIDER && lx >= ax && lx <= ax + n->w && ly >= ay - 2 && ly <= ay + 18) {
            int range = (n->max_val > n->min_val) ? (n->max_val - n->min_val) : 1;
            int val = n->min_val + ((lx - ax) * range) / (n->w > 0 ? n->w : 1);
            if (val < n->min_val) val = n->min_val;
            if (val > n->max_val) val = n->max_val;
            n->val = val;
            dll_strcpy(doc->last_clicked_id, n->id, sizeof(doc->last_clicked_id));
            return doc->last_clicked_id;
        }
        if (n->type == XNODE_LIST && lx >= ax && lx <= ax + n->w && ly >= ay && ly <= ay + n->h) {
            int32_t rh = n->row_h > 0 ? n->row_h : 26;
            int32_t step = rh + 4;
            int idx = (ly - ay) / step;
            if (idx >= 0 && idx < n->list_count) {
                n->val = idx;
                dll_strcpy(doc->last_clicked_id, n->id, sizeof(doc->last_clicked_id));
                return doc->last_clicked_id;
            }
        }
    }
    return NULL;
}

static bool xuiml_key(xuiml_doc_t* doc, window_t* win, char c) {
    if (!doc || !win) return false;
    if (c == 27 && g_api && g_api->wm_destroy_window) {
        g_api->wm_destroy_window(win->id);
        return true;
    }
    for (int i = 0; i < doc->num_nodes; i++) {
        xuiml_node_t* n = &doc->nodes[i];
        if (n->type == XNODE_TEXTBOX && n->focused &&
            (n->tab_index < 0 || n->tab_index == doc->active_tab)) {
            size_t len = dll_strlen(n->text);
            if (c == '\b') {
                if (len > 0) n->text[len - 1] = '\0';
                return true;
            }
            if (c >= 32 && c <= 126 && len + 1 < sizeof(n->text)) {
                n->text[len] = c;
                n->text[len + 1] = '\0';
                return true;
            }
        }
    }
    return false;
}

static void xuiml_set_text(xuiml_doc_t* doc, const char* id, const char* text) {
    xuiml_node_t* n = find_node(doc, id);
    if (n) dll_strcpy(n->text, text ? text : "", sizeof(n->text));
}

static const char* xuiml_get_text(xuiml_doc_t* doc, const char* id) {
    xuiml_node_t* n = find_node(doc, id);
    return n ? n->text : "";
}

static void xuiml_set_int(xuiml_doc_t* doc, const char* id, int val) {
    xuiml_node_t* n = find_node(doc, id);
    if (n) n->val = val;
}

static int xuiml_get_int(xuiml_doc_t* doc, const char* id) {
    xuiml_node_t* n = find_node(doc, id);
    return n ? n->val : 0;
}

static void xuiml_list_clear(xuiml_doc_t* doc, const char* id) {
    xuiml_node_t* n = find_node(doc, id);
    if (n) n->list_count = 0;
}

static void xuiml_list_add(xuiml_doc_t* doc, const char* id, const char* item_text) {
    xuiml_node_t* n = find_node(doc, id);
    if (n && n->list_count < MAX_LIST_ITEMS) {
        dll_strcpy(n->list_items[n->list_count++], item_text ? item_text : "", 56);
    }
}

static int xuiml_get_active_tab(xuiml_doc_t* doc) {
    return doc ? doc->active_tab : 0;
}

static void xuiml_set_active_tab(xuiml_doc_t* doc, int tab_idx) {
    if (doc && tab_idx >= 0 && tab_idx < doc->num_tabs) doc->active_tab = tab_idx;
}

static const xuiml_api_t g_xuiml_api = {
    .magic          = 0x4D495558u,
    .load           = xuiml_load,
    .free           = xuiml_free,
    .render         = xuiml_render,
    .mouse          = xuiml_mouse,
    .key            = xuiml_key,
    .set_text       = xuiml_set_text,
    .get_text       = xuiml_get_text,
    .set_int        = xuiml_set_int,
    .get_int        = xuiml_get_int,
    .list_clear     = xuiml_list_clear,
    .list_add       = xuiml_list_add,
    .get_active_tab = xuiml_get_active_tab,
    .set_active_tab = xuiml_set_active_tab
};

__attribute__((section(".text.entry"), used))
const xuiml_api_t* xuiml_dll_entry(const kernel_api_t* api) {
    xuiml_apply_relocs();
    if (!api) return NULL;
    g_api = api;
    return &g_xuiml_api;
}
