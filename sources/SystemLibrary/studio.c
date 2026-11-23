/**
 * SystemLibrary/studio.c
 * NewOS Studio — Visual Studio-style Fullscreen IDE, Form Designer .xuiml & Code Viewer
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

/* Palette Visual Studio Dark */
#define VS_BG_EDITOR     0xFF1E1E1Eu
#define VS_BG_PANEL      0xFF252526u
#define VS_BG_HEADER     0xFF2D2D30u
#define VS_BG_HOVER      0xFF3E3E42u
#define VS_BORDER        0xFF3F3F46u
#define VS_ACCENT        0xFF007ACCu
#define VS_ACCENT_LIGHT  0xFF1C97EAu
#define VS_TEXT          0xFFF1F1F1u
#define VS_TEXT_MUTED    0xFF9D9D9Du
#define VS_CODE_KW       0xFF569CD6u
#define VS_CODE_STR      0xFFD69D85u
#define VS_CODE_CMT      0xFF6A9955u
#define VS_CODE_FN       0xFFDCDCAAu
#define VS_ERR_GREEN     0xFF4EC9B0u

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

/* 0 = Form1.xuiml [Design], 1 = Form1.xuiml [Code], 2 = studio_app.c [Code] */
static int       g_doc_tab    = 0;
static bool      g_edit_prop_text = false;
static bool      g_edit_code  = false;
static uint64_t  g_last_click_tick = 0;
static int       g_last_click_idx  = -1;

static char      g_c_code[2048];
static char      g_output_lines[4][76];
static char      g_status[80] = "Pronto - Doppio click su un controllo per aprire il Code Viewer";
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

static void st_push_output(const char* line) {
    for (int i = 0; i < 3; i++) dll_strcpy(g_output_lines[i], g_output_lines[i + 1], 76);
    dll_strcpy(g_output_lines[3], line ? line : "", 76);
}

static void st_regen_c_code(const char* focus_id) {
    if (!g_rt || !g_rt->snprintf) return;
    int pos = 0;
    pos += g_rt->snprintf(g_c_code + pos, sizeof(g_c_code) - (size_t)pos,
        "// studio_app.c - Generato da NewOS Studio IDE\n"
        "#include <newos/sdk.h>\n\n"
        "static const nsrt_api_t* g_rt = NULL;\n\n"
        "static void on_ui_event(nsrt_app_t* app, const char* control_id, void* ctx) {\n"
        "    NS_UNUSED(ctx);\n"
        "    const nsrt_api_t* rt = g_rt;\n"
        "    if (!rt) return;\n");

    for (int i = 0; i < g_ctrl_count && pos < (int)sizeof(g_c_code) - 220; i++) {
        if (g_ctrls[i].type == ST_BUTTON || (focus_id && g_rt->strcmp(g_ctrls[i].id, focus_id) == 0)) {
            if (g_rt->strcmp(g_ctrls[i].id, "btn_exit") == 0) {
                pos += g_rt->snprintf(g_c_code + pos, sizeof(g_c_code) - (size_t)pos,
                    "    NS_ON_CLICK(\"%s\") { rt->close_app(app); }\n", g_ctrls[i].id);
            } else {
                pos += g_rt->snprintf(g_c_code + pos, sizeof(g_c_code) - (size_t)pos,
                    "    NS_ON_CLICK(\"%s\") {\n"
                    "        rt->set_text(app, \"lbl_msg\", \"Azione su %s!\");\n"
                    "    }\n", g_ctrls[i].id, g_ctrls[i].id);
            }
        }
    }

    pos += g_rt->snprintf(g_c_code + pos, sizeof(g_c_code) - (size_t)pos,
        "}\n\n"
        "void app_main(const kernel_api_t* kapi) {\n"
        "    g_rt = nsrt_load_library(kapi);\n"
        "    if (g_rt) g_rt->create_xuiml_app(\"Studio App\", 420, 280,\n"
        "        \"C:\\\\User\\\\studio_form.xuiml\", on_ui_event, NULL);\n"
        "}\n");
}

