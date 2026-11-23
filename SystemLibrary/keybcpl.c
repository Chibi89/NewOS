/**
 * SystemLibrary/keybcpl.c
 * Libreria Dinamica Out-of-Kernel KEYBCPL.DLL (Applet Proprieta' Tastiera -
 * KEYBOARD.CPL) Vista dichiarativa definita in
 * C:\NewOS\SystemLibrary\UI\keybcpl.xuiml (XUIML.DLL); questo modulo gestisce
 * esclusivamente la logica applicativa e di sistema. (C) Newspoint Software -
 * NewSoft Dynamic System Library
 */

#include "cpl_dll_ui.h"
#include "xuiml.h"
#include <kernel_ext_api.h>

#ifndef BUILDING_KERNEL_LOADER
__attribute__((section(".header"), used)) module_header_t g_keybcpl_header = {
    .magic = MODULE_MAGIC_NEWOS,
    .header_size = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type = MODULE_TYPE_SYSTEM_LIBRARY,
    .code_size = 0,
    .entry_offset = 64,
    .checksum = 0,
    .security_flags =
        SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name = "KEYBCPL",
    .signature = MODULE_AUTH_TAG};
#endif

static const kernel_api_t *g_api = NULL;

typedef struct {
  const xuiml_api_t *xapi;
  xuiml_doc_t *doc;
} keybcpl_state_t;

static int k_str_eq(const char *a, const char *b) {
  if (!a || !b)
    return 0;
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return (*a == '\0' && *b == '\0');
}

static void save_keyboard_settings(keybcpl_state_t *st) {
  if (!g_api || !st || !st->xapi || !st->doc)
    return;
  int delay = st->xapi->get_int(st->doc, "sld_delay");
  int speed = st->xapi->get_int(st->doc, "sld_speed");
  int blink = st->xapi->get_int(st->doc, "sld_blink");
  int layout = st->xapi->get_int(st->doc, "lst_layout");

  const kernel_ext_api_t *ext = (const kernel_ext_api_t *)g_api->ext_api;
  if (ext && ext->keyboard_set_layout) {
    ext->keyboard_set_layout(layout);
  }
  if (g_api->registry_set_value_dword) {
    g_api->registry_set_value_dword(
        "HKEY_CURRENT_USER\\Control Panel\\Keyboard", "KeyboardDelay",
        (uint32_t)delay);
    g_api->registry_set_value_dword(
        "HKEY_CURRENT_USER\\Control Panel\\Keyboard", "KeyboardSpeed",
        (uint32_t)speed);
    g_api->registry_set_value_dword(
        "HKEY_CURRENT_USER\\Control Panel\\Keyboard", "CursorBlinkRate",
        (uint32_t)blink);
    g_api->registry_set_value_dword(
        "HKEY_CURRENT_USER\\Control Panel\\Keyboard", "Layout",
        (uint32_t)layout);
    if (g_api->registry_save_to_disk)
      g_api->registry_save_to_disk();
  }
  st->xapi->set_text(st->doc, "lbl_status",
                     "Parametri e layout tastiera salvati!");
}

static void keybcpl_draw(window_t *win) {
  keybcpl_state_t *st = (keybcpl_state_t *)win->app_data;
  if (!st || !st->xapi || !st->doc)
    return;
  st->xapi->render(st->doc, win);
}

