/**
 * SystemLibrary/studio.c
 * NewOS Studio — Form Designer Visuale .xuiml & Generatore App SDK C
 * 100% Out-of-Kernel (C:\NewOS\SystemLibrary\STUDIO.DLL)
 * (C) Newspoint Software - NewSoft Dynamic System Library
 */

#include "cpl_dll_ui.h"
#include "nsrt.h"
#include "studio.h"

#ifndef BUILDING_KERNEL_LOADER
__attribute__((section(".header"), used))
module_header_t g_studio_header = {
    .magic          = MODULE_MAGIC_NEWOS,
    .header_size    = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type    = MODULE_TYPE_SYSTEM_LIBRARY,
    .code_size      = 0,
    .entry_offset   = 64,
    .checksum       = 0,
    .security_flags = SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name    = "STUDIO",
    .signature      = MODULE_AUTH_TAG
};
#endif

static volatile uint32_t g_reloc_info[2] = { 0x52454C4Fu, 0xCAFEBABEu };
static const kernel_api_t* g_api = NULL;
static const nsrt_api_t*   g_rt  = NULL;

static void studio_apply_relocs(void) {
    if (g_reloc_info[0] != 0x52454C4Fu || g_reloc_info[1] == 0xCAFEBABEu) return;
    uintptr_t base = (uintptr_t)&g_studio_header;
    const uint32_t* tbl = (const uint32_t*)(base + g_reloc_info[1]);
    uint32_t count = tbl[0];
    for (uint32_t i = 0; i < count; i++) {
        uint32_t off = tbl[1 + i];
        *(uint64_t*)(base + off) += (uint64_t)base;
    }
    g_reloc_info[0] = 0;
}

typedef enum {
    ST_LABEL = 0,
    ST_BUTTON,
    ST_TEXTBOX,
    ST_CHECKBOX,
    ST_GROUPBOX
} st_ctrl_type_t;

typedef struct {
    st_ctrl_type_t type;
    char id[24];
    char text[48];
    int32_t x, y, w, h;
} st_ctrl_t;

#define ST_MAX_CTRLS 20
static st_ctrl_t g_ctrls[ST_MAX_CTRLS];
static int       g_ctrl_count = 0;
static int       g_sel_idx    = -1;
static int       g_seq        = 1;
static bool      g_edit_text  = false;
static char      g_status[80] = "Pronto. Seleziona o aggiungi controlli per progettare il form .xuiml";
static window_t* g_studio_win = NULL;

static const char* st_type_tag(st_ctrl_type_t t) {
    switch (t) {
        case ST_LABEL:    return "Label";
        case ST_BUTTON:   return "Button";
        case ST_TEXTBOX:  return "TextBox";
        case ST_CHECKBOX: return "CheckBox";
        case ST_GROUPBOX: return "GroupBox";
        default:          return "Control";
    }
}

static void st_init_template(void) {
    g_ctrl_count = 0;
    g_seq = 1;
    g_edit_text = false;

    g_ctrls[0] = (st_ctrl_t){ ST_GROUPBOX, "grp_main", " App Generata con NewOS Studio ", 12, 34, 340, 170 };
    g_ctrls[1] = (st_ctrl_t){ ST_LABEL,    "lbl_msg",  "Benvenuto nella tua App XUIML!", 24, 60, 280, 20 };
    g_ctrls[2] = (st_ctrl_t){ ST_CHECKBOX, "chk_opt",  "Abilita modalita' Sandbox ACL",  24, 92, 260, 22 };
    g_ctrls[3] = (st_ctrl_t){ ST_BUTTON,   "btn_ok",   "Esegui Azione",                  24, 148, 120, 26 };
    g_ctrls[4] = (st_ctrl_t){ ST_BUTTON,   "btn_exit", "Chiudi",                         232, 148, 100, 26 };
    g_ctrl_count = 5;
    g_sel_idx = 3;
    dll_strcpy(g_status, "Nuovo progetto XUIML caricato (5 controlli).", sizeof(g_status));
}