static void st_init_template(void) {
    g_ctrl_count = 0;
    g_seq = 1;
    g_edit_prop_text = false;
    g_edit_code = false;
    g_doc_tab = 0;

    g_ctrls[0] = (st_ctrl_t){ ST_GROUPBOX, "grp_main", " App NewOS Studio ",             14, 34, 390, 190 };
    g_ctrls[1] = (st_ctrl_t){ ST_LABEL,    "lbl_msg",  "Benvenuto nella tua App XUIML!", 28, 62, 300, 20 };
    g_ctrls[2] = (st_ctrl_t){ ST_CHECKBOX, "chk_opt",  "Abilita Sandbox ACL",            28, 94, 240, 22 };
    g_ctrls[3] = (st_ctrl_t){ ST_BUTTON,   "btn_ok",   "Esegui Azione",                  28, 174, 130, 28 };
    g_ctrls[4] = (st_ctrl_t){ ST_BUTTON,   "btn_exit", "Chiudi",                         264, 174, 120, 28 };
    g_ctrl_count = 5;
    g_sel_idx = 3;
    st_regen_c_code(NULL);
    st_push_output("Soluzione 'NewOSApp' caricata (5 controlli, target x86_64).");
    st_push_output("Suggerimento: doppio click su un controllo per aprire studio_app.c.");
    dll_strcpy(g_status, "Pronto - Doppio click su un controllo per aprire il Code Viewer", sizeof(g_status));
}

