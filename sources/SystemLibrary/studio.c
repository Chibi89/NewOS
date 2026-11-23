/**
 * SystemLibrary/studio.c
 * NewOS Studio — Visual Studio-style Fullscreen IDE, Form Designer .xuiml &
 * Code Viewer 100% Out-of-Kernel (C:\NewOS\SystemLibrary\STUDIO.DLL) (C)
 * Newspoint Software - NewSoft Dynamic System Library
 */

#include "studio.h"
#include "cpl_dll_ui.h"
#include "nsrt.h"
#include <gui/theme_mgr.h>

#ifndef BUILDING_KERNEL_LOADER
__attribute__((section(".header"), used)) module_header_t g_studio_header = {
    .magic = MODULE_MAGIC_NEWOS,
    .header_size = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type = MODULE_TYPE_SYSTEM_LIBRARY,
    .code_size = 0,
    .entry_offset = 64,
    .checksum = 0,
    .security_flags =
        SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name = "STUDIO",
    .signature = MODULE_AUTH_TAG};
#endif

static volatile uint32_t g_reloc_info[2] = {0x52454C4Fu, 0xCAFEBABEu};
static const kernel_api_t *g_api = NULL;
static const nsrt_api_t *g_rt = NULL;

static void studio_apply_relocs(void) {
  if (g_reloc_info[0] != 0x52454C4Fu || g_reloc_info[1] == 0xCAFEBABEu)
    return;
  uintptr_t base = (uintptr_t)&g_studio_header;
  const uint32_t *tbl = (const uint32_t *)(base + g_reloc_info[1]);
  uint32_t count = tbl[0];
  for (uint32_t i = 0; i < count; i++) {
    uint32_t off = tbl[1 + i];
    *(uint64_t *)(base + off) += (uint64_t)base;
  }
  g_reloc_info[0] = 0;
}

/* Palette dinamica agganciata al tema attivo di sistema (o override VS Dark) */
typedef struct {
  uint32_t bg_editor;
  uint32_t bg_panel;
  uint32_t bg_header;
  uint32_t bg_gutter;
  uint32_t border;
  uint32_t accent;
  uint32_t accent_text;
  uint32_t text;
  uint32_t text_muted;
  uint32_t code_kw;
  uint32_t code_prep;
  uint32_t code_cmt;
  uint32_t code_fn;
  uint32_t status_ok;
  bool is_dark;
  bool is_glass;
  bool modern;
  uint8_t radius;
} vs_palette_t;

static bool g_force_dark_theme = false;

static vs_palette_t st_get_palette(void) {
  vs_palette_t p;
  if (!g_force_dark_theme && g_api) {
    dll_theme_t dt;
    dll_theme_init(g_api, &dt);
    p.is_dark = dt.is_dark;
    p.is_glass = dt.is_glass;
    p.modern = !dt.is_classic;
    p.radius = (uint8_t)(dt.panel_radius > 0 ? dt.panel_radius : 6);
    p.bg_editor = 0xFF000000u | (dt.input_bg & 0x00FFFFFFu);
    p.bg_panel = 0xFF000000u | (dt.card_bg & 0x00FFFFFFu);
    p.bg_header = 0xFF000000u | ((dt.th && dt.th->btn_hover)
                                     ? (dt.th->btn_hover & 0x00FFFFFFu)
                                     : (dt.is_dark ? 0x2D2D36u : 0xE6ECF5u));
    p.bg_gutter =
        0xFF000000u | (dt.is_dark ? 0x1A1A22u : (dt.card_bg & 0x00FFFFFFu));
    p.border = 0xFF000000u | (dt.card_border & 0x00FFFFFFu);
    p.accent = 0xFF000000u | (dt.accent & 0x00FFFFFFu);
    p.accent_text = 0xFF000000u | (dt.accent_text & 0x00FFFFFFu);
    p.text = 0xFF000000u | (dt.text_primary & 0x00FFFFFFu);
    p.text_muted = 0xFF000000u | (dt.text_secondary & 0x00FFFFFFu);
    p.code_kw = dt.is_dark ? 0xFF569CD6u : 0xFF0000FFu;
    p.code_prep = dt.is_dark ? 0xFFC586C0u : 0xFFAF00DBu;
    p.code_cmt = dt.is_dark ? 0xFF6A9955u : 0xFF008000u;
    p.code_fn = dt.is_dark ? 0xFFDCDCAAu : 0xFF795E26u;
    p.status_ok = dt.is_dark ? 0xFF4EC9B0u : 0xFF107C41u;
    return p;
  }

  /* Fallback / Override Visual Studio Dark */
  p.is_dark = true;
  p.is_glass = false;
  p.modern = true;
  p.radius = 4;
  p.bg_editor = 0xFF1E1E1Eu;
  p.bg_panel = 0xFF252526u;
  p.bg_header = 0xFF2D2D30u;
  p.bg_gutter = 0xFF1A1A1Au;
  p.border = 0xFF3F3F46u;
  p.accent = 0xFF007ACCu;
  p.accent_text = 0xFFFFFFFFu;
  p.text = 0xFFF1F1F1u;
  p.text_muted = 0xFF9D9D9Du;
  p.code_kw = 0xFF569CD6u;
  p.code_prep = 0xFFC586C0u;
  p.code_cmt = 0xFF6A9955u;
  p.code_fn = 0xFFDCDCAAu;
  p.status_ok = 0xFF4EC9B0u;
  return p;
}