static void st_add_control(st_ctrl_type_t type) {
    if (g_ctrl_count >= ST_MAX_CTRLS) {
        dll_strcpy(g_status, "Limite massimo controlli raggiunto (20).", sizeof(g_status));
        return;
    }
    st_ctrl_t* c = &g_ctrls[g_ctrl_count];
    c->type = type;
    if (g_rt && g_rt->snprintf) {
        g_rt->snprintf(c->id, sizeof(c->id), "%s_%d",
                       type == ST_BUTTON ? "btn" :
                       type == ST_LABEL  ? "lbl" :
                       type == ST_TEXTBOX ? "txt" :
                       type == ST_CHECKBOX ? "chk" : "grp", g_seq++);
    }
    if (type == ST_LABEL)    { dll_strcpy(c->text, "Nuova Etichetta", 48); c->w = 180; c->h = 20; }
    if (type == ST_BUTTON)   { dll_strcpy(c->text, "Pulsante", 48);        c->w = 110; c->h = 26; }
    if (type == ST_TEXTBOX)  { dll_strcpy(c->text, "Input...", 48);        c->w = 180; c->h = 24; }
    if (type == ST_CHECKBOX) { dll_strcpy(c->text, "Opzione", 48);         c->w = 160; c->h = 22; }
    if (type == ST_GROUPBOX) { dll_strcpy(c->text, " Gruppo ", 48);        c->w = 240; c->h = 100; }

    c->x = 24 + ((g_ctrl_count % 3) * 80);
    c->y = 114;
    g_sel_idx = g_ctrl_count++;
    g_edit_text = false;
    dll_strcpy(g_status, "Controllo aggiunto al form.", sizeof(g_status));
}

static int st_build_xuiml(char* out, int max_len) {
    if (!g_rt || !g_rt->snprintf || !out || max_len < 256) return 0;
    int pos = 0;
    pos += g_rt->snprintf(out + pos, (size_t)(max_len - pos),
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<Form id=\"studio_app\" title=\"NewOS Studio Live Preview\" width=\"380\" height=\"260\">\n"
        "    <Tab id=\"tab_main\" title=\"Principale\" x=\"8\" w=\"120\">\n");

    for (int i = 0; i < g_ctrl_count && pos < max_len - 160; i++) {
        const st_ctrl_t* c = &g_ctrls[i];
        if (c->type == ST_GROUPBOX) {
            pos += g_rt->snprintf(out + pos, (size_t)(max_len - pos),
                "        <GroupBox title=\"%s\" x=\"%d\" y=\"%d\" w=\"%d\" h=\"%d\" />\n",
                c->text, (int)c->x, (int)c->y, (int)c->w, (int)c->h);
        } else if (c->type == ST_LABEL) {
            pos += g_rt->snprintf(out + pos, (size_t)(max_len - pos),
                "        <Label id=\"%s\" text=\"%s\" x=\"%d\" y=\"%d\" />\n",
                c->id, c->text, (int)c->x, (int)c->y);
        } else {
            pos += g_rt->snprintf(out + pos, (size_t)(max_len - pos),
                "        <%s id=\"%s\" text=\"%s\" x=\"%d\" y=\"%d\" w=\"%d\" h=\"%d\" />\n",
                st_type_tag(c->type), c->id, c->text, (int)c->x, (int)c->y, (int)c->w, (int)c->h);
        }
    }

    pos += g_rt->snprintf(out + pos, (size_t)(max_len - pos),
        "    </Tab>\n</Form>\n");
    return pos;
}

static bool st_export_xuiml(void) {
    if (!g_rt) return false;
    char* buf = (char*)g_rt->alloc(4096);
    if (!buf) return false;
    int len = st_build_xuiml(buf, 4096);
    bool ok = g_rt->file_write("C:\\User\\studio_form.xuiml", buf, (uint32_t)len, true);
    g_rt->free(buf);
    dll_strcpy(g_status, ok ? "Esportato: C:\\User\\studio_form.xuiml" : "Errore export .xuiml!", sizeof(g_status));
    return ok;
}