static void keybcpl_mouse(window_t *win, int32_t lx, int32_t ly, bool clicked) {
  keybcpl_state_t *st = (keybcpl_state_t *)win->app_data;
  if (!st || !st->xapi || !st->doc || !clicked)
    return;

  const char *id = st->xapi->mouse(st->doc, win, lx, ly, clicked);
  if (!id || !id[0])
    return;

  if (k_str_eq(id, "lst_hw")) {
    int row = st->xapi->get_int(st->doc, "lst_hw");
    if (row == 0) {
      st->xapi->set_text(st->doc, "lbl_hw_mfg",
                         "Produttore: (Tastiere standard x86_64)");
      st->xapi->set_text(
          st->doc, "lbl_hw_loc",
          "Ubicazione: Controller i8042 (Porte 0x60, 0x64 - IRQ 1)");
    } else {
      st->xapi->set_text(st->doc, "lbl_hw_mfg",
                         "Produttore: USB Implementers Forum (HID Class)");
      st->xapi->set_text(st->doc, "lbl_hw_loc",
                         "Ubicazione: Root Hub USB (UHCI / OHCI / xHCI)");
    }
  } else if (k_str_eq(id, "lst_layout")) {
    int layout = st->xapi->get_int(st->doc, "lst_layout");
    const kernel_ext_api_t *ext = (const kernel_ext_api_t *)g_api->ext_api;
    if (ext && ext->keyboard_set_layout)
      ext->keyboard_set_layout(layout);
    st->xapi->set_text(
        st->doc, "lbl_status",
        "Layout tastiera applicato (premi OK/Applica per salvare)");
  } else if (k_str_eq(id, "btn_apply")) {
    save_keyboard_settings(st);
  } else if (k_str_eq(id, "btn_ok")) {
    save_keyboard_settings(st);
    g_api->wm_destroy_window(win->id);
  } else if (k_str_eq(id, "btn_close")) {
    g_api->wm_destroy_window(win->id);
  }
}

static void keybcpl_key(window_t *win, char c) {
  keybcpl_state_t *st = (keybcpl_state_t *)win->app_data;
  if (!st || !st->xapi || !st->doc)
    return;
  st->xapi->key(st->doc, win, c);
}

__attribute__((section(".text.entry"), used)) int
keybcpl_dll_entry(const kernel_api_t *api, window_t *win) {
  if (!api || !win)
    return -1;
  g_api = api;

  const xuiml_api_t *xapi = xuiml_load_library(api);
  if (!xapi)
    return -1;

  xuiml_doc_t *doc = xapi->load("C:\\NewOS\\SystemLibrary\\UI\\keybcpl.xuiml");
  if (!doc)
    doc = xapi->load("/NewOS/SystemLibrary/UI/keybcpl.xuiml");
  if (!doc)
    return -1;

  keybcpl_state_t *st =
      (keybcpl_state_t *)g_api->kmalloc(sizeof(keybcpl_state_t));
  if (!st) {
    xapi->free(doc);
    return -1;
  }
  st->xapi = xapi;
  st->doc = doc;

  xapi->list_clear(doc, "lst_hw");
  xapi->list_add(doc, "lst_hw", "Tastiera standard PS/2 (Controller 8042)");
  xapi->list_add(doc, "lst_hw", "Tastiera USB HID (Driver xHCI/UHCI)");

  xapi->list_clear(doc, "lst_layout");
  xapi->list_add(doc, "lst_layout",
                 "US  - Inglese (Stati Uniti / QWERTY Standard)");
  xapi->list_add(doc, "lst_layout",
                 "IT  - Italiano (QWERTY, tasti accentati e simboli)");
  xapi->list_add(doc, "lst_layout",
                 "UK  - Inglese (Regno Unito / GBP e simboli UK)");
  xapi->list_add(doc, "lst_layout", "DE  - Tedesco (QWERTZ / Y-Z invertiti)");
  xapi->list_add(doc, "lst_layout",
                 "FR  - Francese (AZERTY / A-Q e W-Z invertiti)");

  const kernel_ext_api_t *ext = (const kernel_ext_api_t *)g_api->ext_api;
  if (ext && ext->keyboard_get_layout) {
    xapi->set_int(doc, "lst_layout", ext->keyboard_get_layout());
  }

  win->app_data = st;
  win->on_draw = keybcpl_draw;
  win->on_mouse = keybcpl_mouse;
  win->on_key = keybcpl_key;
  return 0;
}