static void vs_draw_surface(const vs_palette_t *p, int32_t x, int32_t y,
                            int32_t w, int32_t h, uint32_t color,
                            uint8_t glass_alpha) {
  if (p->is_glass && g_api->gfx_fill_rounded_alpha) {
    g_api->gfx_fill_rounded_alpha(x, y, w, h, p->radius, color & 0x00FFFFFFu,
                                  glass_alpha);
    if (g_api->gfx_draw_rounded_rect)
      g_api->gfx_draw_rounded_rect(x, y, w, h, p->radius, p->border);
    if (g_api->gfx_draw_specular_rim)
      g_api->gfx_draw_specular_rim(x, y, w, h, p->radius);
  } else {
    g_api->gfx_fill_rect(x, y, w, h, color);
    g_api->gfx_draw_rect(x, y, w, h, p->border);
  }
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
static int g_ctrl_count = 0;
static int g_sel_idx = -1;
static int g_seq = 1;

/* 0 = Form1.xuiml [Design], 1 = Form1.xuiml [Code], 2 = studio_app.c [Code] */
static int g_doc_tab = 0;
static int g_scroll_x = 0;
static int g_scroll_y = 0;
static int g_code_total_rows = 0;
static int g_code_max_cols = 0;
static int g_code_errors = 0;
static int g_code_handlers = 0;
static int g_code_lines_cnt = 0;
static int g_code_bytes_cnt = 0;
static bool g_edit_prop_text = false;
static bool g_edit_code = false;
static uint64_t g_last_click_tick = 0;
static int g_last_click_idx = -1;

/* Gestione Progetti (.nsproj) */
static char g_proj_name[32] = "NewOSApp";
static char g_proj_dir[128] = "C:\\User\\Projects\\NewOSApp";
static int g_proj_seq = 1;
static int g_proj_tpl = 1; /* 0 = Vuoto, 1 = SDK App, 2 = SysInfo */
static bool g_proj_dirty = false;
static bool g_show_proj_mgr = false;
static bool g_edit_proj_name = false;
static char g_new_proj_buf[24] = "";
static char g_proj_list[8][32];
static int g_proj_list_cnt = 0;

/* Configurazione Progetto & File Output (Modalita' Progetto) */
typedef struct {
  char name[40];
  uint8_t
      usage; /* 0=UI Layout, 1=Sorgente C, 2=Progetto, 3=Config, 4=Risorsa */
  uint8_t copy_mode; /* 0=Non copiare, 1=Copia sempre, 2=Solo se nuovo */
} st_proj_file_t;

#define ST_MAX_PFILES 12
static st_proj_file_t g_pfiles[ST_MAX_PFILES];
static int g_pfile_cnt = 0;
static int g_open_combo_idx = -1;
static int g_proj_ver_minor = 0;
static int g_proj_build_num = 100;
static int g_proj_target_efi = 0; /* 0=x86_64, 1=x86_64+EFI */
static int g_proj_app_type = 0;   /* 0=Desktop GUI, 1=Console (Prompt) */
static int g_proj_icon_idx = 0;

static const int g_proj_icons[8] = {10, 43, 8, 44, 24, 37, 14, 20};
static const char *const g_proj_icon_names[8] = {
    "Desktop", "Console", "Esegui", "Impostaz.",
    "Play",    "Setup",   "Rete",   "Dati"};

static char g_c_code[2048];
static char g_output_lines[4][76];
static char g_status[80] =
    "Pronto - Doppio click su un controllo per aprire il Code Viewer";
static window_t *g_studio_win = NULL;

static const char *st_type_tag(st_ctrl_type_t t) {
  switch (t) {
  case ST_LABEL:
    return "Label";
  case ST_BUTTON:
    return "Button";
  case ST_TEXTBOX:
    return "TextBox";
  case ST_CHECKBOX:
    return "CheckBox";
  case ST_GROUPBOX:
    return "GroupBox";
  default:
    return "Control";
  }
}

static void st_push_output(const char *line) {
  for (int i = 0; i < 3; i++)
    dll_strcpy(g_output_lines[i], g_output_lines[i + 1], 76);
  dll_strcpy(g_output_lines[3], line ? line : "", 76);
}

static void st_regen_c_code(const char *focus_id) {
  if (!g_rt || !g_rt->snprintf)
    return;
  if (g_proj_app_type == 1) {
    g_rt->snprintf(
        g_c_code, sizeof(g_c_code),
        "// studio_app.c - App Console (Prompt dei Comandi)\n"
        "#include <newos/sdk.h>\n\n"
        "void app_main(const kernel_api_t* kapi) {\n"
        "    const nsrt_api_t* rt = nsrt_load_library(kapi);\n"
        "    if (!rt) return;\n"
        "    rt->serial_print(\"[CONSOLE] %s v1.%d.%d avviato...\\n\");\n"
        "    NS_LOG_INFO(\"App Console eseguita (Subsystem: CONSOLE)\");\n"
        "}\n",
        g_proj_name, g_proj_ver_minor, g_proj_build_num);
    return;
  }
  int pos = 0;
  pos += g_rt->snprintf(g_c_code + pos, sizeof(g_c_code) - (size_t)pos,
                        "// studio_app.c - Generato da NewOS Studio IDE\n"
                        "#include <newos/sdk.h>\n\n"
                        "static const nsrt_api_t* g_rt = NULL;\n\n"
                        "static void on_ui_event(nsrt_app_t* app, const char* "
                        "control_id, void* ctx) {\n"
                        "    NS_UNUSED(ctx);\n"
                        "    const nsrt_api_t* rt = g_rt;\n"
                        "    if (!rt) return;\n");

  for (int i = 0; i < g_ctrl_count && pos < (int)sizeof(g_c_code) - 220; i++) {
    if (g_ctrls[i].type == ST_BUTTON ||
        (focus_id && g_rt->strcmp(g_ctrls[i].id, focus_id) == 0)) {
      if (g_rt->strcmp(g_ctrls[i].id, "btn_exit") == 0) {
        pos += g_rt->snprintf(
            g_c_code + pos, sizeof(g_c_code) - (size_t)pos,
            "    NS_ON_CLICK(\"%s\") { rt->close_app(app); }\n", g_ctrls[i].id);
      } else {
        pos += g_rt->snprintf(
            g_c_code + pos, sizeof(g_c_code) - (size_t)pos,
            "    NS_ON_CLICK(\"%s\") {\n"
            "        rt->set_text(app, \"lbl_msg\", \"Azione su %s!\");\n"
            "    }\n",
            g_ctrls[i].id, g_ctrls[i].id);
      }
    }
  }

  pos += g_rt->snprintf(
      g_c_code + pos, sizeof(g_c_code) - (size_t)pos,
      "}\n\n"
      "void app_main(const kernel_api_t* kapi) {\n"
      "    g_rt = nsrt_load_library(kapi);\n"
      "    if (g_rt) g_rt->create_xuiml_app(\"Studio App\", 420, 280,\n"
      "        \"C:\\\\User\\\\studio_form.xuiml\", on_ui_event, NULL);\n"
      "}\n");
}

static int st_build_xuiml(char *out, int max_len);
static void st_exec_action(int act);

static void st_run_code_analysis(void) {
  const char *src = g_c_code;
  char *tmp_xml = NULL;
  if (g_doc_tab == 1 && g_rt && g_rt->alloc) {
    tmp_xml = (char *)g_rt->alloc(4096);
    if (tmp_xml) {
      st_build_xuiml(tmp_xml, 4096);
      src = tmp_xml;
    }
  }

  int lines = 1, bytes = 0, handlers = 0, errs = 0;
  int brace = 0, paren = 0, angle = 0;
  bool in_str = false, in_line_cmt = false;

  for (int i = 0; src[i]; i++) {
    char c = src[i];
    bytes++;
    if (c == '\n') {
      lines++;
      in_line_cmt = false;
      continue;
    }
    if (in_line_cmt)
      continue;
    if (c == '"' && (i == 0 || src[i - 1] != '\\')) {
      in_str = !in_str;
      continue;
    }
    if (in_str)
      continue;
    if (g_doc_tab == 2 && c == '/' && src[i + 1] == '/') {
      in_line_cmt = true;
      continue;
    }
    if (g_doc_tab == 1) {
      if (c == '<')
        angle++;
      else if (c == '>') {
        angle--;
        if (angle < 0) {
          errs++;
          angle = 0;
        }
      }
    } else {
      if (c == '{')
        brace++;
      else if (c == '}') {
        brace--;
        if (brace < 0) {
          errs++;
          brace = 0;
        }
      } else if (c == '(')
        paren++;
      else if (c == ')') {
        paren--;
        if (paren < 0) {
          errs++;
          paren = 0;
        }
      }
      /* Conta occorrenze di NS_ON_CLICK */
      if (c == 'N' && src[i + 1] == 'S' && src[i + 2] == '_' &&
          src[i + 3] == 'O' && src[i + 4] == 'N' && src[i + 5] == '_') {
        handlers++;
      }
    }
  }

  if (in_str)
    errs++;
  if (g_doc_tab == 1) {
    if (angle != 0)
      errs += angle;
  } else {
    if (brace != 0)
      errs += (brace > 0 ? brace : -brace);
    if (paren != 0)
      errs += (paren > 0 ? paren : -paren);
  }

  g_code_lines_cnt = lines;
  g_code_bytes_cnt = bytes;
  g_code_errors = errs;
  g_code_handlers = (g_doc_tab == 1) ? g_ctrl_count : handlers;

  if (tmp_xml && g_rt && g_rt->free)
    g_rt->free(tmp_xml);

  if (g_rt && g_rt->snprintf) {
    char msg[76];
    if (errs == 0) {
      g_rt->snprintf(
          msg, sizeof(msg), "[ANALISI] Sintassi OK (%d righe, %d B, %d %s)",
          lines, bytes, g_code_handlers, g_doc_tab == 1 ? "tag" : "handler");
    } else {
      g_rt->snprintf(msg, sizeof(msg),
                     "[ANALISI] Rilevati %d errori di bilanciamento/stringhe!",
                     errs);
    }
    st_push_output(msg);
    dll_strcpy(g_status, msg, sizeof(g_status));
  }
}

static void st_insert_c_snippet(int idx) {
  if (g_doc_tab == 1) {
    if (idx >= 0 && idx < 5) {
      st_exec_action(2 + idx);
      g_doc_tab = 1;
      g_edit_code = false;
      st_run_code_analysis();
    }
    return;
  }

  static const char *const snippets[5] = {
      "    NS_ON_CLICK(\"btn_1\") {\n"
      "        rt->set_text(app, \"lbl_msg\", \"Click!\");\n"
      "    }\n",
      "    rt->set_text(app, \"lbl_msg\", \"Aggiornato da SDK\");\n",
      "    rt->file_write(\"C:\\\\User\\\\out.txt\", \"OK\", 2, true);\n",
      "    char regv[64]; rt->reg_read_sz(\"OS\\\\Version\", regv, 64);\n",
      "    NS_LOG_INFO(\"Evento eseguito con successo\");\n"};
  if (idx < 0 || idx >= 5 || !g_rt || !g_rt->strlen)
    return;

  const char *snip = snippets[idx];
  size_t slen = g_rt->strlen(snip);
  size_t clen = g_rt->strlen(g_c_code);
  if (clen + slen + 2 >= sizeof(g_c_code)) {
    st_push_output("[WARN] Buffer studio_app.c pieno, impossibile inserire.");
    return;
  }

  /* Inserisce prima della chiusura di on_ui_event se presente */
  const char *marker = "}\n\nvoid app_main";
  int ins_pos = -1;
  for (int i = 0; g_c_code[i]; i++) {
    int m = 0;
    while (marker[m] && g_c_code[i + m] == marker[m])
      m++;
    if (!marker[m]) {
      ins_pos = i;
      break;
    }
  }

  if (ins_pos >= 0) {
    for (int j = (int)clen; j >= ins_pos; j--) {
      g_c_code[j + (int)slen] = g_c_code[j];
    }
    for (size_t k = 0; k < slen; k++) {
      g_c_code[ins_pos + (int)k] = snip[k];
    }
  } else {
    for (size_t k = 0; k < slen; k++) {
      g_c_code[clen + k] = snip[k];
    }
    g_c_code[clen + slen] = '\0';
  }

  g_edit_code = true;
  st_run_code_analysis();
}

static void st_init_template(void) {
  g_ctrl_count = 0;
  g_seq = 1;
  g_edit_prop_text = false;
  g_edit_code = false;
  g_doc_tab = 0;

  g_ctrls[0] = (st_ctrl_t){
      ST_GROUPBOX, "grp_main", " App NewOS Studio ", 14, 34, 390, 190};
  g_ctrls[1] = (st_ctrl_t){
      ST_LABEL, "lbl_msg", "Benvenuto nella tua App XUIML!", 28, 62, 300, 20};
  g_ctrls[2] = (st_ctrl_t){
      ST_CHECKBOX, "chk_opt", "Abilita Sandbox ACL", 28, 94, 240, 22};
  g_ctrls[3] =
      (st_ctrl_t){ST_BUTTON, "btn_ok", "Esegui Azione", 28, 174, 130, 28};
  g_ctrls[4] = (st_ctrl_t){ST_BUTTON, "btn_exit", "Chiudi", 264, 174, 120, 28};
  g_ctrl_count = 5;
  g_sel_idx = 3;
  st_regen_c_code(NULL);
  st_push_output("Soluzione 'NewOSApp' caricata (5 controlli, target x86_64).");
  st_push_output(
      "Suggerimento: doppio click su un controllo per aprire studio_app.c.");
  dll_strcpy(g_status,
             "Pronto - Doppio click su un controllo per aprire il Code Viewer",
             sizeof(g_status));
}

static void st_add_control(st_ctrl_type_t type) {
  if (g_ctrl_count >= ST_MAX_CTRLS) {
    st_push_output("[WARN] Limite massimo controlli raggiunto (20).");
    return;
  }
  st_ctrl_t *c = &g_ctrls[g_ctrl_count];
  c->type = type;
  if (g_rt && g_rt->snprintf) {
    g_rt->snprintf(c->id, sizeof(c->id), "%s_%d",
                   type == ST_BUTTON     ? "btn"
                   : type == ST_LABEL    ? "lbl"
                   : type == ST_TEXTBOX  ? "txt"
                   : type == ST_CHECKBOX ? "chk"
                                         : "grp",
                   g_seq++);
  }
  if (type == ST_LABEL) {
    dll_strcpy(c->text, "Nuova Etichetta", 48);
    c->w = 180;
    c->h = 20;
  }
  if (type == ST_BUTTON) {
    dll_strcpy(c->text, "Pulsante", 48);
    c->w = 110;
    c->h = 28;
  }
  if (type == ST_TEXTBOX) {
    dll_strcpy(c->text, "Input...", 48);
    c->w = 180;
    c->h = 24;
  }
  if (type == ST_CHECKBOX) {
    dll_strcpy(c->text, "Opzione", 48);
    c->w = 160;
    c->h = 22;
  }
  if (type == ST_GROUPBOX) {
    dll_strcpy(c->text, " Gruppo ", 48);
    c->w = 240;
    c->h = 100;
  }

  c->x = 28 + ((g_ctrl_count % 3) * 90);
  c->y = 128;
  g_sel_idx = g_ctrl_count++;
  g_doc_tab = 0;
  g_proj_dirty = true;
  st_regen_c_code(NULL);
  st_push_output("Aggiunto nuovo controllo al form.");
  dll_strcpy(g_status,
             "Controllo aggiunto - Doppio click per generare handler C",
             sizeof(g_status));
}

static void st_proj_path(char *dst, int cap, const char *filename) {
  if (g_rt && g_rt->snprintf) {
    g_rt->snprintf(dst, (size_t)cap, "%s\\%s", g_proj_dir, filename);
  } else if (dst && cap > 0) {
    dst[0] = '\0';
  }
}

static void st_ensure_projects_root(void) {
  if (!g_rt || !g_rt->mkdir)
    return;
  if (!g_rt->file_exists || !g_rt->file_exists("C:\\User"))
    g_rt->mkdir("C:\\User", true);
  if (!g_rt->file_exists || !g_rt->file_exists("C:\\User\\Projects"))
    g_rt->mkdir("C:\\User\\Projects", true);
  if (!g_rt->file_exists || !g_rt->file_exists(g_proj_dir))
    g_rt->mkdir(g_proj_dir, true);
}

static void st_refresh_project_list(void) {
  g_proj_list_cnt = 0;
  if (!g_rt || !g_rt->readdir || !g_rt->alloc || !g_rt->free)
    return;
  ns_dirent_t *ents = (ns_dirent_t *)g_rt->alloc(sizeof(ns_dirent_t) * 16);
  if (!ents)
    return;
  int32_t cnt = g_rt->readdir("C:\\User\\Projects", ents, 16);
  for (int32_t i = 0; i < cnt && g_proj_list_cnt < 8; i++) {
    if (!ents[i].is_directory || ents[i].name[0] == '.')
      continue;
    bool dup = false;
    for (int j = 0; j < g_proj_list_cnt; j++) {
      if (g_rt->strcmp && g_rt->strcmp(g_proj_list[j], ents[i].name) == 0) {
        dup = true;
        break;
      }
    }
    if (!dup)
      dll_strcpy(g_proj_list[g_proj_list_cnt++], ents[i].name, 32);
  }
  g_rt->free(ents);
}

static int st_parse_int_tok(const char **pp, char sep) {
  const char *s = *pp;
  int sign = 1, val = 0;
  if (*s == '-') {
    sign = -1;
    s++;
  }
  while (*s >= '0' && *s <= '9') {
    val = val * 10 + (*s - '0');
    s++;
  }
  if (*s == sep)
    s++;
  *pp = s;
  return val * sign;
}

static void st_read_str_tok(const char **pp, char sep, char *out, int cap) {
  const char *s = *pp;
  int i = 0;
  while (*s && *s != sep && *s != '\n' && *s != '\r') {
    if (i < cap - 1)
      out[i++] = *s;
    s++;
  }
  out[i] = '\0';
  if (*s == sep)
    s++;
  *pp = s;
}

static int st_build_xuiml(char *out, int max_len) {
  if (!g_rt || !g_rt->snprintf || !out || max_len < 256)
    return 0;
  int pos = 0;
  pos += g_rt->snprintf(
      out + pos, (size_t)(max_len - pos),
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<Form id=\"studio_app\" title=\"NewOS Studio Live\" width=\"420\" "
      "height=\"280\">\n"
      "  <Tab id=\"tab_main\" title=\"Principale\" x=\"8\" w=\"120\">\n");

  for (int i = 0; i < g_ctrl_count && pos < max_len - 160; i++) {
    const st_ctrl_t *c = &g_ctrls[i];
    if (c->type == ST_GROUPBOX) {
      pos += g_rt->snprintf(
          out + pos, (size_t)(max_len - pos),
          "    <GroupBox title=\"%s\" x=\"%d\" y=\"%d\" w=\"%d\" h=\"%d\" />\n",
          c->text, (int)c->x, (int)c->y, (int)c->w, (int)c->h);
    } else if (c->type == ST_LABEL) {
      pos += g_rt->snprintf(
          out + pos, (size_t)(max_len - pos),
          "    <Label id=\"%s\" text=\"%s\" x=\"%d\" y=\"%d\" />\n", c->id,
          c->text, (int)c->x, (int)c->y);
    } else {
      pos += g_rt->snprintf(out + pos, (size_t)(max_len - pos),
                            "    <%s id=\"%s\" text=\"%s\" x=\"%d\" y=\"%d\" "
                            "w=\"%d\" h=\"%d\" />\n",
                            st_type_tag(c->type), c->id, c->text, (int)c->x,
                            (int)c->y, (int)c->w, (int)c->h);
    }
  }
  pos +=
      g_rt->snprintf(out + pos, (size_t)(max_len - pos), "  </Tab>\n</Form>\n");
  return pos;
}

static bool st_export_xuiml(void) {
  if (!g_rt)
    return false;
  st_ensure_projects_root();
  char *buf = (char *)g_rt->alloc(4096);
  if (!buf)
    return false;
  char xpath[160];
  st_proj_path(xpath, sizeof(xpath), "studio_form.xuiml");
  int len = st_build_xuiml(buf, 4096);
  bool ok = g_rt->file_write(xpath, buf, (uint32_t)len, true);
  g_rt->free(buf);
  if (g_rt->snprintf) {
    char msg[76];
    g_rt->snprintf(msg, sizeof(msg),
                   ok ? "[BUILD] Esportato %s\\studio_form.xuiml"
                      : "[ERR] Fallito export .xuiml",
                   g_proj_name);
    st_push_output(msg);
    dll_strcpy(g_status, ok ? xpath : "Errore export!", sizeof(g_status));
  }
  return ok;
}

static void st_export_c_app(void) {
  if (!g_rt)
    return;
  st_ensure_projects_root();
  char cpath[160];
  st_proj_path(cpath, sizeof(cpath), "studio_app.c");
  size_t len = g_rt->strlen ? g_rt->strlen(g_c_code) : 0;
  bool ok = g_rt->file_write(cpath, g_c_code, (uint32_t)len, true);
  if (g_rt->snprintf) {
    char msg[76];
    g_rt->snprintf(msg, sizeof(msg),
                   ok ? "[BUILD] Salvato %s\\studio_app.c (SDK v3.0)"
                      : "[ERR] Fallito export studio_app.c",
                   g_proj_name);
    st_push_output(msg);
    dll_strcpy(g_status, ok ? cpath : "Errore salvataggio C!",
               sizeof(g_status));
  }
}

static void st_refresh_project_files(void);

static bool st_save_project(void) {
  if (!g_rt || !g_rt->snprintf || !g_rt->alloc || !g_rt->free)
    return false;
  st_ensure_projects_root();
  bool ok_x = st_export_xuiml();
  st_export_c_app();

  char *pbuf = (char *)g_rt->alloc(2048);
  if (!pbuf)
    return false;
  int pos = 0;
  pos += g_rt->snprintf(
      pbuf + pos, 2048 - (size_t)pos,
      "[NewOSProject]\nname=%s\nversion=1.%d.%d\nauthor=NewOS Developer\n"
      "target=%s\nsubsystem=%s\nicon_id=%d\ntemplate=%d\nver_minor=%d\n"
      "build_num=%d\ntarget_efi=%d\napp_type=%d\nicon_idx=%d\nseq=%d\n"
      "ctrl_count=%d\n",
      g_proj_name, g_proj_ver_minor, g_proj_build_num,
      g_proj_target_efi ? "x86_64+EFI" : "x86_64",
      g_proj_app_type ? "CONSOLE" : "WINDOWS_GUI",
      g_proj_icons[g_proj_icon_idx & 7], g_proj_tpl, g_proj_ver_minor,
      g_proj_build_num, g_proj_target_efi, g_proj_app_type, g_proj_icon_idx,
      g_seq, g_ctrl_count);
  for (int i = 0; i < g_pfile_cnt && pos < 1750; i++) {
    pos += g_rt->snprintf(pbuf + pos, 2048 - (size_t)pos, "copy_%s=%d\n",
                          g_pfiles[i].name, (int)g_pfiles[i].copy_mode);
  }
  for (int i = 0; i < g_ctrl_count && pos < 1900; i++) {
    const st_ctrl_t *c = &g_ctrls[i];
    pos +=
        g_rt->snprintf(pbuf + pos, 2048 - (size_t)pos,
                       "ctrl_%d=%d|%s|%s|%d|%d|%d|%d\n", i, (int)c->type, c->id,
                       c->text, (int)c->x, (int)c->y, (int)c->w, (int)c->h);
  }
  char pfname[48], ppath[176];
  g_rt->snprintf(pfname, sizeof(pfname), "%s.nsproj", g_proj_name);
  st_proj_path(ppath, sizeof(ppath), pfname);
  bool ok_p = g_rt->file_write(ppath, pbuf, (uint32_t)pos, true);
  g_rt->free(pbuf);

  if (ok_p && ok_x) {
    g_proj_dirty = false;
    if (g_rt->reg_write_sz)
      g_rt->reg_write_sz("Software\\NewOSStudio", "LastProject", g_proj_name,
                         true);
    st_refresh_project_list();
    char msg[76];
    g_rt->snprintf(msg, sizeof(msg),
                   "[PROJ] Progetto '%s' salvato in C:\\User\\Projects",
                   g_proj_name);
    st_push_output(msg);
    dll_strcpy(g_status, msg, sizeof(g_status));
    if (g_rt->notify)
      g_rt->notify("NewOS Studio", msg);
  }
  return ok_p;
}

static bool st_load_project_file(const char *nsproj_path) {
  if (!g_rt || !g_rt->file_read || !nsproj_path || !nsproj_path[0])
    return false;
  uint32_t sz = 0;
  char *data = (char *)g_rt->file_read(nsproj_path, &sz);
  if (!data || sz == 0)
    return false;

  /* Ricava g_proj_dir tagliando l'ultimo '\' */
  int last_slash = -1;
  for (int i = 0; nsproj_path[i]; i++) {
    if (nsproj_path[i] == '\\' || nsproj_path[i] == '/')
      last_slash = i;
  }
  if (last_slash > 0 && last_slash < (int)sizeof(g_proj_dir)) {
    for (int i = 0; i < last_slash; i++)
      g_proj_dir[i] = nsproj_path[i];
    g_proj_dir[last_slash] = '\0';
  }

  g_ctrl_count = 0;
  g_seq = 1;
  const char *p = data;
  const char *end = data + sz;
  while (p < end && *p) {
    if (g_rt->strncmp && g_rt->strncmp(p, "name=", 5) == 0) {
      p += 5;
      st_read_str_tok(&p, '\n', g_proj_name, sizeof(g_proj_name));
    } else if (g_rt->strncmp && g_rt->strncmp(p, "template=", 9) == 0) {
      p += 9;
      g_proj_tpl = st_parse_int_tok(&p, '\n');
    } else if (g_rt->strncmp && g_rt->strncmp(p, "ver_minor=", 10) == 0) {
      p += 10;
      g_proj_ver_minor = st_parse_int_tok(&p, '\n');
    } else if (g_rt->strncmp && g_rt->strncmp(p, "build_num=", 10) == 0) {
      p += 10;
      g_proj_build_num = st_parse_int_tok(&p, '\n');
    } else if (g_rt->strncmp && g_rt->strncmp(p, "target_efi=", 11) == 0) {
      p += 11;
      g_proj_target_efi = st_parse_int_tok(&p, '\n');
    } else if (g_rt->strncmp && g_rt->strncmp(p, "app_type=", 9) == 0) {
      p += 9;
      g_proj_app_type = st_parse_int_tok(&p, '\n') ? 1 : 0;
    } else if (g_rt->strncmp && g_rt->strncmp(p, "icon_idx=", 9) == 0) {
      p += 9;
      g_proj_icon_idx = st_parse_int_tok(&p, '\n') & 7;
    } else if (g_rt->strncmp && g_rt->strncmp(p, "seq=", 4) == 0) {
      p += 4;
      g_seq = st_parse_int_tok(&p, '\n');
    } else if (g_rt->strncmp && g_rt->strncmp(p, "copy_", 5) == 0) {
      p += 5;
      char fname[40];
      st_read_str_tok(&p, '=', fname, sizeof(fname));
      int cm = st_parse_int_tok(&p, '\n');
      st_refresh_project_files();
      for (int fi = 0; fi < g_pfile_cnt; fi++) {
        if (g_rt->strcmp && g_rt->strcmp(g_pfiles[fi].name, fname) == 0) {
          g_pfiles[fi].copy_mode = (uint8_t)(cm >= 0 && cm <= 2 ? cm : 0);
          break;
        }
      }
    } else if (g_rt->strncmp && g_rt->strncmp(p, "ctrl_", 5) == 0 &&
               g_rt->strncmp(p, "ctrl_count=", 11) != 0) {
      while (p < end && *p && *p != '=')
        p++;
      if (*p == '=')
        p++;
      if (g_ctrl_count < ST_MAX_CTRLS) {
        st_ctrl_t *c = &g_ctrls[g_ctrl_count++];
        c->type = (st_ctrl_type_t)st_parse_int_tok(&p, '|');
        st_read_str_tok(&p, '|', c->id, sizeof(c->id));
        st_read_str_tok(&p, '|', c->text, sizeof(c->text));
        c->x = st_parse_int_tok(&p, '|');
        c->y = st_parse_int_tok(&p, '|');
        c->w = st_parse_int_tok(&p, '|');
        c->h = st_parse_int_tok(&p, '\n');
      }
    }
    while (p < end && *p && *p != '\n')
      p++;
    if (p < end && *p == '\n')
      p++;
  }
  g_rt->free(data);

  if (g_ctrl_count == 0)
    st_init_template();
  g_sel_idx = (g_ctrl_count > 0) ? 0 : -1;

  char cpath[160];
  st_proj_path(cpath, sizeof(cpath), "studio_app.c");
  uint32_t csz = 0;
  char *cdata = g_rt->file_read(cpath, &csz);
  if (cdata && csz > 0) {
    size_t copy_n = csz < sizeof(g_c_code) - 1 ? csz : sizeof(g_c_code) - 1;
    g_rt->memcpy(g_c_code, cdata, copy_n);
    g_c_code[copy_n] = '\0';
    g_rt->free(cdata);
  } else {
    st_regen_c_code(NULL);
  }

  g_proj_dirty = false;
  g_show_proj_mgr = false;
  g_edit_proj_name = false;
  g_doc_tab = 0;
  g_edit_code = false;
  g_edit_prop_text = false;
  if (g_rt->reg_write_sz)
    g_rt->reg_write_sz("Software\\NewOSStudio", "LastProject", g_proj_name,
                       true);
  st_run_code_analysis();
  if (g_rt->snprintf) {
    char msg[76];
    g_rt->snprintf(msg, sizeof(msg),
                   "[PROJ] Aperto progetto '%s' (%d controlli)", g_proj_name,
                   g_ctrl_count);
    st_push_output(msg);
    dll_strcpy(g_status, msg, sizeof(g_status));
  }
  return true;
}

static bool st_load_project_by_name(const char *name) {
  if (!g_rt || !g_rt->snprintf || !name || !name[0])
    return false;
  char path[176];
  g_rt->snprintf(path, sizeof(path), "C:\\User\\Projects\\%s\\%s.nsproj", name,
                 name);
  return st_load_project_file(path);
}

static void st_apply_project_template(int tpl) {
  g_ctrl_count = 0;
  g_seq = 1;
  g_edit_prop_text = false;
  g_edit_code = false;
  g_doc_tab = 0;
  if (tpl == 0) {
    g_ctrls[0] =
        (st_ctrl_t){ST_GROUPBOX, "grp_main", " App Vuota ", 14, 34, 390, 190};
    g_ctrls[1] = (st_ctrl_t){
        ST_LABEL, "lbl_msg", "Progetto vuoto pronto.", 28, 62, 260, 20};
    g_ctrl_count = 2;
    g_sel_idx = 1;
    st_regen_c_code(NULL);
  } else if (tpl == 2 && g_rt && g_rt->snprintf) {
    g_ctrls[0] = (st_ctrl_t){
        ST_GROUPBOX, "grp_sys", " Diagnostica Sistema ", 14, 34, 390, 190};
    g_ctrls[1] = (st_ctrl_t){
        ST_LABEL, "lbl_os", "OS: NewOS V1 (x86_64)", 28, 62, 320, 20};
    g_ctrls[2] = (st_ctrl_t){
        ST_LABEL, "lbl_ram", "RAM: pronto al rilevamento", 28, 92, 320, 20};
    g_ctrls[3] =
        (st_ctrl_t){ST_BUTTON, "btn_ref", "Aggiorna Info", 28, 174, 130, 28};
    g_ctrls[4] =
        (st_ctrl_t){ST_BUTTON, "btn_exit", "Chiudi", 264, 174, 120, 28};
    g_ctrl_count = 5;
    g_sel_idx = 3;
    g_rt->snprintf(
        g_c_code, sizeof(g_c_code),
        "// studio_app.c - Template SysInfo (SDK v3.0)\n"
        "#include <newos/sdk.h>\n\n"
        "static const nsrt_api_t* g_rt = NULL;\n\n"
        "static void on_ui_event(nsrt_app_t* app, const char* control_id, "
        "void* ctx) {\n"
        "    NS_UNUSED(ctx);\n"
        "    const nsrt_api_t* rt = g_rt;\n"
        "    if (!rt) return;\n"
        "    NS_ON_CLICK(\"btn_ref\") {\n"
        "        ns_sys_info_t info; char buf[64];\n"
        "        if (rt->get_sys_info(&info)) {\n"
        "            rt->snprintf(buf, 64, \"RAM Libera: %u KB\", "
        "info.free_ram_kb);\n"
        "            rt->set_text(app, \"lbl_ram\", buf);\n"
        "        }\n"
        "    }\n"
        "    NS_ON_CLICK(\"btn_exit\") { rt->close_app(app); }\n"
        "}\n\n"
        "void app_main(const kernel_api_t* kapi) {\n"
        "    g_rt = nsrt_load_library(kapi);\n"
        "    if (g_rt) g_rt->create_xuiml_app(\"SysInfo App\", 420, 280,\n"
        "        \"C:\\\\User\\\\studio_form.xuiml\", on_ui_event, NULL);\n"
        "}\n");
  } else {
    st_init_template();
  }
}

static void st_new_project(void) {
  if (!g_rt || !g_rt->snprintf)
    return;
  if (g_new_proj_buf[0]) {
    dll_strcpy(g_proj_name, g_new_proj_buf, sizeof(g_proj_name));
    g_new_proj_buf[0] = '\0';
  } else {
    g_proj_seq++;
    g_rt->snprintf(g_proj_name, sizeof(g_proj_name), "App_%d", g_proj_seq);
  }
  g_rt->snprintf(g_proj_dir, sizeof(g_proj_dir), "C:\\User\\Projects\\%s",
                 g_proj_name);
  st_apply_project_template(g_proj_tpl);
  g_show_proj_mgr = false;
  g_edit_proj_name = false;
  st_save_project();
}

static void st_duplicate_project(const char *src_name) {
  if (!g_rt || !g_rt->snprintf || !src_name || !src_name[0])
    return;
  char dst_name[32];
  g_rt->snprintf(dst_name, sizeof(dst_name), "%.18s_cp", src_name);
  char dst_dir[128];
  g_rt->snprintf(dst_dir, sizeof(dst_dir), "C:\\User\\Projects\\%s", dst_name);
  if (!g_rt->file_exists || !g_rt->file_exists(dst_dir))
    g_rt->mkdir(dst_dir, true);

  const char *files[2] = {"studio_form.xuiml", "studio_app.c"};
  for (int i = 0; i < 2; i++) {
    char sp[160], dp[160];
    g_rt->snprintf(sp, sizeof(sp), "C:\\User\\Projects\\%s\\%s", src_name,
                   files[i]);
    g_rt->snprintf(dp, sizeof(dp), "%s\\%s", dst_dir, files[i]);
    uint32_t sz = 0;
    void *d = g_rt->file_read ? g_rt->file_read(sp, &sz) : NULL;
    if (d && sz > 0) {
      g_rt->file_write(dp, d, sz, true);
      g_rt->free(d);
    }
  }
  char p_src[176], p_dst[176];
  g_rt->snprintf(p_src, sizeof(p_src), "C:\\User\\Projects\\%s\\%s.nsproj",
                 src_name, src_name);
  g_rt->snprintf(p_dst, sizeof(p_dst), "%s\\%s.nsproj", dst_dir, dst_name);
  uint32_t psz = 0;
  void *pd = g_rt->file_read ? g_rt->file_read(p_src, &psz) : NULL;
  if (pd && psz > 0) {
    g_rt->file_write(p_dst, pd, psz, true);
    g_rt->free(pd);
  }
  st_refresh_project_list();
  char msg[76];
  g_rt->snprintf(msg, sizeof(msg), "[PROJ] Duplicato '%s' -> '%s'", src_name,
                 dst_name);
  st_push_output(msg);
  dll_strcpy(g_status, msg, sizeof(g_status));
}

static void st_delete_project(const char *name) {
  if (!g_rt || !g_rt->file_delete || !name || !name[0])
    return;
  if (g_rt->strcmp && g_rt->strcmp(name, g_proj_name) == 0) {
    st_push_output("[WARN] Impossibile eliminare il progetto attivo.");
    dll_strcpy(g_status, "Impossibile eliminare il progetto attivo!",
               sizeof(g_status));
    return;
  }
  char p1[176], p2[160], p3[160];
  g_rt->snprintf(p1, sizeof(p1), "C:\\User\\Projects\\%s\\%s.nsproj", name,
                 name);
  g_rt->snprintf(p2, sizeof(p2), "C:\\User\\Projects\\%s\\studio_form.xuiml",
                 name);
  g_rt->snprintf(p3, sizeof(p3), "C:\\User\\Projects\\%s\\studio_app.c", name);
  g_rt->file_delete(p1, true);
  g_rt->file_delete(p2, true);
  g_rt->file_delete(p3, true);
  st_refresh_project_list();
  char msg[76];
  g_rt->snprintf(msg, sizeof(msg), "[PROJ] Rimossi file del progetto '%s'",
                 name);
  st_push_output(msg);
  dll_strcpy(g_status, msg, sizeof(g_status));
}

static uint8_t st_classify_usage(const char *fname) {
  if (!fname)
    return 4;
  int len = 0;
  while (fname[len])
    len++;
  if (len >= 6 && g_rt && g_rt->strcmp &&
      g_rt->strcmp(fname + len - 6, ".xuiml") == 0)
    return 0; /* UI Layout */
  if (len >= 2 && (fname[len - 2] == '.' &&
                   (fname[len - 1] == 'c' || fname[len - 1] == 'h')))
    return 1; /* Sorgente C */
  if (len >= 7 && g_rt && g_rt->strcmp &&
      g_rt->strcmp(fname + len - 7, ".nsproj") == 0)
    return 2; /* Progetto */
  if (len >= 4 && g_rt && g_rt->strcmp &&
      g_rt->strcmp(fname + len - 4, ".ini") == 0)
    return 3; /* Config */
  return 4;   /* Risorsa */
}

static uint8_t st_default_copy_for_usage(uint8_t u) {
  return (u == 0 || u == 3 || u == 4) ? 1 : 0;
}

static void st_add_proj_file_entry(const char *name, uint8_t old_cnt,
                                   const st_proj_file_t *old_list) {
  if (!name || !name[0] || g_pfile_cnt >= ST_MAX_PFILES)
    return;
  for (int i = 0; i < g_pfile_cnt; i++) {
    if (g_rt && g_rt->strcmp && g_rt->strcmp(g_pfiles[i].name, name) == 0)
      return;
  }
  st_proj_file_t *f = &g_pfiles[g_pfile_cnt++];
  dll_strcpy(f->name, name, sizeof(f->name));
  f->usage = st_classify_usage(name);
  f->copy_mode = st_default_copy_for_usage(f->usage);
  for (uint8_t j = 0; j < old_cnt; j++) {
    if (g_rt && g_rt->strcmp && g_rt->strcmp(old_list[j].name, name) == 0) {
      f->copy_mode = old_list[j].copy_mode;
      break;
    }
  }
}

static void st_refresh_project_files(void) {
  st_proj_file_t prev[ST_MAX_PFILES];
  uint8_t prev_cnt = (uint8_t)g_pfile_cnt;
  for (uint8_t i = 0; i < prev_cnt; i++)
    prev[i] = g_pfiles[i];
  g_pfile_cnt = 0;

  if (g_rt && g_rt->readdir && g_rt->alloc && g_rt->free) {
    ns_dirent_t *ents = (ns_dirent_t *)g_rt->alloc(sizeof(ns_dirent_t) * 16);
    if (ents) {
      int32_t cnt = g_rt->readdir(g_proj_dir, ents, 16);
      for (int32_t i = 0; i < cnt; i++) {
        if (ents[i].is_directory || ents[i].name[0] == '.')
          continue;
        st_add_proj_file_entry(ents[i].name, prev_cnt, prev);
      }
      g_rt->free(ents);
    }
  }
  char pfname[40];
  if (g_rt && g_rt->snprintf)
    g_rt->snprintf(pfname, sizeof(pfname), "%s.nsproj", g_proj_name);
  else
    dll_strcpy(pfname, "NewOSApp.nsproj", sizeof(pfname));
  st_add_proj_file_entry("studio_form.xuiml", prev_cnt, prev);
  st_add_proj_file_entry("studio_app.c", prev_cnt, prev);
  st_add_proj_file_entry(pfname, prev_cnt, prev);
  st_add_proj_file_entry("app.ini", prev_cnt, prev);
}

static void st_apply_copy_preset(int preset) {
  st_refresh_project_files();
  for (int i = 0; i < g_pfile_cnt; i++) {
    uint8_t u = g_pfiles[i].usage;
    if (preset == 0) {
      g_pfiles[i].copy_mode = (u == 0 || u == 3 || u == 4) ? 1 : 0;
    } else if (preset == 1) {
      g_pfiles[i].copy_mode = (u == 3) ? 2 : 1;
    } else {
      g_pfiles[i].copy_mode = 1;
    }
  }
  g_proj_dirty = true;
  const char *pnames[3] = {"Solo Runtime", "Sorgenti + SDK", "Copia Tutto"};
  if (g_rt && g_rt->snprintf) {
    char msg[76];
    g_rt->snprintf(msg, sizeof(msg), "[PROJ] Preset copia output: %s",
                   pnames[preset >= 0 && preset < 3 ? preset : 0]);
    st_push_output(msg);
    dll_strcpy(g_status, msg, sizeof(g_status));
  }
}

static void st_deploy_project(void) {
  if (!g_rt || !g_rt->snprintf || !g_rt->alloc || !g_rt->free)
    return;
  st_save_project();
  st_refresh_project_files();
  char app_dir[128];
  g_rt->snprintf(app_dir, sizeof(app_dir), "C:\\ProgramFile\\%s", g_proj_name);
  if (!g_rt->file_exists || !g_rt->file_exists("C:\\ProgramFile"))
    g_rt->mkdir("C:\\ProgramFile", true);
  if (!g_rt->file_exists || !g_rt->file_exists(app_dir))
    g_rt->mkdir(app_dir, true);

  int copied = 0, skipped = 0;
  for (int i = 0; i < g_pfile_cnt; i++) {
    const st_proj_file_t *pf = &g_pfiles[i];
    if (pf->copy_mode == 0) {
      skipped++;
      continue;
    }
    char dp[168];
    g_rt->snprintf(dp, sizeof(dp), "%s\\%s", app_dir, pf->name);
    if (pf->copy_mode == 2 && g_rt->file_exists && g_rt->file_exists(dp)) {
      skipped++;
      continue;
    }
    if (g_rt->strcmp && g_rt->strcmp(pf->name, "app.ini") == 0) {
      char ini[256];
      int ilen = g_rt->snprintf(
          ini, sizeof(ini),
          "[AppManifest]\nName=%s\nVersion=1.%d.%d\nSubsystem=%s\nIconId=%d\n"
          "Runtime=NSRT.DLL+XUIML.DLL\nTarget=%s\n",
          g_proj_name, g_proj_ver_minor, g_proj_build_num,
          g_proj_app_type ? "CONSOLE" : "WINDOWS_GUI",
          g_proj_icons[g_proj_icon_idx & 7],
          g_proj_target_efi ? "x86_64+EFI" : "x86_64");
      if (g_rt->file_write(dp, ini, (uint32_t)ilen, true))
        copied++;
    } else {
      char sp[168];
      st_proj_path(sp, sizeof(sp), pf->name);
      uint32_t sz = 0;
      void *d = g_rt->file_read ? g_rt->file_read(sp, &sz) : NULL;
      if (d && sz > 0) {
        if (g_rt->file_write(dp, d, sz, true))
          copied++;
        g_rt->free(d);
      }
    }
  }
  char msg[76];
  g_rt->snprintf(msg, sizeof(msg),
                 "[DEPLOY] %s: %d copiati, %d esclusi -> C:\\ProgramFile",
                 g_proj_name, copied, skipped);
  st_push_output(msg);
  dll_strcpy(g_status, msg, sizeof(g_status));
  if (copied > 0 && g_rt->notify)
    g_rt->notify("NewOS Studio - Publish", msg);
}

static void st_create_installer(void) {
  if (!g_rt || !g_rt->snprintf || !g_rt->alloc || !g_rt->free)
    return;
  st_save_project();
  st_refresh_project_files();

  /* Regola obbligatoria: verifica collegamento a PKGSVC.DLL */
  const char *pkg_svc_path = "C:\\NewOS\\SystemLibrary\\PKGSVC.DLL";
  if (g_rt->file_exists && !g_rt->file_exists(pkg_svc_path)) {
    st_push_output("[ERR] Servizio obbligatorio PKGSVC.DLL non trovato!");
    dll_strcpy(g_status, "Errore: PKGSVC.DLL assente!", sizeof(g_status));
    return;
  }

  char inst_dir[160];
  st_proj_path(inst_dir, sizeof(inst_dir), "Installer");
  if (!g_rt->file_exists || !g_rt->file_exists(inst_dir))
    g_rt->mkdir(inst_dir, true);

  char *buf = (char *)g_rt->alloc(4096);
  if (!buf)
    return;

  /* 1. Manifest Installer (.nsi) con linker obbligatorio a PKGSVC.DLL */
  int pos = 0;
  int inc_cnt = 0;
  pos += g_rt->snprintf(
      buf + pos, 4096 - (size_t)pos,
      "[NewOSInstaller]\n"
      "PackageName=%s\n"
      "Version=1.%d.%d\n"
      "Subsystem=%s\n"
      "IconId=%d\n"
      "Target=%s\n"
      "RequiredService=C:\\NewOS\\SystemLibrary\\PKGSVC.DLL\n"
      "ServiceModule=PKGSVC\n"
      "ServiceEntry=pkgsvc_dll_entry\n"
      "InstallDir=C:\\ProgramFile\\%s\\\n"
      "SandboxACL=PROGRAMFILE_ONLY\n",
      g_proj_name, g_proj_ver_minor, g_proj_build_num,
      g_proj_app_type ? "CONSOLE" : "WINDOWS_GUI",
      g_proj_icons[g_proj_icon_idx & 7],
      g_proj_target_efi ? "x86_64+EFI" : "x86_64", g_proj_name);
  for (int i = 0; i < g_pfile_cnt && pos < 3600; i++) {
    if (g_pfiles[i].copy_mode == 0)
      continue;
    pos += g_rt->snprintf(buf + pos, 4096 - (size_t)pos, "File_%d=%s|%d\n",
                          inc_cnt++, g_pfiles[i].name,
                          (int)g_pfiles[i].copy_mode);
  }
  pos += g_rt->snprintf(buf + pos, 4096 - (size_t)pos, "FileCount=%d\n",
                        inc_cnt);
  char p_nsi[180];
  g_rt->snprintf(p_nsi, sizeof(p_nsi), "%s\\%s_setup.nsi", inst_dir,
                 g_proj_name);
  bool ok_nsi = g_rt->file_write(p_nsi, buf, (uint32_t)pos, true);

  /* 2. Wizard UI Installer (.xuiml) */
  int xlen = g_rt->snprintf(
      buf, 4096,
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<Form id=\"pkg_setup\" title=\"Installer - %s v1.%d\" width=\"420\" "
      "height=\"240\">\n"
      "  <GroupBox title=\" Installazione Guidata (PKGSVC.DLL) \" x=\"12\" "
      "y=\"26\" w=\"396\" h=\"162\" />\n"
      "  <Label id=\"lbl_pkg\" text=\"Pacchetto: %s (1.%d) - %d file\" "
      "x=\"24\" y=\"54\" />\n"
      "  <Label id=\"lbl_dst\" text=\"Destinazione: C:\\ProgramFile\\%s\\\" "
      "x=\"24\" y=\"82\" />\n"
      "  <Label id=\"lbl_svc\" text=\"Servizio: PKGSVC.DLL (Obbligatorio)\" "
      "x=\"24\" y=\"110\" />\n"
      "  <Button id=\"btn_install\" text=\"Installa\" x=\"24\" y=\"148\" "
      "w=\"130\" h=\"28\" />\n"
      "  <Button id=\"btn_cancel\" text=\"Annulla\" x=\"266\" y=\"148\" "
      "w=\"120\" h=\"28\" />\n"
      "</Form>\n",
      g_proj_name, g_proj_ver_minor, g_proj_name, g_proj_ver_minor, inc_cnt,
      g_proj_name);
  char p_ui[180];
  g_rt->snprintf(p_ui, sizeof(p_ui), "%s\\%s_installer.xuiml", inst_dir,
                 g_proj_name);
  bool ok_ui = g_rt->file_write(p_ui, buf, (uint32_t)xlen, true);

  /* 3. Sorgente Installer C linkato a newos/pkg.h + PKGSVC.DLL */
  int clen = g_rt->snprintf(
      buf, 4096,
      "// %s_installer.c - Generato da NewOS Studio (Linker: PKGSVC.DLL)\n"
      "#include <newos/sdk.h>\n"
      "#include <newos/pkg.h>\n\n"
      "static const ns_pkg_manifest_t g_manifest = {\n"
      "    \"%s\", \"%s v1.%d\", \"NewOS Developer\",\n"
      "    {1, %d, 0}, NS_PKG_PERM_FS_USER, \"C:\\\\ProgramFile\\\\%s\\\\\"\n"
      "};\n\n"
      "void app_main(const kernel_api_t* kapi) {\n"
      "    const nsrt_api_t* rt = nsrt_load_library(kapi);\n"
      "    if (!rt || !rt->file_exists(\"C:\\\\NewOS\\\\SystemLibrary\\\\PKGSVC.DLL\")) return;\n"
      "    rt->mkdir(\"C:\\\\ProgramFile\", true);\n"
      "    rt->mkdir(g_manifest.install_dir, true);\n"
      "    rt->notify(\"Installer PKGSVC\", \"Installazione completata in C:\\\\ProgramFile\");\n"
      "}\n",
      g_proj_name, g_proj_name, g_proj_name, g_proj_ver_minor,
      g_proj_ver_minor, g_proj_name);
  char p_c[180];
  g_rt->snprintf(p_c, sizeof(p_c), "%s\\%s_installer.c", inst_dir, g_proj_name);
  bool ok_c = g_rt->file_write(p_c, buf, (uint32_t)clen, true);

  g_rt->free(buf);

  char msg[76];
  if (ok_nsi && ok_ui && ok_c) {
    g_rt->snprintf(msg, sizeof(msg),
                   "[INSTALLER] Creato %s_setup.nsi (%d file, link: PKGSVC.DLL)",
                   g_proj_name, inc_cnt);
    if (g_rt->notify)
      g_rt->notify("NewOS Studio - Installer", msg);
  } else {
    g_rt->snprintf(msg, sizeof(msg),
                   "[ERR] Creazione installer per '%s' fallita", g_proj_name);
  }
  st_push_output(msg);
  dll_strcpy(g_status, msg, sizeof(g_status));
}

static void st_build_verify_project(void) {
  st_save_project();
  int prev_tab = g_doc_tab;
  g_doc_tab = 2;
  st_run_code_analysis();
  int c_err = g_code_errors;
  g_doc_tab = 1;
  st_run_code_analysis();
  int x_err = g_code_errors;
  g_doc_tab = prev_tab;
  g_code_errors = c_err + x_err;
  if (g_rt && g_rt->snprintf) {
    char msg[76];
    g_rt->snprintf(msg, sizeof(msg),
                   "[BUILD] '%s': %d err (C:%d, XML:%d), %d controlli",
                   g_proj_name, g_code_errors, c_err, x_err, g_ctrl_count);
    st_push_output(msg);
    dll_strcpy(g_status, msg, sizeof(g_status));
  }
}

static void st_open_comdlg_cb(const char *path, void *ud) {
  (void)ud;
  if (path && path[0]) {
    if (!st_load_project_file(path)) {
      st_push_output("[ERR] File .nsproj non valido o illeggibile.");
    }
  }
}

static void st_preview_event(nsrt_app_t *app, const char *id, void *ctx) {
  (void)ctx;
  if (!g_rt || !app || !id)
    return;
  if (g_rt->strcmp(id, "btn_exit") == 0) {
    g_rt->close_app(app);
    return;
  }
  g_rt->set_text(app, "lbl_msg", "Evento click eseguito via NSRT.DLL!");
}

static void st_launch_preview(void) {
  if (!st_save_project())
    return;
  char xpath[160];
  st_proj_path(xpath, sizeof(xpath), "studio_form.xuiml");
  nsrt_app_t *p = g_rt->create_xuiml_app("NewOS Studio - Anteprima Live", 420,
                                         280, xpath, st_preview_event, NULL);
  st_push_output(p ? "[RUN] Anteprima Live avviata con XUIML.DLL + NSRT.DLL"
                   : "[ERR] Impossibile avviare anteprima");
  dll_strcpy(g_status,
             p ? "In esecuzione: Anteprima Live XUIML"
               : "Errore avvio anteprima",
             sizeof(g_status));
}

static void st_open_code_for_ctrl(int idx) {
  if (idx < 0 || idx >= g_ctrl_count)
    return;
  g_sel_idx = idx;
  st_regen_c_code(g_ctrls[idx].id);
  g_doc_tab = 2; /* Apre studio_app.c al posto del Form Preview */
  g_edit_code = true;
  g_edit_prop_text = false;
  if (g_rt && g_rt->snprintf) {
    char msg[72];
    g_rt->snprintf(msg, sizeof(msg),
                   "[IDE] Generato handler NS_ON_CLICK(\"%s\") in studio_app.c",
                   g_ctrls[idx].id);
    st_push_output(msg);
    g_rt->snprintf(
        g_status, sizeof(g_status),
        "Code Viewer: studio_app.c (focus su %s - digita per scrivere)",
        g_ctrls[idx].id);
  }
}

/* Helper grafico bottone: usa il motore temi di sistema se agganciato al
 * sistema, altrimenti flat VS Dark */
static void vs_draw_btn(const vs_palette_t *p, int32_t x, int32_t y, int32_t w,
                        int32_t h, const char *lbl, bool primary) {
  if (!g_force_dark_theme && !primary && g_api->wm_draw_themed_button) {
    g_api->wm_draw_themed_button(x, y, w, h, lbl, false);
    return;
  }
  uint32_t bg = primary ? p->accent : p->bg_header;
  uint32_t fg = primary ? p->accent_text : p->text;
  if (p->modern && p->radius > 0 && g_api->gfx_fill_rounded_rect) {
    g_api->gfx_fill_rounded_rect(x, y, w, h, 4, bg);
    g_api->gfx_draw_rounded_rect(x, y, w, h, 4,
                                 primary ? p->accent : p->border);
  } else {
    g_api->gfx_fill_rect(x, y, w, h, bg);
    g_api->gfx_draw_rect(x, y, w, h, primary ? p->accent : p->border);
  }
  g_api->gfx_draw_string(x + 8, y + (h - 14) / 2, lbl, fg, 0, true);
}

static void vs_draw_code_lines(const vs_palette_t *p, int32_t x, int32_t y,
                               int32_t w, int32_t h, const char *src) {
  int32_t sb = 8;
  vs_draw_surface(p, x, y, w, h, p->bg_editor, 220);
  g_api->gfx_fill_rect(x + 2, y + 4, 34, h - sb - 8, p->bg_gutter);
  g_api->gfx_fill_rect(x + 36, y + 4, 1, h - sb - 8, p->border);

  /* 1. Calcola dimensioni totali del documento (righe e lunghezza massima riga)
   */
  int total_rows = 0;
  int max_cols = 0;
  const char *scan = src;
  while (*scan) {
    int len = 0;
    while (*scan && *scan != '\n') {
      len++;
      scan++;
    }
    if (len > max_cols)
      max_cols = len;
    total_rows++;
    if (*scan == '\n')
      scan++;
  }
  g_code_total_rows = total_rows;
  g_code_max_cols = max_cols;

  int32_t vis_rows = (h - sb - 10) / 18;
  if (vis_rows < 1)
    vis_rows = 1;
  int32_t vis_cols = (w - 44 - sb - 8) / 8;
  if (vis_cols < 8)
    vis_cols = 8;

  int max_sy = (total_rows > vis_rows) ? (total_rows - vis_rows) : 0;
  int max_sx = (max_cols > vis_cols) ? (max_cols - vis_cols) : 0;
  if (g_scroll_y < 0)
    g_scroll_y = 0;
  if (g_scroll_y > max_sy)
    g_scroll_y = max_sy;
  if (g_scroll_x < 0)
    g_scroll_x = 0;
  if (g_scroll_x > max_sx)
    g_scroll_x = max_sx;

  /* 2. Salta le prime g_scroll_y righe */
  const char *s = src;
  for (int r = 0; r < g_scroll_y && *s; r++) {
    while (*s && *s != '\n')
      s++;
    if (*s == '\n')
      s++;
  }

  /* 3. Disegna le righe visibili con clipping rigoroso [g_scroll_x ..
   * g_scroll_x + vis_cols] */
  int32_t row = 0;
  while (*s && row < vis_rows) {
    char full[256];
    int flen = 0;
    while (*s && *s != '\n' && flen < 255)
      full[flen++] = *s++;
    while (*s && *s != '\n')
      s++;
    if (*s == '\n')
      s++;
    full[flen] = '\0';

    char lnum[8];
    if (g_rt && g_rt->snprintf)
      g_rt->snprintf(lnum, sizeof(lnum), "%d", g_scroll_y + row + 1);
    else {
      lnum[0] = '1';
      lnum[1] = '\0';
    }
    g_api->gfx_draw_string(x + 6, y + 6 + row * 18, lnum, p->text_muted, 0,
                           true);

    uint32_t col = p->text;
    if (full[0] == '/' || full[0] == '<')
      col = (full[0] == '/') ? p->code_cmt : p->code_kw;
    else if (full[0] == '#')
      col = p->code_prep;
    if (flen > 6 && full[4] == 'N' && full[5] == 'S' && full[6] == '_')
      col = p->code_fn;

    if (flen > g_scroll_x) {
      char clipped[128];
      int ci = 0;
      int lim = vis_cols < 127 ? vis_cols : 127;
      for (int k = g_scroll_x; k < flen && ci < lim; k++)
        clipped[ci++] = full[k];
      clipped[ci] = '\0';
      g_api->gfx_draw_string(x + 42, y + 6 + row * 18, clipped, col, 0, true);
    }
    row++;
  }

  /* 4. Scrollbar Verticale sottile (pillola 5px dentro track 8px) */
  int32_t vx = x + w - sb - 4, vy = y + 6, vh = h - sb - 12;
  if (vh > 16) {
    if (g_api->gfx_fill_rounded_alpha) {
      g_api->gfx_fill_rounded_alpha(vx, vy, sb, vh, 4,
                                    p->text_muted & 0x00FFFFFFu, 32);
    }
    int32_t vthumb_h =
        (total_rows > vis_rows) ? (vh * vis_rows) / total_rows : vh;
    if (vthumb_h < 22)
      vthumb_h = 22;
    int32_t vthumb_y =
        vy + ((max_sy > 0) ? ((vh - vthumb_h) * g_scroll_y) / max_sy : 0);
    uint32_t vcol = (max_sy > 0) ? p->accent : p->text_muted;
    uint8_t valpha = (max_sy > 0) ? 200 : 75;
    if (g_api->gfx_fill_rounded_alpha) {
      g_api->gfx_fill_rounded_alpha(vx + 1, vthumb_y, sb - 2, vthumb_h, 3,
                                    vcol & 0x00FFFFFFu, valpha);
    } else {
      g_api->gfx_fill_rect(vx + 1, vthumb_y, sb - 2, vthumb_h, vcol);
    }
  }

  /* 5. Scrollbar Orizzontale sottile (pillola 5px dentro track 8px) */
  int32_t hx = x + 42, hy = y + h - sb - 4, hw = w - 42 - sb - 8;
  if (hw > 16) {
    if (g_api->gfx_fill_rounded_alpha) {
      g_api->gfx_fill_rounded_alpha(hx, hy, hw, sb, 4,
                                    p->text_muted & 0x00FFFFFFu, 32);
    }
    int32_t hthumb_w = (max_cols > vis_cols) ? (hw * vis_cols) / max_cols : hw;
    if (hthumb_w < 26)
      hthumb_w = 26;
    int32_t hthumb_x =
        hx + ((max_sx > 0) ? ((hw - hthumb_w) * g_scroll_x) / max_sx : 0);
    uint32_t hcol = (max_sx > 0) ? p->accent : p->text_muted;
    uint8_t halpha = (max_sx > 0) ? 200 : 75;
    if (g_api->gfx_fill_rounded_alpha) {
      g_api->gfx_fill_rounded_alpha(hthumb_x, hy + 1, hthumb_w, sb - 2, 3,
                                    hcol & 0x00FFFFFFu, halpha);
    } else {
      g_api->gfx_fill_rect(hthumb_x, hy + 1, hthumb_w, sb - 2, hcol);
    }
  }
}

typedef struct {
  int32_t x, y, w, h;
  const char *lbl;
  int act;
} vs_rect_btn_t;
static const vs_rect_btn_t g_tb_btns[] = {
    {8, 28, 92, 22, "Nuovo Prog.", 1},
    {106, 28, 92, 22, "Progetti...", 20},
    {204, 28, 92, 22, "Salva Prog.", 21},
    {302, 28, 136, 22, "> Avvia Preview", 7},
    {444, 28, 76, 22, "Elimina", 16}};

static const vs_rect_btn_t g_prop_btns[] = {
    {0, 152, 46, 20, "X-10", 10},   {50, 152, 46, 20, "X+10", 11},
    {100, 152, 46, 20, "Y-10", 12}, {150, 152, 46, 20, "Y+10", 13},
    {0, 176, 62, 20, "W+20", 14},   {66, 176, 62, 20, "W-20", 15}};

static void studio_draw(window_t *win) {
  if (!g_api)
    return;
  vs_palette_t pal = st_get_palette();
  const vs_palette_t *p = &pal;

  int32_t cx = win->x + 4;
  int32_t cy = win->y + 28;
  int32_t cw = win->width - 8;
  int32_t ch = win->height - 32;

  /* Sfondo IDE: in Liquid Glass lasciamo trasparire il vetro del WM; altrimenti
   * riempimento opaco */
  if (!p->is_glass) {
    g_api->gfx_fill_rect(cx, cy, cw, ch, p->bg_editor);
  }

  /* Menu Bar + Toolbar */
  if (p->is_glass && g_api->gfx_fill_rounded_alpha) {
    g_api->gfx_fill_rounded_alpha(cx + 2, cy + 2, cw - 4, 52, p->radius,
                                  p->bg_panel & 0x00FFFFFFu, 175);
    if (g_api->gfx_draw_specular_rim)
      g_api->gfx_draw_specular_rim(cx + 2, cy + 2, cw - 4, 52, p->radius);
  } else {
    g_api->gfx_fill_rect(cx, cy, cw, 24, p->bg_header);
    g_api->gfx_fill_rect(cx, cy + 24, cw, 30, p->bg_panel);
    g_api->gfx_fill_rect(cx, cy + 54, cw, 1, p->border);
  }
  g_api->gfx_draw_string(
      cx + 10, cy + 5,
      "File   Modifica   Visualizza   Progetto   Compila   Debug   Aiuto",
      p->text, 0, true);
  g_api->gfx_draw_string(
      cx + cw - 156, cy + 5,
      g_force_dark_theme ? "[Tema: VS Dark]"
                         : (p->is_glass ? "[Liquid Glass]" : "[Tema: Sistema]"),
      p->accent, 0, true);

  for (int i = 0; i < 5; i++) {
    vs_draw_btn(p, cx + g_tb_btns[i].x, cy + g_tb_btns[i].y, g_tb_btns[i].w,
                g_tb_btns[i].h, g_tb_btns[i].lbl, g_tb_btns[i].act == 7);
  }

  int32_t body_y = cy + 58;
  int32_t body_h = ch - 58 - 26; /* -26 status bar */
  int32_t out_h = 96;
  int32_t mid_h = body_h - out_h;

  /* 1. Sinistra: Toolbox (Design) o Strumenti Codice (Code Viewer) */
  int32_t tx = cx + 2, tw = 180;
  vs_draw_surface(p, tx, body_y, tw, body_h, p->bg_panel, 170);
  if (g_doc_tab == 0) {
    g_api->gfx_draw_string(tx + 10, body_y + 6, "Toolbox", p->text, 0, true);
    g_api->gfx_draw_string(tx + 10, body_y + 28, "v Controlli Comuni",
                           p->text_muted, 0, true);

    const char *tb_items[] = {"+ Label", "+ Button", "+ TextBox", "+ CheckBox",
                              "+ GroupBox"};
    for (int i = 0; i < 5; i++) {
      vs_draw_btn(p, tx + 10, body_y + 50 + i * 30, tw - 20, 24, tb_items[i],
                  false);
    }
  } else if (g_doc_tab == 3) {
    g_api->gfx_draw_string(tx + 10, body_y + 6, "Strumenti Progetto", p->text,
                           0, true);
    g_api->gfx_draw_string(tx + 10, body_y + 26, "v Personalizza",
                           p->text_muted, 0, true);
    if (g_rt && g_rt->snprintf) {
      char tb1[32], tb2[32], tb3[20], tb4[20], tb5[32];
      g_rt->snprintf(tb1, sizeof(tb1), "Target: %s",
                     g_proj_target_efi ? "x86+EFI" : "x86_64");
      g_rt->snprintf(tb2, sizeof(tb2), "Tipo: %s",
                     g_proj_app_type ? "Console (CMD)" : "Desktop GUI");
      g_rt->snprintf(tb3, sizeof(tb3), "Ver:1.%d+", g_proj_ver_minor);
      g_rt->snprintf(tb4, sizeof(tb4), "Bld:#%d+", g_proj_build_num);
      g_rt->snprintf(tb5, sizeof(tb5), "   Icona: %s",
                     g_proj_icon_names[g_proj_icon_idx & 7]);
      vs_draw_btn(p, tx + 10, body_y + 44, tw - 20, 22, tb1, false);
      vs_draw_btn(p, tx + 10, body_y + 70, tw - 20, 22, tb2,
                  g_proj_app_type == 1);
      vs_draw_btn(p, tx + 10, body_y + 96, 78, 22, tb3, false);
      vs_draw_btn(p, tx + 92, body_y + 96, 78, 22, tb4, false);
      vs_draw_btn(p, tx + 10, body_y + 122, tw - 20, 24, tb5, false);
      if (g_rt->gfx_draw_icon16)
        g_rt->gfx_draw_icon16(tx + 15, body_y + 126,
                              g_proj_icons[g_proj_icon_idx & 7]);
    }

    g_api->gfx_draw_string(tx + 10, body_y + 154, "v Preset Copia Output",
                           p->text_muted, 0, true);
    vs_draw_btn(p, tx + 10, body_y + 172, tw - 20, 22, "Solo Runtime", false);
    vs_draw_btn(p, tx + 10, body_y + 198, tw - 20, 22, "Sorgenti + SDK", false);
    vs_draw_btn(p, tx + 10, body_y + 224, tw - 20, 22, "Copia Tutto", false);
    vs_draw_btn(p, tx + 10, body_y + 250, tw - 20, 22, "Aggiorna Lista", false);

    g_api->gfx_draw_string(tx + 10, body_y + 280, "v Compila & Deploy",
                           p->text_muted, 0, true);
    vs_draw_btn(p, tx + 10, body_y + 298, tw - 20, 24, "Verifica & Compila",
                true);
    vs_draw_btn(p, tx + 10, body_y + 326, tw - 20, 24, "Pubblica Eseguibile",
                true);
    vs_draw_btn(p, tx + 10, body_y + 354, tw - 20, 24, "Crea Installer", true);
    g_api->gfx_draw_string(tx + 10, body_y + 384, "Linker: PKGSVC.DLL",
                           p->status_ok, 0, true);
  } else {
    if (g_code_lines_cnt == 0)
      st_run_code_analysis();
    g_api->gfx_draw_string(tx + 10, body_y + 6, "Strumenti Codice", p->text, 0,
                           true);
    g_api->gfx_draw_string(tx + 10, body_y + 28, "v Analisi Rapida",
                           p->text_muted, 0, true);
    if (g_rt && g_rt->snprintf) {
      char l1[36], l2[36], l3[36];
      g_rt->snprintf(l1, sizeof(l1), "Righe: %d  (%d B)", g_code_lines_cnt,
                     g_code_bytes_cnt);
      if (g_code_errors == 0)
        g_rt->snprintf(l2, sizeof(l2), "Sintassi: OK (0 err)");
      else
        g_rt->snprintf(l2, sizeof(l2), "Errori: %d", g_code_errors);
      g_rt->snprintf(l3, sizeof(l3),
                     g_doc_tab == 1 ? "Tag UI: %d" : "Handler UI: %d",
                     g_code_handlers);
      g_api->gfx_draw_string(tx + 12, body_y + 48, l1, p->text, 0, true);
      g_api->gfx_draw_string(tx + 12, body_y + 66, l2,
                             g_code_errors == 0 ? 0xFF4EC9B0u : 0xFFF48771u, 0,
                             true);
      g_api->gfx_draw_string(tx + 12, body_y + 84, l3, p->text_muted, 0, true);
    }
    vs_draw_btn(p, tx + 10, body_y + 106, tw - 20, 24, "Analizza Sintassi",
                true);

    g_api->gfx_draw_string(tx + 10, body_y + 144, "v Autocompletamento",
                           p->text_muted, 0, true);
    const char *c_snips[] = {"+ NS_ON_CLICK", "+ set_text()", "+ file_write()",
                             "+ reg_read_sz()", "+ NS_LOG_INFO()"};
    const char *x_snips[] = {"+ <Label />", "+ <Button />", "+ <TextBox />",
                             "+ <CheckBox />", "+ <GroupBox />"};
    const char *const *snips = (g_doc_tab == 1) ? x_snips : c_snips;
    for (int i = 0; i < 5; i++) {
      vs_draw_btn(p, tx + 10, body_y + 166 + i * 28, tw - 20, 23, snips[i],
                  false);
    }
    vs_draw_btn(p, tx + 10, body_y + 314, tw - 20, 24, "Rigenera Codice",
                false);
  }

  /* 2. Destra: Esplora Soluzione + Proprieta' */
  int32_t rw = 240;
  int32_t rx = cx + cw - rw - 2;
  vs_draw_surface(p, rx, body_y, rw, body_h, p->bg_panel, 170);

  /* Esplora Soluzione (sopra) */
  g_api->gfx_draw_string(rx + 10, body_y + 6, "Esplora Soluzione", p->text, 0,
                         true);
  if (g_rt && g_rt->snprintf) {
    char sol_hdr[44], proj_f[40];
    g_rt->snprintf(sol_hdr, sizeof(sol_hdr), "Soluzione '%s'%s", g_proj_name,
                   g_proj_dirty ? " *" : "");
    g_rt->snprintf(proj_f, sizeof(proj_f), "%s.nsproj", g_proj_name);
    g_api->gfx_draw_string(rx + 10, body_y + 28, sol_hdr, p->text, 0, true);
    g_api->gfx_draw_string(rx + 22, body_y + 48, "studio_form.xuiml [Design]",
                           g_doc_tab == 0 ? p->accent : p->text_muted, 0, true);
    g_api->gfx_draw_string(rx + 22, body_y + 68, "studio_form.xuiml [XML]",
                           g_doc_tab == 1 ? p->accent : p->text_muted, 0, true);
    g_api->gfx_draw_string(rx + 22, body_y + 88, "studio_app.c [Code]",
                           g_doc_tab == 2 ? p->accent : p->text_muted, 0, true);
    g_api->gfx_draw_string(rx + 22, body_y + 108, proj_f,
                           g_doc_tab == 3 ? p->accent : p->status_ok, 0, true);
  } else {
    g_api->gfx_draw_string(rx + 10, body_y + 28, "Soluzione 'NewOSApp'",
                           p->text, 0, true);
    g_api->gfx_draw_string(rx + 22, body_y + 48, "studio_form.xuiml [Design]",
                           g_doc_tab == 0 ? p->accent : p->text_muted, 0, true);
    g_api->gfx_draw_string(rx + 22, body_y + 68, "studio_form.xuiml [XML]",
                           g_doc_tab == 1 ? p->accent : p->text_muted, 0, true);
    g_api->gfx_draw_string(rx + 22, body_y + 88, "studio_app.c [Code]",
                           g_doc_tab == 2 ? p->accent : p->text_muted, 0, true);
    g_api->gfx_draw_string(rx + 22, body_y + 108, "SDK: NSRT.DLL + XUIML",
                           p->status_ok, 0, true);
  }

  /* Proprieta' (sotto) */
  int32_t py = body_y + 134;
  g_api->gfx_fill_rect(rx + 8, py - 4, rw - 16, 1, p->border);
  g_api->gfx_draw_string(rx + 10, py + 4, "Proprieta'", p->text, 0, true);
  if (g_sel_idx >= 0 && g_sel_idx < g_ctrl_count && g_rt && g_rt->snprintf) {
    const st_ctrl_t *s = &g_ctrls[g_sel_idx];
    char r1[36], r2[36], r3[36];
    g_rt->snprintf(r1, sizeof(r1), "ID:   %s (%s)", s->id,
                   st_type_tag(s->type));
    g_rt->snprintf(r2, sizeof(r2), "Pos:  X=%d  Y=%d", (int)s->x, (int)s->y);
    g_rt->snprintf(r3, sizeof(r3), "Size: W=%d  H=%d", (int)s->w, (int)s->h);
    g_api->gfx_draw_string(rx + 10, py + 30, r1, p->accent, 0, true);
    g_api->gfx_draw_string(rx + 10, py + 50, r2, p->text, 0, true);
    g_api->gfx_draw_string(rx + 10, py + 70, r3, p->text, 0, true);
    g_api->gfx_draw_string(rx + 10, py + 92,
                           "Text (click per edit):", p->text_muted, 0, true);
    g_api->gfx_fill_rect(rx + 10, py + 110, rw - 22, 22, p->bg_editor);
    g_api->gfx_draw_rect(rx + 10, py + 110, rw - 22, 22,
                         g_edit_prop_text ? p->accent : p->border);
    char tbuf[28];
    dll_strcpy(tbuf, s->text, 24);
    g_api->gfx_draw_string(rx + 14, py + 114, tbuf, p->text, 0, true);
  }
  for (int i = 0; i < 6; i++) {
    vs_draw_btn(p, rx + 10 + g_prop_btns[i].x, py + g_prop_btns[i].y,
                g_prop_btns[i].w, g_prop_btns[i].h, g_prop_btns[i].lbl, false);
  }

  /* 3. Centro: Document Well (tra Toolbox e Right Panel) */
  int32_t mx = tx + tw + 4;
  int32_t mw = rx - mx - 4;

  /* Document Tabs */
  if (!p->is_glass)
    g_api->gfx_fill_rect(mx, body_y, mw, 26, p->bg_header);
  const char *doc_tabs[] = {"Form1 [Design]", "Form1 [XML]", "studio_app.c",
                            "Progetto [.nsproj]"};
  int32_t tab_w = 140;
  for (int t = 0; t < 4; t++) {
    int32_t ttx = mx + t * tab_w;
    if (p->is_glass && g_api->gfx_fill_rounded_alpha) {
      g_api->gfx_fill_rounded_alpha(
          ttx, body_y, tab_w - 4, 24, 5,
          (g_doc_tab == t ? p->bg_editor : p->bg_panel) & 0x00FFFFFFu,
          (g_doc_tab == t) ? 215 : 130);
    } else {
      g_api->gfx_fill_rect(ttx, body_y, tab_w - 1, 26,
                           (g_doc_tab == t) ? p->bg_editor : p->bg_header);
    }
    if (g_doc_tab == t)
      g_api->gfx_fill_rect(ttx + 4, body_y, tab_w - 12, 2, p->accent);
    g_api->gfx_draw_string(ttx + 10, body_y + 5, doc_tabs[t],
                           (g_doc_tab == t) ? p->text : p->text_muted, 0, true);
  }

  int32_t doc_y = body_y + 26;
  int32_t doc_h = mid_h - 30;

  if (g_doc_tab == 0) {
    /* Form Designer Visuale su superficie Liquid Glass */
    vs_draw_surface(p, mx, doc_y, mw, doc_h, p->bg_editor, 155);
    int32_t fw = 420, fh = 280;
    int32_t fx = mx + (mw - fw) / 2;
    int32_t fy = doc_y + (doc_h - fh) / 2;
    if (p->modern && g_api->gfx_fill_rounded_rect) {
      g_api->gfx_fill_rounded_rect(fx, fy, fw, fh, p->radius, 0xFFF8FAFCu);
      g_api->gfx_draw_rounded_rect(fx, fy, fw, fh, p->radius, p->accent);
    } else {
      g_api->gfx_fill_rect(fx, fy, fw, fh, 0xFFF8FAFCu);
      g_api->gfx_draw_rect(fx, fy, fw, fh, p->accent);
    }
    g_api->gfx_fill_rect(fx, fy, fw, 24, p->accent);
    g_api->gfx_draw_string(fx + 10, fy + 5,
                           "Form1 - XUIML Designer (doppio click = codice C)",
                           p->accent_text, 0, true);

    for (int32_t gy = fy + 34; gy < fy + fh - 6; gy += 16) {
      for (int32_t gx = fx + 10; gx < fx + fw - 6; gx += 16) {
        g_api->gfx_draw_pixel(gx, gy, 0xFFCBD5E1u);
      }
    }
    for (int i = 0; i < g_ctrl_count; i++) {
      const st_ctrl_t *c = &g_ctrls[i];
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
        g_api->gfx_draw_rect(ax, ay + 3, 14, 14, p->accent);
        g_api->gfx_draw_string(ax + 20, ay + 3, c->text, 0xFF0F172Au, 0, true);
      }
      if (i == g_sel_idx) {
        g_api->gfx_draw_rect(ax - 2, ay - 2, c->w + 4, c->h + 4, p->accent);
      }
    }
  } else if (g_doc_tab == 1) {
    char *xbuf = g_rt ? (char *)g_rt->alloc(4096) : NULL;
    if (xbuf) {
      st_build_xuiml(xbuf, 4096);
      vs_draw_code_lines(p, mx, doc_y, mw, doc_h, xbuf);
      g_rt->free(xbuf);
    }
  } else if (g_doc_tab == 3) {
    if (g_pfile_cnt == 0)
      st_refresh_project_files();
    vs_draw_surface(p, mx, doc_y, mw, doc_h, p->bg_editor, 165);
    if (g_rt && g_rt->snprintf) {
      if (g_rt->gfx_draw_icon32)
        g_rt->gfx_draw_icon32(mx + 14, doc_y + 7,
                              g_proj_icons[g_proj_icon_idx & 7]);
      char hdr[96], sub[96];
      g_rt->snprintf(hdr, sizeof(hdr), "%s  v1.%d.%d  (%s | %s)", g_proj_name,
                     g_proj_ver_minor, g_proj_build_num,
                     g_proj_app_type ? "Console Prompt" : "Desktop GUI",
                     g_proj_target_efi ? "x86_64+EFI" : "x86_64");
      g_rt->snprintf(sub, sizeof(sub),
                     "Icona: %s  |  Output: C:\\ProgramFile\\%s",
                     g_proj_icon_names[g_proj_icon_idx & 7], g_proj_name);
      g_api->gfx_draw_string(mx + 54, doc_y + 9, hdr, p->accent, 0, true);
      g_api->gfx_draw_string(mx + 54, doc_y + 27, sub, p->text_muted, 0, true);
    }
    g_api->gfx_fill_rect(mx + 12, doc_y + 48, mw - 24, 20, p->bg_header);
    g_api->gfx_draw_string(mx + 20, doc_y + 52, "File del Progetto", p->text, 0,
                           true);
    g_api->gfx_draw_string(mx + 235, doc_y + 52, "Utilizzo", p->text, 0, true);
    g_api->gfx_draw_string(mx + mw - 182, doc_y + 52, "Copia in Output",
                           p->text, 0, true);

    static const char *const u_lbls[5] = {"UI Layout", "Sorgente C", "Progetto",
                                          "Config", "Risorsa"};
    static const char *const c_lbls[3] = {"Non copiare  v", "Copia sempre v",
                                          "Solo se nuovo v"};
    int max_rows = (doc_h - 80) / 32;
    if (max_rows > g_pfile_cnt)
      max_rows = g_pfile_cnt;
    for (int i = 0; i < max_rows; i++) {
      int32_t ry_f = doc_y + 72 + i * 32;
      const st_proj_file_t *pf = &g_pfiles[i];
      vs_draw_surface(p, mx + 12, ry_f, mw - 24, 28, p->bg_panel, 185);
      g_api->gfx_draw_string(mx + 20, ry_f + 7, pf->name, p->text, 0, true);
      g_api->gfx_draw_string(
          mx + 235, ry_f + 7, u_lbls[pf->usage < 5 ? pf->usage : 4],
          pf->usage == 0 ? p->status_ok : p->text_muted, 0, true);
      int32_t cbx = mx + mw - 182;
      bool is_open = (g_open_combo_idx == i);
      g_api->gfx_fill_rect(cbx, ry_f + 3, 162, 22, p->bg_editor);
      g_api->gfx_draw_rect(cbx, ry_f + 3, 162, 22,
                           is_open ? p->accent : p->border);
      g_api->gfx_draw_string(
          cbx + 8, ry_f + 7, c_lbls[pf->copy_mode < 3 ? pf->copy_mode : 0],
          pf->copy_mode == 0 ? p->text_muted : p->accent, 0, true);
    }
    /* Dropdown a singola scelta disegnato sopra le righe */
    if (g_open_combo_idx >= 0 && g_open_combo_idx < max_rows) {
      int32_t cbx = mx + mw - 182;
      int32_t ddy = doc_y + 72 + g_open_combo_idx * 32 + 25;
      static const char *const opts[3] = {"Non copiare", "Copia sempre",
                                          "Solo se nuovo"};
      g_api->gfx_fill_rect(cbx, ddy, 162, 66, p->bg_panel);
      g_api->gfx_draw_rect(cbx, ddy, 162, 66, p->accent);
      for (int o = 0; o < 3; o++) {
        bool sel = (g_pfiles[g_open_combo_idx].copy_mode == (uint8_t)o);
        if (sel)
          g_api->gfx_fill_rect(cbx + 1, ddy + 1 + o * 22, 160, 20, p->accent);
        g_api->gfx_draw_string(cbx + 8, ddy + 4 + o * 22, opts[o],
                               sel ? p->accent_text : p->text, 0, true);
      }
    }
  } else {
    vs_draw_code_lines(p, mx, doc_y, mw, doc_h, g_c_code);
  }

  /* 4. Basso: Output & Error List */
  int32_t oy = body_y + mid_h;
  vs_draw_surface(p, mx, oy, mw, out_h - 2, p->bg_panel, 175);
  if (g_rt && g_rt->snprintf) {
    char ohdr[64];
    g_rt->snprintf(ohdr, sizeof(ohdr),
                   "Output   |   Elenco Errori (%d)   |   NSRT Log",
                   g_code_errors);
    g_api->gfx_draw_string(mx + 10, oy + 5, ohdr, p->text, 0, true);
  } else {
    g_api->gfx_draw_string(mx + 10, oy + 5,
                           "Output   |   Elenco Errori (0)   |   NSRT Log",
                           p->text, 0, true);
  }
  for (int i = 0; i < 4; i++) {
    g_api->gfx_draw_string(mx + 10, oy + 26 + i * 16, g_output_lines[i],
                           p->text_muted, 0, true);
  }

  /* Overlay Modale: Gestione Progetti (C:\User\Projects) */
  if (g_show_proj_mgr) {
    int32_t pmx = mx + 20, pmy = doc_y + 12;
    int32_t pmw = mw - 40, pmh = doc_h - 24;
    vs_draw_surface(p, pmx, pmy, pmw, pmh, p->bg_panel, 245);
    g_api->gfx_fill_rect(pmx + 1, pmy + 1, pmw - 2, 28, p->accent);
    g_api->gfx_draw_string(pmx + 12, pmy + 7,
                           "Gestione Progetti  (C:\\User\\Projects)",
                           p->accent_text, 0, true);
    vs_draw_btn(p, pmx + pmw - 72, pmy + 3, 64, 22, "Chiudi", false);

    /* Riga 1: Nome personalizzato + Template + Crea */
    g_api->gfx_draw_string(pmx + 14, pmy + 41, "Nome:", p->text_muted, 0, true);
    g_api->gfx_fill_rect(pmx + 60, pmy + 37, 130, 22, p->bg_editor);
    g_api->gfx_draw_rect(pmx + 60, pmy + 37, 130, 22,
                         g_edit_proj_name ? p->accent : p->border);
    g_api->gfx_draw_string(
        pmx + 65, pmy + 41, g_new_proj_buf[0] ? g_new_proj_buf : "App_...",
        g_new_proj_buf[0] ? p->text : p->text_muted, 0, true);

    const char *tpl_lbls[3] = {"Vuoto", "SDK App", "SysInfo"};
    for (int t = 0; t < 3; t++) {
      vs_draw_btn(p, pmx + 196 + t * 70, pmy + 37, 66, 22, tpl_lbls[t],
                  g_proj_tpl == t);
    }
    vs_draw_btn(p, pmx + 412, pmy + 37, 82, 22, "+ Crea", true);

    /* Riga 2: Azioni progetto corrente */
    vs_draw_btn(p, pmx + 14, pmy + 68, 96, 22, "Salva", false);
    vs_draw_btn(p, pmx + 116, pmy + 68, 178, 22, "Pubblica (ProgramFile)",
                false);
    vs_draw_btn(p, pmx + 300, pmy + 68, 162, 22, "Sfoglia .nsproj...", false);

    g_api->gfx_draw_string(pmx + 14, pmy + 100,
                           "Progetti su disco:", p->text_muted, 0, true);
    int show_cnt = g_proj_list_cnt;
    if (show_cnt > 5)
      show_cnt = 5;
    for (int i = 0; i < show_cnt; i++) {
      int32_t ry_row = pmy + 120 + i * 34;
      bool is_cur = (g_rt && g_rt->strcmp &&
                     g_rt->strcmp(g_proj_list[i], g_proj_name) == 0);
      vs_draw_surface(p, pmx + 14, ry_row, pmw - 28, 28, p->bg_editor, 195);
      if (g_rt && g_rt->snprintf) {
        char row_lbl[64];
        g_rt->snprintf(row_lbl, sizeof(row_lbl), "%s%s", g_proj_list[i],
                       is_cur ? "  [Attivo]" : "");
        g_api->gfx_draw_string(pmx + 24, ry_row + 6, row_lbl,
                               is_cur ? p->accent : p->text, 0, true);
      }
      vs_draw_btn(p, pmx + pmw - 196, ry_row + 3, 54, 22, "Apri", !is_cur);
      vs_draw_btn(p, pmx + pmw - 138, ry_row + 3, 58, 22, "Copia", false);
      vs_draw_btn(p, pmx + pmw - 76, ry_row + 3, 52, 22, "Elim.", false);
    }
    if (show_cnt == 0) {
      g_api->gfx_draw_string(
          pmx + 16, pmy + 126,
          "Nessuna cartella trovata. Premi '+ Crea' o 'Salva'.", p->text_muted,
          0, true);
    }
  }

  /* 5. Barra di Stato */
  int32_t sy = cy + ch - 24;
  g_api->gfx_fill_rect(cx, sy, cw, 24, p->accent);
  g_api->gfx_draw_string(cx + 10, sy + 5, g_status, p->accent_text, 0, true);
  if (g_rt && g_rt->snprintf) {
    char rinfo[48];
    g_rt->snprintf(rinfo, sizeof(rinfo), "Controlli: %d  |  x86_64",
                   g_ctrl_count);
    g_api->gfx_draw_string(cx + cw - 175, sy + 5, rinfo, p->accent_text, 0,
                           true);
  }
}