static void st_export_c_app(void) {
    if (!g_rt || !g_rt->snprintf) return;
    char* buf = (char*)g_rt->alloc(4096);
    if (!buf) return;
    int pos = 0;
    pos += g_rt->snprintf(buf + pos, 4096 - pos,
        "/* Generato da NewOS Studio IDE */\n"
        "#include <newos/sdk.h>\n\n"
        "static const nsrt_api_t* g_rt = NULL;\n\n"
        "static void on_ui_event(nsrt_app_t* app, const char* control_id, void* ctx) {\n"
        "    NS_UNUSED(ctx);\n"
        "    const nsrt_api_t* rt = g_rt;\n"
        "    if (!rt) return;\n");

    for (int i = 0; i < g_ctrl_count && pos < 3600; i++) {
        if (g_ctrls[i].type == ST_BUTTON) {
            if (g_rt->strcmp(g_ctrls[i].id, "btn_exit") == 0) {
                pos += g_rt->snprintf(buf + pos, (size_t)(4096 - pos),
                    "    NS_ON_CLICK(\"%s\") { rt->close_app(app); }\n", g_ctrls[i].id);
            } else {
                pos += g_rt->snprintf(buf + pos, (size_t)(4096 - pos),
                    "    NS_ON_CLICK(\"%s\") { NS_LOG_INFO(rt, \"Click %s\"); }\n", g_ctrls[i].id, g_ctrls[i].id);
            }
        }
    }

    pos += g_rt->snprintf(buf + pos, (size_t)(4096 - pos),
        "}\n\n"
        "void app_main(const kernel_api_t* kapi) {\n"
        "    g_rt = nsrt_load_library(kapi);\n"
        "    if (g_rt) g_rt->create_xuiml_app(\"Studio App\", 380, 260, \"C:\\\\User\\\\studio_form.xuiml\", on_ui_event, NULL);\n"
        "}\n");

    bool ok = g_rt->file_write("C:\\User\\studio_app.c", buf, (uint32_t)pos, true);
    g_rt->free(buf);
    dll_strcpy(g_status, ok ? "Generato codice C: C:\\User\\studio_app.c" : "Errore scrittura app.c!", sizeof(g_status));
}

static void st_preview_event(nsrt_app_t* app, const char* id, void* ctx) {
    (void)ctx;
    if (!g_rt || !app || !id) return;
    if (g_rt->strcmp(id, "btn_exit") == 0) {
        g_rt->close_app(app);
        return;
    }
    g_rt->set_text(app, "lbl_msg", "Evento click ricevuto da NSRT.DLL!");
}

static void st_launch_preview(void) {
    if (!st_export_xuiml()) return;
    nsrt_app_t* p = g_rt->create_xuiml_app("NewOS Studio - Anteprima Live", 380, 260,
                                           "C:\\User\\studio_form.xuiml", st_preview_event, NULL);
    dll_strcpy(g_status, p ? "Anteprima Live avviata via XUIML.DLL!" : "Errore avvio anteprima!", sizeof(g_status));
}

/* --- Rendering GUI Liquid Glass --- */
typedef struct {
    int32_t x, y, w, h;
    const char* label;
    int action;
} st_btn_t;

static const st_btn_t g_toolbar[] = {
    { 12,  8,  60, 24, "Nuovo",    1 },
    { 78,  8,  66, 24, "+Label",   2 },
    { 148, 8,  72, 24, "+Button",  3 },
    { 224, 8,  78, 24, "+TextBox", 4 },
    { 306, 8,  82, 24, "+CheckBox",5 },
    { 392, 8,  82, 24, "+GroupBox",6 },
    { 488, 8,  94, 24, "Anteprima",7 },
    { 586, 8,  86, 24, "Esporta",  8 },
    { 676, 8,  92, 24, "Gen. C",   9 }
};

static const st_btn_t g_prop_btns[] = {
    { 594, 194, 40, 22, "X-10", 10 },
    { 638, 194, 40, 22, "X+10", 11 },
    { 682, 194, 40, 22, "Y-10", 12 },
    { 726, 194, 40, 22, "Y+10", 13 },
    { 594, 222, 56, 22, "W+20", 14 },
    { 654, 222, 56, 22, "W-20", 15 },
    { 714, 222, 54, 22, "Elim.",16 }
};