static void st_add_control(st_ctrl_type_t type) {
    if (g_ctrl_count >= ST_MAX_CTRLS) {
        st_push_output("[WARN] Limite massimo controlli raggiunto (20).");
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
    if (type == ST_BUTTON)   { dll_strcpy(c->text, "Pulsante", 48);        c->w = 110; c->h = 28; }
    if (type == ST_TEXTBOX)  { dll_strcpy(c->text, "Input...", 48);        c->w = 180; c->h = 24; }
    if (type == ST_CHECKBOX) { dll_strcpy(c->text, "Opzione", 48);         c->w = 160; c->h = 22; }
    if (type == ST_GROUPBOX) { dll_strcpy(c->text, " Gruppo ", 48);        c->w = 240; c->h = 100; }

    c->x = 28 + ((g_ctrl_count % 3) * 90);
    c->y = 128;
    g_sel_idx = g_ctrl_count++;
    g_doc_tab = 0;
    st_regen_c_code(NULL);
    st_push_output("Aggiunto nuovo controllo al form.");
    dll_strcpy(g_status, "Controllo aggiunto - Doppio click per generare handler C", sizeof(g_status));
}

static int st_build_xuiml(char* out, int max_len) {
    if (!g_rt || !g_rt->snprintf || !out || max_len < 256) return 0;
    int pos = 0;
    pos += g_rt->snprintf(out + pos, (size_t)(max_len - pos),
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<Form id=\"studio_app\" title=\"NewOS Studio Live\" width=\"420\" height=\"280\">\n"
        "  <Tab id=\"tab_main\" title=\"Principale\" x=\"8\" w=\"120\">\n");

    for (int i = 0; i < g_ctrl_count && pos < max_len - 160; i++) {
        const st_ctrl_t* c = &g_ctrls[i];
        if (c->type == ST_GROUPBOX) {
            pos += g_rt->snprintf(out + pos, (size_t)(max_len - pos),
                "    <GroupBox title=\"%s\" x=\"%d\" y=\"%d\" w=\"%d\" h=\"%d\" />\n",
                c->text, (int)c->x, (int)c->y, (int)c->w, (int)c->h);
        } else if (c->type == ST_LABEL) {
            pos += g_rt->snprintf(out + pos, (size_t)(max_len - pos),
                "    <Label id=\"%s\" text=\"%s\" x=\"%d\" y=\"%d\" />\n",
                c->id, c->text, (int)c->x, (int)c->y);
        } else {
            pos += g_rt->snprintf(out + pos, (size_t)(max_len - pos),
                "    <%s id=\"%s\" text=\"%s\" x=\"%d\" y=\"%d\" w=\"%d\" h=\"%d\" />\n",
                st_type_tag(c->type), c->id, c->text, (int)c->x, (int)c->y, (int)c->w, (int)c->h);
        }
    }
    pos += g_rt->snprintf(out + pos, (size_t)(max_len - pos), "  </Tab>\n</Form>\n");
    return pos;
}

static bool st_export_xuiml(void) {
    if (!g_rt) return false;
    char* buf = (char*)g_rt->alloc(4096);
    if (!buf) return false;
    int len = st_build_xuiml(buf, 4096);
    bool ok = g_rt->file_write("C:\\User\\studio_form.xuiml", buf, (uint32_t)len, true);
    g_rt->free(buf);
    st_push_output(ok ? "[BUILD] Esportato C:\\User\\studio_form.xuiml" : "[ERR] Fallito export .xuiml");
    dll_strcpy(g_status, ok ? "Esportato: C:\\User\\studio_form.xuiml" : "Errore export!", sizeof(g_status));
    return ok;
}

static void st_export_c_app(void) {
    if (!g_rt) return;
    size_t len = g_rt->strlen ? g_rt->strlen(g_c_code) : 0;
    bool ok = g_rt->file_write("C:\\User\\studio_app.c", g_c_code, (uint32_t)len, true);
    st_push_output(ok ? "[BUILD] Salvato C:\\User\\studio_app.c (NewSoft SDK v3.0)" : "[ERR] Fallito export studio_app.c");
    dll_strcpy(g_status, ok ? "Salvato: C:\\User\\studio_app.c" : "Errore salvataggio C!", sizeof(g_status));
}

static void st_preview_event(nsrt_app_t* app, const char* id, void* ctx) {
    (void)ctx;
    if (!g_rt || !app || !id) return;
    if (g_rt->strcmp(id, "btn_exit") == 0) { g_rt->close_app(app); return; }
    g_rt->set_text(app, "lbl_msg", "Evento click eseguito via NSRT.DLL!");
}

static void st_launch_preview(void) {
    if (!st_export_xuiml()) return;
    st_export_c_app();
    nsrt_app_t* p = g_rt->create_xuiml_app("NewOS Studio - Anteprima Live", 420, 280,
                                           "C:\\User\\studio_form.xuiml", st_preview_event, NULL);
    st_push_output(p ? "[RUN] Anteprima Live avviata con XUIML.DLL + NSRT.DLL" : "[ERR] Impossibile avviare anteprima");
    dll_strcpy(g_status, p ? "In esecuzione: Anteprima Live XUIML" : "Errore avvio anteprima", sizeof(g_status));
}

static void st_open_code_for_ctrl(int idx) {
    if (idx < 0 || idx >= g_ctrl_count) return;
    g_sel_idx = idx;
    st_regen_c_code(g_ctrls[idx].id);
    g_doc_tab = 2; /* Apre studio_app.c al posto del Form Preview */
    g_edit_code = true;
    g_edit_prop_text = false;
    if (g_rt && g_rt->snprintf) {
        char msg[72];
        g_rt->snprintf(msg, sizeof(msg), "[IDE] Generato handler NS_ON_CLICK(\"%s\") in studio_app.c", g_ctrls[idx].id);
        st_push_output(msg);
        g_rt->snprintf(g_status, sizeof(g_status), "Code Viewer: studio_app.c (focus su %s - digita per scrivere)", g_ctrls[idx].id);
    }
}

/* Helper grafico VS button flat */
static void vs_draw_btn(int32_t x, int32_t y, int32_t w, int32_t h, const char* lbl, uint32_t bg, uint32_t fg) {
    g_api->gfx_fill_rect(x, y, w, h, bg);
    g_api->gfx_draw_rect(x, y, w, h, VS_BORDER);
    g_api->gfx_draw_string(x + 8, y + (h - 14) / 2, lbl, fg, 0, true);
}

static void vs_draw_code_lines(int32_t x, int32_t y, int32_t w, int32_t h, const char* src) {
    g_api->gfx_fill_rect(x, y, w, h, VS_BG_EDITOR);
    g_api->gfx_fill_rect(x, y, 36, h, 0xFF1A1A1Au);
    g_api->gfx_fill_rect(x + 36, y, 1, h, VS_BORDER);

    int32_t row = 0;
    const char* p = src;
    while (*p && (row * 18 + 18) < h) {
        char line[88];
        int li = 0;
        while (*p && *p != '\n' && li < 84) line[li++] = *p++;
        if (*p == '\n') p++;
        line[li] = '\0';

        char lnum[8];
        if (g_rt && g_rt->snprintf) g_rt->snprintf(lnum, sizeof(lnum), "%d", row + 1);
        else { lnum[0] = '1'; lnum[1] = '\0'; }
        g_api->gfx_draw_string(x + 8, y + 6 + row * 18, lnum, 0xFF858585u, 0, true);

        uint32_t col = VS_TEXT;
        if (line[0] == '/' || line[0] == '<') col = (line[0] == '/') ? VS_CODE_CMT : VS_CODE_KW;
        else if (line[0] == '#') col = 0xFFC586C0u;
        /* Evidenzia righe con NS_ON_CLICK */
        if (line[4] == 'N' && line[5] == 'S' && line[6] == '_') col = VS_CODE_FN;
        g_api->gfx_draw_string(x + 44, y + 6 + row * 18, line, col, 0, true);
        row++;
    }
}

typedef struct { int32_t x, y, w, h; const char* lbl; int act; } vs_rect_btn_t;
static const vs_rect_btn_t g_tb_btns[] = {
    { 8,   28,  64, 22, "Nuovo",          1 },
    { 78,  28, 108, 22, "Salva XUIML",    8 },
    { 192, 28,  84, 22, "Salva C",        9 },
    { 282, 28, 140, 22, "> Avvia Preview",7 },
    { 428, 28,  76, 22, "Elimina",       16 }
};

static const vs_rect_btn_t g_prop_btns[] = {
    { 0, 152, 46, 20, "X-10", 10 },
    { 50,152, 46, 20, "X+10", 11 },
    { 100,152,46, 20, "Y-10", 12 },
    { 150,152,46, 20, "Y+10", 13 },
    { 0, 176, 62, 20, "W+20", 14 },
    { 66,176, 62, 20, "W-20", 15 }
};

static void studio_draw(window_t* win) {
    if (!g_api) return;
    int32_t cx = win->x + 4;
    int32_t cy = win->y + 28;
    int32_t cw = win->width - 8;
    int32_t ch = win->height - 32;

    /* Sfondo IDE */
    g_api->gfx_fill_rect(cx, cy, cw, ch, VS_BG_EDITOR);

    /* Menu Bar + Toolbar stile Visual Studio */
    g_api->gfx_fill_rect(cx, cy, cw, 24, VS_BG_HEADER);
    g_api->gfx_draw_string(cx + 10, cy + 5, "File   Modifica   Visualizza   Progetto   Compila   Debug   Aiuto", VS_TEXT, 0, true);
    g_api->gfx_draw_string(cx + cw - 150, cy + 5, "NewSoft SDK v3.0", VS_ACCENT_LIGHT, 0, true);

    g_api->gfx_fill_rect(cx, cy + 24, cw, 30, VS_BG_PANEL);
    g_api->gfx_fill_rect(cx, cy + 54, cw, 1, VS_BORDER);
    for (int i = 0; i < 5; i++) {
        uint32_t bg = (g_tb_btns[i].act == 7) ? VS_ACCENT : VS_BG_HEADER;
        vs_draw_btn(cx + g_tb_btns[i].x, cy + g_tb_btns[i].y, g_tb_btns[i].w, g_tb_btns[i].h, g_tb_btns[i].lbl, bg, VS_TEXT);
    }

    int32_t body_y = cy + 55;
    int32_t body_h = ch - 55 - 24; /* -24 status bar */
    int32_t out_h  = 96;
    int32_t mid_h  = body_h - out_h;

    /* 1. Sinistra: Toolbox (184 px) */
    int32_t tx = cx, tw = 184;
    g_api->gfx_fill_rect(tx, body_y, tw, body_h, VS_BG_PANEL);
    g_api->gfx_fill_rect(tx + tw - 1, body_y, 1, body_h, VS_BORDER);
    g_api->gfx_fill_rect(tx, body_y, tw - 1, 22, VS_BG_HEADER);
    g_api->gfx_draw_string(tx + 8, body_y + 4, "Toolbox", VS_TEXT, 0, true);
    g_api->gfx_draw_string(tx + 8, body_y + 28, "v Controlli Comuni", VS_TEXT_MUTED, 0, true);

    const char* tb_items[] = { "+ Label", "+ Button", "+ TextBox", "+ CheckBox", "+ GroupBox" };
    for (int i = 0; i < 5; i++) {
        vs_draw_btn(tx + 10, body_y + 50 + i * 30, tw - 22, 24, tb_items[i], VS_BG_HEADER, VS_TEXT);
    }

    /* 2. Destra: Esplora Soluzione + Proprieta' (244 px) */
    int32_t rw = 244;
    int32_t rx = cx + cw - rw;
    g_api->gfx_fill_rect(rx, body_y, rw, body_h, VS_BG_PANEL);
    g_api->gfx_fill_rect(rx, body_y, 1, body_h, VS_BORDER);

    /* Esplora Soluzione (sopra) */
    g_api->gfx_fill_rect(rx + 1, body_y, rw - 1, 22, VS_BG_HEADER);
    g_api->gfx_draw_string(rx + 10, body_y + 4, "Esplora Soluzione", VS_TEXT, 0, true);
    g_api->gfx_draw_string(rx + 10, body_y + 28, "Soluzione 'NewOSApp'", VS_TEXT, 0, true);
    g_api->gfx_draw_string(rx + 22, body_y + 48, "studio_form.xuiml [Design]", g_doc_tab == 0 ? VS_ACCENT_LIGHT : VS_TEXT_MUTED, 0, true);
    g_api->gfx_draw_string(rx + 22, body_y + 68, "studio_form.xuiml [XML]",    g_doc_tab == 1 ? VS_ACCENT_LIGHT : VS_TEXT_MUTED, 0, true);
    g_api->gfx_draw_string(rx + 22, body_y + 88, "studio_app.c [Code]",       g_doc_tab == 2 ? VS_ACCENT_LIGHT : VS_TEXT_MUTED, 0, true);
    g_api->gfx_draw_string(rx + 22, body_y + 108,"SDK: NSRT.DLL + XUIML",     VS_ERR_GREEN, 0, true);

    /* Proprieta' (sotto) */
    int32_t py = body_y + 134;
    g_api->gfx_fill_rect(rx + 1, py, rw - 1, 22, VS_BG_HEADER);
    g_api->gfx_draw_string(rx + 10, py + 4, "Proprieta'", VS_TEXT, 0, true);
    if (g_sel_idx >= 0 && g_sel_idx < g_ctrl_count && g_rt && g_rt->snprintf) {
        const st_ctrl_t* s = &g_ctrls[g_sel_idx];
        char r1[36], r2[36], r3[36];
        g_rt->snprintf(r1, sizeof(r1), "ID:   %s (%s)", s->id, st_type_tag(s->type));
        g_rt->snprintf(r2, sizeof(r2), "Pos:  X=%d  Y=%d", (int)s->x, (int)s->y);
        g_rt->snprintf(r3, sizeof(r3), "Size: W=%d  H=%d", (int)s->w, (int)s->h);
        g_api->gfx_draw_string(rx + 10, py + 30, r1, VS_ACCENT_LIGHT, 0, true);
        g_api->gfx_draw_string(rx + 10, py + 50, r2, VS_TEXT, 0, true);
        g_api->gfx_draw_string(rx + 10, py + 70, r3, VS_TEXT, 0, true);
        g_api->gfx_draw_string(rx + 10, py + 92, "Text (click per edit):", VS_TEXT_MUTED, 0, true);
        g_api->gfx_fill_rect(rx + 10, py + 110, rw - 22, 22, g_edit_prop_text ? 0xFF333338u : VS_BG_EDITOR);
        g_api->gfx_draw_rect(rx + 10, py + 110, rw - 22, 22, g_edit_prop_text ? VS_ACCENT : VS_BORDER);
        char tbuf[28];
        dll_strcpy(tbuf, s->text, 24);
        g_api->gfx_draw_string(rx + 14, py + 114, tbuf, VS_TEXT, 0, true);
    }
    for (int i = 0; i < 6; i++) {
        vs_draw_btn(rx + 10 + g_prop_btns[i].x, py + g_prop_btns[i].y,
                    g_prop_btns[i].w, g_prop_btns[i].h, g_prop_btns[i].lbl, VS_BG_HEADER, VS_TEXT);
    }

    /* 3. Centro: Document Well (tra Toolbox e Right Panel) */
    int32_t mx = tx + tw;
    int32_t mw = rx - mx;

    /* Document Tabs */
    g_api->gfx_fill_rect(mx, body_y, mw, 26, VS_BG_HEADER);
    const char* doc_tabs[] = { "Form1.xuiml [Design]", "Form1.xuiml", "studio_app.c" };
    int32_t tab_w = 180;
    for (int t = 0; t < 3; t++) {
        int32_t ttx = mx + t * tab_w;
        g_api->gfx_fill_rect(ttx, body_y, tab_w - 1, 26, (g_doc_tab == t) ? VS_BG_EDITOR : VS_BG_HEADER);
        if (g_doc_tab == t) g_api->gfx_fill_rect(ttx, body_y, tab_w - 1, 2, VS_ACCENT);
        g_api->gfx_draw_string(ttx + 10, body_y + 6, doc_tabs[t], (g_doc_tab == t) ? VS_TEXT : VS_TEXT_MUTED, 0, true);
    }

    int32_t doc_y = body_y + 26;
    int32_t doc_h = mid_h - 26;

    if (g_doc_tab == 0) {
        /* Form Designer Visuale */
        g_api->gfx_fill_rect(mx, doc_y, mw, doc_h, VS_BG_EDITOR);
        int32_t fw = 420, fh = 280;
        int32_t fx = mx + (mw - fw) / 2;
        int32_t fy = doc_y + (doc_h - fh) / 2;
        g_api->gfx_fill_rect(fx, fy, fw, fh, 0xFFF8FAFCu);
        g_api->gfx_draw_rect(fx, fy, fw, fh, VS_ACCENT);
        g_api->gfx_fill_rect(fx, fy, fw, 24, VS_ACCENT);
        g_api->gfx_draw_string(fx + 10, fy + 5, "Form1 - XUIML Designer (doppio click = codice C)", 0xFFFFFFFFu, 0, true);

        for (int32_t gy = fy + 34; gy < fy + fh - 6; gy += 16) {
            for (int32_t gx = fx + 10; gx < fx + fw - 6; gx += 16) {
                g_api->gfx_draw_pixel(gx, gy, 0xFFCBD5E1u);
            }
        }
        for (int i = 0; i < g_ctrl_count; i++) {
            const st_ctrl_t* c = &g_ctrls[i];
            int32_t ax = fx + c->x, ay = fy + c->y;
            if (c->type == ST_GROUPBOX) {
                g_api->gfx_draw_rect(ax, ay, c->w, c->h, 0xFF94A3B8u);
                g_api->gfx_fill_rect(ax + 6, ay - 6, 130, 14, 0xFFF8FAFCu);
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
                g_api->gfx_draw_rect(ax, ay + 3, 14, 14, VS_ACCENT);
                g_api->gfx_draw_string(ax + 20, ay + 3, c->text, 0xFF0F172Au, 0, true);
            }
            if (i == g_sel_idx) {
                g_api->gfx_draw_rect(ax - 2, ay - 2, c->w + 4, c->h + 4, VS_ACCENT);
            }
        }
    } else if (g_doc_tab == 1) {
        char* xbuf = g_rt ? (char*)g_rt->alloc(4096) : NULL;
        if (xbuf) {
            st_build_xuiml(xbuf, 4096);
            vs_draw_code_lines(mx, doc_y, mw, doc_h, xbuf);
            g_rt->free(xbuf);
        }
    } else {
        vs_draw_code_lines(mx, doc_y, mw, doc_h, g_c_code);
    }

    /* 4. Basso: Output & Error List */
    int32_t oy = body_y + mid_h;
    g_api->gfx_fill_rect(mx, oy, mw, out_h, VS_BG_PANEL);
    g_api->gfx_fill_rect(mx, oy, mw, 1, VS_BORDER);
    g_api->gfx_fill_rect(mx, oy + 1, mw, 22, VS_BG_HEADER);
    g_api->gfx_draw_string(mx + 10, oy + 5, "Output   |   Elenco Errori (0)   |   NSRT Log", VS_TEXT, 0, true);
    for (int i = 0; i < 4; i++) {
        g_api->gfx_draw_string(mx + 10, oy + 26 + i * 16, g_output_lines[i], VS_TEXT_MUTED, 0, true);
    }

    /* 5. Barra di Stato blu Visual Studio */
    int32_t sy = cy + ch - 24;
    g_api->gfx_fill_rect(cx, sy, cw, 24, VS_ACCENT);
    g_api->gfx_draw_string(cx + 10, sy + 5, g_status, 0xFFFFFFFFu, 0, true);
    if (g_rt && g_rt->snprintf) {
        char rinfo[48];
        g_rt->snprintf(rinfo, sizeof(rinfo), "Controlli: %d  |  x86_64", g_ctrl_count);
        g_api->gfx_draw_string(cx + cw - 175, sy + 5, rinfo, 0xFFFFFFFFu, 0, true);
    }
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
    else if (act == 11 && s->x + s->w < 410) s->x += 10;
    else if (act == 12 && s->y >= 34) s->y -= 10;
    else if (act == 13 && s->y + s->h < 270) s->y += 10;
    else if (act == 14 && s->w < 380) s->w += 20;
    else if (act == 15 && s->w > 40) s->w -= 20;
    else if (act == 16) {
        for (int i = g_sel_idx; i < g_ctrl_count - 1; i++) g_ctrls[i] = g_ctrls[i + 1];
        g_ctrl_count--;
        if (g_sel_idx >= g_ctrl_count) g_sel_idx = g_ctrl_count - 1;
        st_regen_c_code(NULL);
        st_push_output("[IDE] Controllo rimosso dal form.");
    }
}

static void studio_mouse(window_t* win, int32_t lx, int32_t ly, bool clicked) {
    if (!clicked) return;
    int32_t rx = lx - 4;
    int32_t ry = ly - 28;
    int32_t cw = win->width - 8;
    int32_t ch = win->height - 32;

    /* Toolbar */
    for (int i = 0; i < 5; i++) {
        if (rx >= g_tb_btns[i].x && rx < g_tb_btns[i].x + g_tb_btns[i].w &&
            ry >= g_tb_btns[i].y && ry < g_tb_btns[i].y + g_tb_btns[i].h) {
            g_edit_prop_text = false; g_edit_code = false;
            st_exec_action(g_tb_btns[i].act);
            return;
        }
    }

    int32_t body_y = 55;
    int32_t body_h = ch - 55 - 24;
    int32_t out_h  = 96;
    int32_t mid_h  = body_h - out_h;
    int32_t tw = 184;
    int32_t rw = 244;
    int32_t rx_panel = cw - rw;

    /* Toolbox clicks */
    if (rx >= 10 && rx < tw - 12 && ry >= body_y + 50 && ry < body_y + 200) {
        int ti = (ry - (body_y + 50)) / 30;
        if (ti >= 0 && ti < 5) {
            g_edit_prop_text = false; g_edit_code = false;
            st_exec_action(2 + ti);
            return;
        }
    }

    /* Esplora Soluzione clicks */
    if (rx >= rx_panel && ry >= body_y + 46 && ry < body_y + 108) {
        int si = (ry - (body_y + 46)) / 20;
        if (si >= 0 && si <= 2) { g_doc_tab = si; g_edit_code = (si == 2); return; }
    }

    /* Proprieta' */
    int32_t py = body_y + 134;
    if (rx >= rx_panel + 10 && rx < cw - 12 && ry >= py + 110 && ry < py + 132 && g_sel_idx >= 0) {
        g_edit_prop_text = true; g_edit_code = false;
        dll_strcpy(g_status, "Modifica testo controllo: digita e premi Invio", sizeof(g_status));
        return;
    }
    for (int i = 0; i < 6; i++) {
        int32_t bx = rx_panel + 10 + g_prop_btns[i].x;
        int32_t by = py + g_prop_btns[i].y;
        if (rx >= bx && rx < bx + g_prop_btns[i].w && ry >= by && ry < by + g_prop_btns[i].h) {
            g_edit_prop_text = false; g_edit_code = false;
            st_exec_action(g_prop_btns[i].act);
            return;
        }
    }

    /* Document Well Tabs */
    int32_t mx = tw, mw = rx_panel - mx;
    if (rx >= mx && rx < rx_panel && ry >= body_y && ry < body_y + 26) {
        int t = (rx - mx) / 180;
        if (t >= 0 && t < 3) { g_doc_tab = t; g_edit_code = (t == 2); return; }
    }

    /* Canvas Design: Click + Doppio Click */
    if (g_doc_tab == 0 && rx >= mx && rx < rx_panel && ry >= body_y + 26 && ry < body_y + mid_h) {
        int32_t doc_y = body_y + 26, doc_h = mid_h - 26;
        int32_t fx = mx + (mw - 420) / 2;
        int32_t fy = doc_y + (doc_h - 280) / 2;
        for (int i = g_ctrl_count - 1; i >= 0; i--) {
            const st_ctrl_t* c = &g_ctrls[i];
            if (rx >= fx + c->x && rx < fx + c->x + c->w &&
                ry >= fy + c->y && ry < fy + c->y + c->h) {
                uint64_t now = (g_api && g_api->timer_get_ticks) ? g_api->timer_get_ticks() : 0;
                if (g_last_click_idx == i && (now - g_last_click_tick) < 600) {
                    g_last_click_idx = -1;
                    st_open_code_for_ctrl(i);
                } else {
                    g_sel_idx = i;
                    g_last_click_idx = i;
                    g_last_click_tick = now;
                }
                return;
            }
        }
    }

    if (g_doc_tab == 2 && rx >= mx && rx < rx_panel && ry >= body_y + 26 && ry < body_y + mid_h) {
        g_edit_code = true;
        g_edit_prop_text = false;
    }
}

static void studio_key(window_t* win, char c) {
    (void)win;
    if (g_edit_prop_text && g_sel_idx >= 0 && g_sel_idx < g_ctrl_count) {
        st_ctrl_t* s = &g_ctrls[g_sel_idx];
        size_t len = 0; while (s->text[len]) len++;
        if (c == '\n' || c == '\r' || c == 0x1B) { g_edit_prop_text = false; return; }
        if (c == '\b') { if (len > 0) s->text[len - 1] = '\0'; return; }
        if (c >= 32 && c < 127 && len < 46) { s->text[len] = c; s->text[len + 1] = '\0'; }
        return;
    }

    if (g_doc_tab == 2 && g_edit_code) {
        size_t len = 0; while (g_c_code[len]) len++;
        if (c == '\b') { if (len > 0) g_c_code[len - 1] = '\0'; return; }
        if (c == '\r') c = '\n';
        if ((c == '\n' || (c >= 32 && c < 127)) && len < sizeof(g_c_code) - 2) {
            g_c_code[len] = c;
            g_c_code[len + 1] = '\0';
        }
    }
}

static void studio_open(void) {
    if (!g_api) return;
    if (!g_rt) g_rt = nsrt_load_library(g_api);
    if (g_ctrl_count == 0) st_init_template();

    uint32_t sw = g_api->gfx_get_width ? g_api->gfx_get_width() : 1024;
    uint32_t sh = g_api->gfx_get_height ? g_api->gfx_get_height() : 768;
    int32_t w = (int32_t)sw;
    int32_t h = (int32_t)sh - 36; /* Schermo intero sopra la Taskbar */

    g_studio_win = (window_t*)g_api->wm_create_window("NewOS Studio - Visual XUIML & SDK IDE", 0, 0, w, h, APP_ABOUT);
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