static void st_exec_action(int act) {
  if (act == 1) {
    st_new_project();
    return;
  }
  if (act == 20) {
    st_refresh_project_list();
    g_show_proj_mgr = !g_show_proj_mgr;
    return;
  }
  if (act == 21) {
    st_save_project();
    return;
  }
  if (act == 2) {
    st_add_control(ST_LABEL);
    return;
  }
  if (act == 3) {
    st_add_control(ST_BUTTON);
    return;
  }
  if (act == 4) {
    st_add_control(ST_TEXTBOX);
    return;
  }
  if (act == 5) {
    st_add_control(ST_CHECKBOX);
    return;
  }
  if (act == 6) {
    st_add_control(ST_GROUPBOX);
    return;
  }
  if (act == 7) {
    st_launch_preview();
    return;
  }
  if (act == 8) {
    st_export_xuiml();
    return;
  }
  if (act == 9) {
    st_export_c_app();
    return;
  }

  if (g_sel_idx < 0 || g_sel_idx >= g_ctrl_count)
    return;
  st_ctrl_t *s = &g_ctrls[g_sel_idx];
  if (act == 10 && s->x >= 10)
    s->x -= 10;
  else if (act == 11 && s->x + s->w < 410)
    s->x += 10;
  else if (act == 12 && s->y >= 34)
    s->y -= 10;
  else if (act == 13 && s->y + s->h < 270)
    s->y += 10;
  else if (act == 14 && s->w < 380)
    s->w += 20;
  else if (act == 15 && s->w > 40)
    s->w -= 20;
  else if (act == 16) {
    for (int i = g_sel_idx; i < g_ctrl_count - 1; i++)
      g_ctrls[i] = g_ctrls[i + 1];
    g_ctrl_count--;
    if (g_sel_idx >= g_ctrl_count)
      g_sel_idx = g_ctrl_count - 1;
    st_regen_c_code(NULL);
    st_push_output("[IDE] Controllo rimosso dal form.");
  }
  g_proj_dirty = true;
}