static void studio_draw(window_t* win) {
    if (!g_api) return;
    int32_t cx = win->x + 6;
    int32_t cy = win->y + 30;
    int32_t cw = win->width - 12;
    int32_t ch = win->height - 36;

    /* Sfondo pannello Studio */
    g_api->gfx_fill_rect(cx, cy, cw, ch, 0xFFF3F6FAu);

    /* Toolbar */
    g_api->gfx_fill_rect(cx, cy, cw, 38, 0xFFE6ECF5u);
    g_api->gfx_fill_rect(cx, cy + 38, cw, 1, 0xFFC5D0E0u);
    for (int i = 0; i < 9; i++) {
        g_api->wm_draw_themed_button(cx + g_toolbar[i].x, cy + g_toolbar[i].y,
                                     g_toolbar[i].w, g_toolbar[i].h, g_toolbar[i].label, false);
    }

    /* Pannello Sinistra: Albero Controlli */
    int32_t lx = cx + 8, ly = cy + 46, lw = 160, lh = ch - 82;
    g_api->gfx_fill_rect(lx, ly, lw, lh, 0xFFFFFFFFu);
    g_api->gfx_draw_rect(lx, ly, lw, lh, 0xFFC5D0E0u);
    g_api->gfx_fill_rect(lx, ly, lw, 22, 0xFFE9EFF8u);
    g_api->gfx_draw_string(lx + 8, ly + 4, "Controlli Form", 0xFF1E293Bu, 0, true);

    for (int i = 0; i < g_ctrl_count && i < 15; i++) {
        int32_t ry = ly + 26 + i * 22;
        if (i == g_sel_idx) g_api->gfx_fill_rect(lx + 2, ry, lw - 4, 20, 0xFFDBEAFEu);
        char line[28];
        if (g_rt && g_rt->snprintf) {
            g_rt->snprintf(line, sizeof(line), "%s: %s", st_type_tag(g_ctrls[i].type), g_ctrls[i].id);
            line[21] = '\0';
        } else {
            dll_strcpy(line, g_ctrls[i].id, sizeof(line));
        }
        g_api->gfx_draw_string(lx + 8, ry + 3, line, (i == g_sel_idx) ? 0xFF1D4ED8u : 0xFF334155u, 0, true);
    }

    /* Pannello Centro: Canvas Visuale con Griglia Snap 10px */
    int32_t vx = cx + 176, vy = cy + 46, vw = 404, vh = ch - 82;
    g_api->gfx_fill_rect(vx, vy, vw, vh, 0xFFE2E8F0u);
    g_api->gfx_draw_rect(vx, vy, vw, vh, 0xFFCBD5E1u);

    /* Finestra Target (380x260) centrata nel canvas */
    int32_t fx = vx + 12, fy = vy + 24, fw = 380, fh = 260;
    g_api->gfx_fill_rect(fx, fy, fw, fh, 0xFFF8FAFCu);
    g_api->gfx_draw_rect(fx, fy, fw, fh, 0xFF94A3B8u);
    g_api->gfx_fill_rect(fx, fy, fw, 24, 0xFF1D6FE8u);
    g_api->gfx_draw_string(fx + 10, fy + 5, "Form Preview (380x260) - XUIML", 0xFFFFFFFFu, 0, true);

    /* Griglia puntinata */
    for (int32_t gy = fy + 34; gy < fy + fh - 6; gy += 20) {
        for (int32_t gx = fx + 10; gx < fx + fw - 6; gx += 20) {
            g_api->gfx_draw_pixel(gx, gy, 0xFFCBD5E1u);
        }
    }

    /* Disegna controlli sul canvas */
    for (int i = 0; i < g_ctrl_count; i++) {
        const st_ctrl_t* c = &g_ctrls[i];
        int32_t ax = fx + c->x;
        int32_t ay = fy + c->y;
        if (c->type == ST_GROUPBOX) {
            g_api->gfx_draw_rect(ax, ay, c->w, c->h, 0xFF94A3B8u);
            g_api->gfx_fill_rect(ax + 6, ay - 6, 120, 14, 0xFFF8FAFCu);
            g_api->gfx_draw_string(ax + 8, ay - 5, c->text, 0xFF334155u, 0, true);
        } else if (c->type == ST_LABEL) {
            g_api->gfx_draw_string(ax, ay + 2, c->text, 0xFF0F172Au, 0, true);
        } else if (c->type == ST_BUTTON) {
            g_api->wm_draw_themed_button(ax, ay, c->w, c->h, c->text, false);
        } else if (c->type == ST_TEXTBOX) {
            g_api->gfx_fill_rect(ax, ay, c->w, c->h, 0xFFFFFFFFu);
            g_api->gfx_draw_rect(ax, ay, c->w, c->h, 0xFF94A3B8u);
            g_api->gfx_draw_string(ax + 6, ay + 4, c->text, 0xFF475569u, 0, true);
        } else if (c->type == ST_CHECKBOX) {
            g_api->gfx_fill_rect(ax, ay + 3, 14, 14, 0xFFFFFFFFu);
            g_api->gfx_draw_rect(ax, ay + 3, 14, 14, 0xFF1D6FE8u);
            g_api->gfx_draw_string(ax + 20, ay + 3, c->text, 0xFF0F172Au, 0, true);
        }

        if (i == g_sel_idx) {
            g_api->gfx_draw_rect(ax - 2, ay - 2, c->w + 4, c->h + 4, 0xFFEF4444u);
        }
    }

    /* Pannello Destra: Ispettore Proprieta' */
    int32_t px = cx + 588, py = cy + 46, pw = cw - 596, ph = ch - 82;
    g_api->gfx_fill_rect(px, py, pw, ph, 0xFFFFFFFFu);
    g_api->gfx_draw_rect(px, py, pw, ph, 0xFFC5D0E0u);
    g_api->gfx_fill_rect(px, py, pw, 22, 0xFFE9EFF8u);
    g_api->gfx_draw_string(px + 8, py + 4, "Ispettore Proprieta'", 0xFF1E293Bu, 0, true);

    if (g_sel_idx >= 0 && g_sel_idx < g_ctrl_count && g_rt && g_rt->snprintf) {
        const st_ctrl_t* s = &g_ctrls[g_sel_idx];
        char l1[40], l2[40], l3[56], l4[40];
        g_rt->snprintf(l1, sizeof(l1), "Tipo: %s", st_type_tag(s->type));
        g_rt->snprintf(l2, sizeof(l2), "ID:   %s", s->id);
        g_rt->snprintf(l3, sizeof(l3), "%s", s->text);
        l3[22] = '\0';
        g_rt->snprintf(l4, sizeof(l4), "Pos: (%d,%d) %dx%d", (int)s->x, (int)s->y, (int)s->w, (int)s->h);

        g_api->gfx_draw_string(px + 8, py + 30, l1, 0xFF334155u, 0, true);
        g_api->gfx_draw_string(px + 8, py + 50, l2, 0xFF1D4ED8u, 0, true);
        g_api->gfx_draw_string(px + 8, py + 72, "Testo (clicca edit):", 0xFF64748Bu, 0, true);
        g_api->gfx_fill_rect(px + 8, py + 90, pw - 16, 22, g_edit_text ? 0xFFFEF9C3u : 0xFFF8FAFCu);
        g_api->gfx_draw_rect(px + 8, py + 90, pw - 16, 22, g_edit_text ? 0xFF1D6FE8u : 0xFFCBD5E1u);
        g_api->gfx_draw_string(px + 12, py + 94, l3, 0xFF0F172Au, 0, true);
        g_api->gfx_draw_string(px + 8, py + 120, l4, 0xFF475569u, 0, true);
    }

    for (int i = 0; i < 7; i++) {
        g_api->wm_draw_themed_button(cx + g_prop_btns[i].x, cy + g_prop_btns[i].y,
                                     g_prop_btns[i].w, g_prop_btns[i].h, g_prop_btns[i].label, false);
    }

    /* Info SDK in basso a destra */
    g_api->gfx_draw_string(px + 8, py + 216, "Output in C:\\User\\:", 0xFF64748Bu, 0, true);
    g_api->gfx_draw_string(px + 8, py + 236, "studio_form.xuiml", 0xFF107C41u, 0, true);
    g_api->gfx_draw_string(px + 8, py + 254, "studio_app.c", 0xFF107C41u, 0, true);

    /* Barra di stato */
    g_api->gfx_fill_rect(cx, cy + ch - 28, cw, 28, 0xFFE6ECF5u);
    g_api->gfx_fill_rect(cx, cy + ch - 28, cw, 1, 0xFFC5D0E0u);
    g_api->gfx_draw_string(cx + 10, cy + ch - 20, g_status, 0xFF107C41u, 0, true);
}