static void studio_mouse(window_t *win, int32_t lx, int32_t ly, bool clicked) {
  if (!clicked)
    return;
  int32_t rx = lx - 4;
  int32_t ry = ly - 28;
  int32_t cw = win->width - 8;
  int32_t ch = win->height - 32;

  /* Toggle Tema Sistema / VS Dark in alto a destra nella Menu Bar */
  if (ry >= 0 && ry < 24 && rx >= cw - 165) {
    g_force_dark_theme = !g_force_dark_theme;
    st_push_output(g_force_dark_theme
                       ? "[UI] Tema impostato su: VS Dark"
                       : "[UI] Tema agganciato al Sistema (Auto)");
    return;
  }

  /* Menu Bar: File / Progetto aprono Gestione Progetti; Compila verifica il
   * progetto */
  if (ry >= 0 && ry < 24 && (rx < 44 || (rx >= 200 && rx < 275))) {
    st_refresh_project_list();
    g_show_proj_mgr = !g_show_proj_mgr;
    g_edit_proj_name = false;
    return;
  }
  if (ry >= 0 && ry < 24 && rx >= 278 && rx < 344) {
    st_build_verify_project();
    return;
  }

  /* Toolbar */
  for (int i = 0; i < 5; i++) {
    if (rx >= g_tb_btns[i].x && rx < g_tb_btns[i].x + g_tb_btns[i].w &&
        ry >= g_tb_btns[i].y && ry < g_tb_btns[i].y + g_tb_btns[i].h) {
      g_edit_prop_text = false;
      g_edit_code = false;
      st_exec_action(g_tb_btns[i].act);
      return;
    }
  }

  int32_t body_y = 55;
  int32_t body_h = ch - 55 - 24;
  int32_t out_h = 96;
  int32_t mid_h = body_h - out_h;
  int32_t tw = 184;
  int32_t rw = 244;
  int32_t rx_panel = cw - rw;
  int32_t mx = tw, mw = rx_panel - mx;

  /* Gestione click nel modale Gestione Progetti */
  if (g_show_proj_mgr) {
    int32_t doc_y = body_y + 26, doc_h = mid_h - 30;
    int32_t pmx = mx + 20, pmy = doc_y + 12;
    int32_t pmw = mw - 40, pmh = doc_h - 24;
    if (rx >= pmx && rx < pmx + pmw && ry >= pmy && ry < pmy + pmh) {
      if (rx >= pmx + pmw - 72 && rx < pmx + pmw - 8 && ry >= pmy + 3 &&
          ry < pmy + 25) {
        g_show_proj_mgr = false;
        g_edit_proj_name = false;
        return;
      }
      if (ry >= pmy + 37 && ry < pmy + 59) {
        if (rx >= pmx + 60 && rx < pmx + 190) {
          g_edit_proj_name = true;
          g_edit_prop_text = false;
          g_edit_code = false;
          return;
        }
        g_edit_proj_name = false;
        for (int t = 0; t < 3; t++) {
          if (rx >= pmx + 196 + t * 70 && rx < pmx + 196 + t * 70 + 66) {
            g_proj_tpl = t;
            return;
          }
        }
        if (rx >= pmx + 412 && rx < pmx + 494) {
          st_new_project();
          return;
        }
      }
      if (ry >= pmy + 68 && ry < pmy + 90) {
        g_edit_proj_name = false;
        if (rx >= pmx + 14 && rx < pmx + 110) {
          st_save_project();
          return;
        }
        if (rx >= pmx + 116 && rx < pmx + 294) {
          st_deploy_project();
          return;
        }
        if (rx >= pmx + 300 && rx < pmx + 462 && g_rt &&
            g_rt->open_file_dialog) {
          g_show_proj_mgr = false;
          g_rt->open_file_dialog("C:\\User\\Projects", st_open_comdlg_cb, NULL);
          return;
        }
      }
      int show_cnt = g_proj_list_cnt > 5 ? 5 : g_proj_list_cnt;
      for (int i = 0; i < show_cnt; i++) {
        int32_t ry_row = pmy + 120 + i * 34;
        if (ry >= ry_row && ry < ry_row + 28) {
          if (rx >= pmx + pmw - 196 && rx < pmx + pmw - 142) {
            st_load_project_by_name(g_proj_list[i]);
            return;
          }
          if (rx >= pmx + pmw - 138 && rx < pmx + pmw - 80) {
            st_duplicate_project(g_proj_list[i]);
            return;
          }
          if (rx >= pmx + pmw - 76 && rx < pmx + pmw - 24) {
            st_delete_project(g_proj_list[i]);
            return;
          }
        }
      }
      return;
    }
    g_show_proj_mgr = false;
    g_edit_proj_name = false;
    return;
  }

  /* Pannello sinistro: Toolbox (Design) o Strumenti Codice (Code Viewer) */
  if (rx >= 10 && rx < tw - 12) {
    if (g_doc_tab == 0 && ry >= body_y + 50 && ry < body_y + 200) {
      int ti = (ry - (body_y + 50)) / 30;
      if (ti >= 0 && ti < 5) {
        g_edit_prop_text = false;
        g_edit_code = false;
        st_exec_action(2 + ti);
        return;
      }
    } else if (g_doc_tab == 3) {
      if (ry >= body_y + 44 && ry < body_y + 66) {
        g_proj_target_efi = !g_proj_target_efi;
        g_proj_dirty = true;
        return;
      }
      if (ry >= body_y + 70 && ry < body_y + 92) {
        g_proj_app_type = !g_proj_app_type;
        if (g_proj_app_type == 1 && g_proj_icon_idx == 0)
          g_proj_icon_idx = 1;
        else if (g_proj_app_type == 0 && g_proj_icon_idx == 1)
          g_proj_icon_idx = 0;
        st_regen_c_code(NULL);
        g_proj_dirty = true;
        st_push_output(g_proj_app_type
                           ? "[PROJ] Tipo app: Console (Prompt dei Comandi)"
                           : "[PROJ] Tipo app: Desktop GUI (XUIML)");
        return;
      }
      if (ry >= body_y + 96 && ry < body_y + 118) {
        if (rx < 90)
          g_proj_ver_minor = (g_proj_ver_minor + 1) % 10;
        else
          g_proj_build_num++;
        g_proj_dirty = true;
        return;
      }
      if (ry >= body_y + 122 && ry < body_y + 146) {
        g_proj_icon_idx = (g_proj_icon_idx + 1) & 7;
        g_proj_dirty = true;
        return;
      }
      if (ry >= body_y + 172 && ry < body_y + 194) {
        st_apply_copy_preset(0);
        return;
      }
      if (ry >= body_y + 198 && ry < body_y + 220) {
        st_apply_copy_preset(1);
        return;
      }
      if (ry >= body_y + 224 && ry < body_y + 246) {
        st_apply_copy_preset(2);
        return;
      }
      if (ry >= body_y + 250 && ry < body_y + 272) {
        st_refresh_project_files();
        return;
      }
      if (ry >= body_y + 298 && ry < body_y + 322) {
        g_proj_build_num++;
        st_build_verify_project();
        return;
      }
      if (ry >= body_y + 326 && ry < body_y + 350) {
        st_deploy_project();
        return;
      }
      if (ry >= body_y + 354 && ry < body_y + 378) {
        st_create_installer();
        return;
      }
    } else if (g_doc_tab == 1 || g_doc_tab == 2) {
      if (ry >= body_y + 106 && ry < body_y + 132) {
        st_run_code_analysis();
        return;
      }
      if (ry >= body_y + 166 && ry < body_y + 306) {
        int si = (ry - (body_y + 166)) / 28;
        if (si >= 0 && si < 5) {
          g_edit_prop_text = false;
          st_insert_c_snippet(si);
          return;
        }
      }
      if (ry >= body_y + 314 && ry < body_y + 340) {
        if (g_doc_tab == 2)
          st_regen_c_code(g_sel_idx >= 0 ? g_ctrls[g_sel_idx].id : NULL);
        st_run_code_analysis();
        return;
      }
    }
  }

  /* Esplora Soluzione clicks */
  if (rx >= rx_panel && ry >= body_y + 46 && ry < body_y + 128) {
    int si = (ry - (body_y + 46)) / 20;
    if (si >= 0 && si <= 3) {
      g_doc_tab = si;
      g_edit_code = (si == 2);
      g_open_combo_idx = -1;
      g_scroll_x = 0;
      g_scroll_y = 0;
      if (si == 3)
        st_refresh_project_files();
      return;
    }
  }

  /* Proprieta' */
  int32_t py = body_y + 134;
  if (rx >= rx_panel + 10 && rx < cw - 12 && ry >= py + 110 && ry < py + 132 &&
      g_sel_idx >= 0) {
    g_edit_prop_text = true;
    g_edit_code = false;
    dll_strcpy(g_status, "Modifica testo controllo: digita e premi Invio",
               sizeof(g_status));
    return;
  }
  for (int i = 0; i < 6; i++) {
    int32_t bx = rx_panel + 10 + g_prop_btns[i].x;
    int32_t by = py + g_prop_btns[i].y;
    if (rx >= bx && rx < bx + g_prop_btns[i].w && ry >= by &&
        ry < by + g_prop_btns[i].h) {
      g_edit_prop_text = false;
      g_edit_code = false;
      st_exec_action(g_prop_btns[i].act);
      return;
    }
  }

  /* Document Well Tabs */
  if (rx >= mx && rx < rx_panel && ry >= body_y && ry < body_y + 26) {
    int t = (rx - mx) / 140;
    if (t >= 0 && t < 4) {
      g_doc_tab = t;
      g_edit_code = (t == 2);
      g_open_combo_idx = -1;
      g_scroll_x = 0;
      g_scroll_y = 0;
      if (t == 3)
        st_refresh_project_files();
      return;
    }
  }

  /* Combobox a singola scelta in Modalita' Progetto (tab 3) */
  if (g_doc_tab == 3 && rx >= mx && rx < rx_panel && ry >= body_y + 26 &&
      ry < body_y + mid_h) {
    int32_t doc_y = body_y + 26, doc_h = mid_h - 30;
    int max_rows = (doc_h - 80) / 32;
    if (max_rows > g_pfile_cnt)
      max_rows = g_pfile_cnt;
    int32_t cbx = mx + mw - 182;
    if (g_open_combo_idx >= 0 && g_open_combo_idx < max_rows) {
      int32_t ddy = doc_y + 72 + g_open_combo_idx * 32 + 25;
      if (rx >= cbx && rx < cbx + 162 && ry >= ddy && ry < ddy + 66) {
        int opt = (ry - ddy) / 22;
        if (opt >= 0 && opt < 3) {
          g_pfiles[g_open_combo_idx].copy_mode = (uint8_t)opt;
          g_proj_dirty = true;
        }
        g_open_combo_idx = -1;
        return;
      }
      g_open_combo_idx = -1;
      return;
    }
    for (int i = 0; i < max_rows; i++) {
      int32_t ry_f = doc_y + 72 + i * 32;
      if (rx >= cbx && rx < cbx + 162 && ry >= ry_f + 3 && ry < ry_f + 25) {
        g_open_combo_idx = i;
        return;
      }
    }
    return;
  }

  /* ScrollViewer click su Code Viewer (tab 1 e tab 2) */
  if ((g_doc_tab == 1 || g_doc_tab == 2) && rx >= mx && rx < rx_panel &&
      ry >= body_y + 26 && ry < body_y + mid_h) {
    int32_t doc_y = body_y + 26, doc_h = mid_h - 30;
    int32_t sb = 8;
    if (rx >= mx + mw - sb - 8) {
      int32_t rel = ry - (doc_y + 6);
      int32_t track = doc_h - sb - 12;
      if (track > 0 && g_code_total_rows > 0)
        g_scroll_y = (rel * g_code_total_rows) / track;
      return;
    }
    if (ry >= doc_y + doc_h - sb - 8) {
      int32_t rel = rx - (mx + 42);
      int32_t track = mw - 42 - sb - 8;
      if (track > 0 && g_code_max_cols > 0)
        g_scroll_x = (rel * g_code_max_cols) / track;
      return;
    }
  }

  /* Canvas Design: Click + Doppio Click */
  if (g_doc_tab == 0 && rx >= mx && rx < rx_panel && ry >= body_y + 26 &&
      ry < body_y + mid_h) {
    int32_t doc_y = body_y + 26, doc_h = mid_h - 26;
    int32_t fx = mx + (mw - 420) / 2;
    int32_t fy = doc_y + (doc_h - 280) / 2;
    for (int i = g_ctrl_count - 1; i >= 0; i--) {
      const st_ctrl_t *c = &g_ctrls[i];
      if (rx >= fx + c->x && rx < fx + c->x + c->w && ry >= fy + c->y &&
          ry < fy + c->y + c->h) {
        uint64_t now =
            (g_api && g_api->timer_get_ticks) ? g_api->timer_get_ticks() : 0;
        if (g_last_click_idx == i && (now - g_last_click_tick) < 100) {
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

  if (g_doc_tab == 2 && rx >= mx && rx < rx_panel && ry >= body_y + 26 &&
      ry < body_y + mid_h) {
    g_edit_code = true;
    g_edit_prop_text = false;
  }
}

static void studio_key(window_t *win, char c) {
  (void)win;
  if (g_show_proj_mgr && g_edit_proj_name) {
    size_t len = 0;
    while (g_new_proj_buf[len])
      len++;
    if (c == '\n' || c == '\r') {
      g_edit_proj_name = false;
      st_new_project();
      return;
    }
    if (c == 0x1B) {
      g_edit_proj_name = false;
      return;
    }
    if (c == '\b') {
      if (len > 0)
        g_new_proj_buf[len - 1] = '\0';
      return;
    }
    if (((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '_' || c == '-') &&
        len < sizeof(g_new_proj_buf) - 2) {
      g_new_proj_buf[len] = c;
      g_new_proj_buf[len + 1] = '\0';
    }
    return;
  }
  if (g_edit_prop_text && g_sel_idx >= 0 && g_sel_idx < g_ctrl_count) {
    st_ctrl_t *s = &g_ctrls[g_sel_idx];
    size_t len = 0;
    while (s->text[len])
      len++;
    if (c == '\n' || c == '\r' || c == 0x1B) {
      g_edit_prop_text = false;
      return;
    }
    if (c == '\b') {
      if (len > 0)
        s->text[len - 1] = '\0';
      return;
    }
    if (c >= 32 && c < 127 && len < 46) {
      s->text[len] = c;
      s->text[len + 1] = '\0';
      g_proj_dirty = true;
    }
    return;
  }

  if (g_doc_tab == 2 && g_edit_code) {
    size_t len = 0;
    while (g_c_code[len])
      len++;
    if (c == '\b') {
      if (len > 0) {
        g_c_code[len - 1] = '\0';
        g_proj_dirty = true;
      }
      return;
    }
    if (c == '\r')
      c = '\n';
    if ((c == '\n' || (c >= 32 && c < 127)) && len < sizeof(g_c_code) - 2) {
      g_c_code[len] = c;
      g_c_code[len + 1] = '\0';
      g_proj_dirty = true;
    }
  }
}

static void studio_open(void) {
  if (!g_api)
    return;
  if (!g_rt)
    g_rt = nsrt_load_library(g_api);
  if (g_ctrl_count == 0) {
    char last_proj[32];
    bool loaded = false;
    if (g_rt && g_rt->reg_read_sz &&
        g_rt->reg_read_sz("Software\\NewOSStudio", "LastProject", last_proj,
                          sizeof(last_proj)) &&
        last_proj[0]) {
      loaded = st_load_project_by_name(last_proj);
    }
    if (!loaded && !st_load_project_by_name("NewOSApp")) {
      st_init_template();
      st_save_project();
    }
  }

  uint32_t sw = g_api->gfx_get_width ? g_api->gfx_get_width() : 1024;
  uint32_t sh = g_api->gfx_get_height ? g_api->gfx_get_height() : 768;
  int32_t w = (int32_t)sw;
  int32_t h = (int32_t)sh - 36; /* Schermo intero sopra la Taskbar */

  g_studio_win = (window_t *)g_api->wm_create_window(
      "NewOS Studio - Visual XUIML & SDK IDE", 0, 0, w, h, APP_ABOUT);
  if (!g_studio_win)
    return;
  g_studio_win->on_draw = studio_draw;
  g_studio_win->on_mouse = studio_mouse;
  g_studio_win->on_key = studio_key;
}

static const studio_api_t g_studio_api = {.magic = STUDIO_MAGIC,
                                          .open = studio_open};

__attribute__((section(".entry"), used)) const studio_api_t *
studio_dll_entry(const kernel_api_t *api) {
  studio_apply_relocs();
  g_api = api;
  return &g_studio_api;
}