static void st_exec_action(int act) {
    if (act == 1) { st_init_template(); return; }
    if (act == 2) { st_add_control(ST_LABEL); return; }
    if (act == 3) { st_add_control(ST_BUTTON); return; }
    if (act == 4) { st_add_control(ST_TEXTBOX); return; }
    if (act == 5) { st_add_control(ST_CHECKBOX); return; }
    if (act == 6) { st_add_control(ST_GROUPBOX); return; }
    if (act == 7) { st_launch_preview(); return; }
    if (act == 8) { st_export_xuiml(); return; }
    if (act == 9) { st_export_c_app(); return; }

    if (g_sel_idx < 0 || g_sel_idx >= g_ctrl_count) return;
    st_ctrl_t* s = &g_ctrls[g_sel_idx];
    if (act == 10 && s->x >= 10) s->x -= 10;
    else if (act == 11 && s->x + s->w < 370) s->x += 10;
    else if (act == 12 && s->y >= 34) s->y -= 10;
    else if (act == 13 && s->y + s->h < 250) s->y += 10;
    else if (act == 14 && s->w < 340) s->w += 20;
    else if (act == 15 && s->w > 40) s->w -= 20;
    else if (act == 16) {
        for (int i = g_sel_idx; i < g_ctrl_count - 1; i++) g_ctrls[i] = g_ctrls[i + 1];
        g_ctrl_count--;
        if (g_sel_idx >= g_ctrl_count) g_sel_idx = g_ctrl_count - 1;
        dll_strcpy(g_status, "Controllo eliminato.", sizeof(g_status));
    }
}

static void studio_mouse(window_t* win, int32_t lx, int32_t ly, bool clicked) {
    if (!clicked) return;
    int32_t rx = lx - 6;
    int32_t ry = ly - 30;

    for (int i = 0; i < 9; i++) {
        if (rx >= g_toolbar[i].x && rx < g_toolbar[i].x + g_toolbar[i].w &&
            ry >= g_toolbar[i].y && ry < g_toolbar[i].y + g_toolbar[i].h) {
            g_edit_text = false;
            st_exec_action(g_toolbar[i].action);
            return;
        }
    }
    for (int i = 0; i < 7; i++) {
        if (rx >= g_prop_btns[i].x && rx < g_prop_btns[i].x + g_prop_btns[i].w &&
            ry >= g_prop_btns[i].y && ry < g_prop_btns[i].y + g_prop_btns[i].h) {
            g_edit_text = false;
            st_exec_action(g_prop_btns[i].action);
            return;
        }
    }

    /* Click su casella testo proprieta' */
    if (rx >= 596 && rx < win->width - 20 && ry >= 136 && ry <= 160 && g_sel_idx >= 0) {
        g_edit_text = true;
        dll_strcpy(g_status, "Digita il nuovo testo e premi Invio.", sizeof(g_status));
        return;
    }
    g_edit_text = false;

    /* Click su lista controlli a sinistra */
    if (rx >= 8 && rx < 168 && ry >= 72) {
        int idx = (ry - 72) / 22;
        if (idx >= 0 && idx < g_ctrl_count) { g_sel_idx = idx; return; }
    }

    /* Click su controllo nel canvas */
    int32_t fx = 188, fy = 70;
    for (int i = g_ctrl_count - 1; i >= 0; i--) {
        const st_ctrl_t* c = &g_ctrls[i];
        if (rx >= fx + c->x && rx < fx + c->x + c->w &&
            ry >= fy + c->y && ry < fy + c->y + c->h) {
            g_sel_idx = i;
            return;
        }
    }
}

static void studio_key(window_t* win, char c) {
    (void)win;
    if (!g_edit_text || g_sel_idx < 0 || g_sel_idx >= g_ctrl_count) return;
    st_ctrl_t* s = &g_ctrls[g_sel_idx];
    size_t len = 0;
    while (s->text[len]) len++;

    if (c == '\n' || c == '\r' || c == 0x1B) {
        g_edit_text = false;
        dll_strcpy(g_status, "Testo controllo aggiornato.", sizeof(g_status));
        return;
    }
    if (c == '\b') {
        if (len > 0) s->text[len - 1] = '\0';
        return;
    }
    if (c >= 32 && c < 127 && len < 46) {
        s->text[len] = c;
        s->text[len + 1] = '\0';
    }
}

static void studio_open(void) {
    if (!g_api) return;
    if (!g_rt) g_rt = nsrt_load_library(g_api);
    if (g_ctrl_count == 0) st_init_template();

    uint32_t sw = g_api->gfx_get_width ? g_api->gfx_get_width() : 1024;
    uint32_t sh = g_api->gfx_get_height ? g_api->gfx_get_height() : 768;
    int32_t w = 780, h = 470;
    int32_t x = ((int32_t)sw - w) / 2;
    int32_t y = ((int32_t)sh - h) / 2 - 14;
    if (x < 8) x = 8;
    if (y < 8) y = 8;

    g_studio_win = (window_t*)g_api->wm_create_window("NewOS Studio - XUIML Form Designer & IDE", x, y, w, h, APP_ABOUT);
    if (!g_studio_win) return;
    g_studio_win->on_draw  = studio_draw;
    g_studio_win->on_mouse = studio_mouse;
    g_studio_win->on_key   = studio_key;
}

static const studio_api_t g_studio_api = {
    .magic = STUDIO_MAGIC,
    .open  = studio_open
};

__attribute__((section(".entry"), used))
const studio_api_t* studio_dll_entry(const kernel_api_t* api) {
    studio_apply_relocs();
    g_api = api;
    return &g_studio_api;
}
