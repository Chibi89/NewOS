#ifndef SCREEN_MODULES_H
#define SCREEN_MODULES_H

#include <drivers/mouse.h>
#include <fs/vfs.h>
#include <types.h>
#include <user.h>


// Magic signature: 'N', 'O', 'S', 'M' (NewOS Secure Module) = 0x4D534F4E in
// little endian
#define MODULE_MAGIC_NEWOS 0x4D534F4E
#define MODULE_VERSION_CURRENT 0x00010000 // v1.0

#define MODULE_TYPE_BOOTSCREEN 1
#define MODULE_TYPE_LOGINSCREEN 2
#define MODULE_TYPE_LOGOUTSCREEN 3
#define MODULE_TYPE_BOOTSETUP 4
#define MODULE_TYPE_CODEC 5
#define MODULE_TYPE_UPDATE 6
#define MODULE_TYPE_SYSTEM_LIBRARY 7
#define MODULE_TYPE_OOBE 8
#define MODULE_TYPE_RECOVERY_BASIC 9
#define MODULE_TYPE_RECOVERY_ADV 10

#define SEC_FLAG_PROTECTED 0x01
#define SEC_FLAG_SYSTEM_SIGNED 0x02
#define SEC_FLAG_VERIFIED 0x04

#define MODULE_AUTH_TAG "NEWSOFT"

// Header di protezione e validazione del modulo (64 byte esatti)
typedef struct __attribute__((packed)) {
  uint32_t magic;          // MODULE_MAGIC_NEWOS (0x4D534F4E)
  uint32_t header_size;    // 64 byte
  uint32_t module_version; // MODULE_VERSION_CURRENT (0x00010000)
  uint32_t module_type;    // 1 = Bootscreen, 2 = Loginscreen, 3 = Logoutscreen
  uint32_t code_size;      // Dimensione codice/dati dopo l'header
  uint32_t entry_offset;   // Offset dell'entry point (0x40 = 64 byte)
  uint32_t checksum;       // Checksum CRC32 del payload
  uint32_t security_flags; // Flag di sicurezza (SEC_FLAG_xxx)
  char module_name[16];    // "BOOTSCREEN", "LOGINSCREEN", "LOGOUTSCREEN"
  char signature[8];       // "NEWSOFT\0"
  uint8_t reserved[8];     // Padding a 64 byte
} module_header_t;

#ifndef _IMAGE_T_DEFINED
#define _IMAGE_T_DEFINED
typedef struct image_s {
  int32_t width;
  int32_t height;
  uint32_t *pixels;
} image_t;
#endif

// Tabella di Export Protetta delle API del Kernel (Kernel ABI)
typedef struct {
  uint32_t api_version;

  // Video & Primitives Grafiche
  uint32_t (*gfx_get_width)(void);
  uint32_t (*gfx_get_height)(void);
  void (*gfx_flip)(void);
  void (*gfx_clear)(uint32_t color);
  void (*gfx_fill_rect)(int32_t x, int32_t y, int32_t w, int32_t h,
                        uint32_t color);
  void (*gfx_draw_rect)(int32_t x, int32_t y, int32_t w, int32_t h,
                        uint32_t color);
  void (*gfx_draw_bevel_rect)(int32_t x, int32_t y, int32_t w, int32_t h,
                              bool sunken);
  void (*gfx_draw_gradient_h)(int32_t x, int32_t y, int32_t w, int32_t h,
                              uint32_t c1, uint32_t c2);
  void (*gfx_draw_gradient_v)(int32_t x, int32_t y, int32_t w, int32_t h,
                              uint32_t c1, uint32_t c2);
  void (*gfx_draw_string)(int32_t x, int32_t y, const char *str, uint32_t color,
                          uint32_t bg, bool transparent);
  void (*gfx_draw_char)(int32_t x, int32_t y, char c, uint32_t color,
                        uint32_t bg, bool transparent);
  void (*gfx_draw_cursor)(int32_t x, int32_t y);
  void (*gfx_draw_windows_logo)(int32_t x, int32_t y);
  void (*gfx_draw_icon_32)(int32_t x, int32_t y, int icon_id);
  void (*gfx_draw_icon_16)(int32_t x, int32_t y, int icon_id);
  void (*gfx_draw_image)(int32_t x, int32_t y, const image_t *img);
  void (*gfx_draw_pixel)(int32_t x, int32_t y, uint32_t color);
  uint32_t (*gfx_get_pixel)(int32_t x, int32_t y);
  void (*gfx_fill_alpha)(int32_t x, int32_t y, int32_t w, int32_t h,
                         uint32_t color, uint8_t alpha);
  void (*gfx_fill_rounded_rect)(int32_t x, int32_t y, int32_t w, int32_t h,
                                int32_t radius, uint32_t color);
  void (*gfx_draw_rounded_rect)(int32_t x, int32_t y, int32_t w, int32_t h,
                                int32_t radius, uint32_t color);
  void (*gfx_draw_specular_rim)(int32_t x, int32_t y, int32_t w, int32_t h,
                                int32_t radius);
  void (*gfx_draw_drop_shadow)(int32_t x, int32_t y, int32_t w, int32_t h,
                               int32_t spread);

  // Decodifica Immagini BMP/JPEG
  image_t *(*image_load_from_file)(const char *vfs_path);
  void (*image_free)(image_t *img);

  // Input Mouse e Tastiera
  void (*mouse_get_state)(mouse_state_t *state);
  bool (*keyboard_has_char)(void);
  char (*keyboard_get_char)(void);

  // Timer e Telemetria
  uint64_t (*timer_get_ticks)(void);
  void (*timer_sleep)(uint32_t ms);
  void (*serial_puts)(const char *str);

  // Profilo Utente e Autenticazione
  user_profile_t *(*user_get_current)(void);
  void (*user_set_profile)(const char *username, const char *password,
                           const char *comp_name, int theme, int power,
                           bool installed);
  bool (*user_save_config)(void);

  // Power Management
  void (*power_shutdown)(void);
  void (*power_reboot)(void);

  // Audio
  void (*sound_play)(int sound_id);
  bool (*sound_play_wav_file)(const char *path);

  // VFS e Memoria
  uint8_t *(*vfs_read_entire_file)(const char *path, uint32_t *out_size);
  int32_t (*vfs_write_entire_file_inplace)(const char *path, const void *buf,
                                           uint32_t count);
  int32_t (*vfs_readdir)(const char *dir_path, void *entries,
                         int32_t max_entries);
  bool (*vfs_exists)(const char *path);
  bool (*vfs_create)(const char *path);
  void (*registry_set_read_only)(bool ro);
  bool (*registry_set_value_sz)(const char *path, const char *val_name, const char *new_val);
  bool (*registry_set_value_dword)(const char *path, const char *val_name, uint32_t new_val);
  bool (*registry_save_to_disk)(void);
  void *(*kmalloc)(size_t size);
  void (*kfree)(void *ptr);
  void (*wm_destroy_window)(int32_t id);
  void (*app_explorer_open_folder)(const char *folder_name);
  bool (*registry_is_read_only)(void);

  // Installazione fisica e gestione dischi
  bool (*disk_install_chunk)(uint32_t chunk_idx, uint32_t total_chunks,
                             char *out_status, uint32_t max_status);
  bool (*disk_get_info)(int index, char *out_model, uint32_t *out_size_mb,
                        bool *is_cd);

  // Rete e Diagnostica per Moduli
  void *(*net_nic_get_active)(void);
  void (*net_format_mac)(const uint8_t mac[6], char *buf, size_t buf_size);
  void (*net_format_ip)(uint32_t ip, char *buf, size_t buf_size);
  uint32_t (*net_parse_ip)(const char *ip_str);
  bool (*dhcp_discover)(void);
  void (*net_nic_poll)(void);
  bool (*icmp_ping_ip)(uint32_t ip, uint32_t count, void *stats, void *on_reply);

  // Estensioni Sistema, Orologio RTC, Migrazione Profilo e UI Temizzata per Applet .DLL
  void (*rtc_get_time)(void *out_rtc_time);
  bool (*user_create_or_migrate)(const char *new_username, const char *password);
  bool (*registry_get_value_sz)(const char *path, const char *val_name, char *out_buf, size_t max_len);
  bool (*registry_get_value_dword)(const char *path, const char *val_name, uint32_t *out_val);
  const void *(*wm_get_current_theme_props)(void);
  int (*wm_get_theme)(void);
  void (*wm_draw_themed_button)(int32_t x, int32_t y, int32_t w, int32_t h, const char *label, bool pressed);
  void (*wm_draw_themed_tab)(int32_t x, int32_t y, int32_t w, int32_t h, const char *label, bool active);
  void (*gfx_fill_rounded_alpha)(int32_t x, int32_t y, int32_t w, int32_t h, int32_t radius, uint32_t color, uint8_t alpha);

  // VFS Avanzato, Appunti Globali (Clipboard) e Socket TCP per COMDLG.DLL & FTP Stack
  bool (*vfs_unlink)(const char *path);
  bool (*vfs_rename)(const char *old_path, const char *new_path);
  bool (*vfs_mkdir)(const char *path);
  void (*clipboard_set_text)(const char *text);
  const char *(*clipboard_get_text)(void);
  bool (*clipboard_push_file)(const char *filepath, bool is_cut);
  int (*clipboard_get_file_count)(void);
  const char *(*clipboard_get_file)(int index, bool *out_is_cut);
  void (*clipboard_clear_files)(void);
  void *(*tcp_create_conn)(void);
  bool (*tcp_connect)(void *conn, uint32_t remote_ip, uint16_t remote_port);
  int (*tcp_send)(void *conn, const void *data, uint16_t len);
  int (*tcp_recv)(void *conn, void *buf, uint16_t max_len);
  void (*tcp_close)(void *conn);
  void (*tcp_destroy_conn)(void *conn);

  // Streaming VFS a blocchi e telemetria bus hardware reale per misurazione live
  void *(*vfs_open)(const char *path);
  int32_t (*vfs_read)(void *file, void *buf, uint32_t count);
  int32_t (*vfs_write)(void *file, const void *buf, uint32_t count);
  void (*vfs_close)(void *file);
  uint64_t (*vfs_file_size)(const char *path);
  void (*vfs_flush)(void);
  bool (*vfs_get_drive_bus_info)(char drive_letter, vfs_bus_info_t *out_info);

  // Creazione finestre autonome e lancio impostazioni per moduli UI Style (.DLL)
  void *(*wm_create_window)(const char *title, int32_t x, int32_t y, int32_t w, int32_t h, int app_type);
  void (*app_control_panel_open)(void);

  // Socket TCP/IP & DNS per moduli di rete/TLS/Web out-of-kernel (NETTLS.DLL)
  int (*net_socket)(int domain, int type, int protocol);
  int (*net_connect)(int sockfd, const void *addr);
  int (*net_send)(int sockfd, const void *buf, size_t len, int flags);
  int (*net_recv)(int sockfd, void *buf, size_t len, int flags);
  int (*net_close)(int sockfd);
  void (*net_poll)(void);
  uint32_t (*dns_resolve)(const char *hostname);

  // Caricamento moduli secondari, Font TrueType e HTTP/TLS per app out-of-kernel
  bool (*screen_mgr_verify_module)(const void *buffer, uint32_t size,
                                   uint32_t expected_type,
                                   const char *expected_name);
  bool (*fontmgr_get_ttf_glyph)(uint32_t codepoint, int pixel_height,
                                uint8_t *out_alpha, int max_bytes,
                                int *w, int *h, int *xoff, int *yoff,
                                int *advance);
  bool (*nettls_http_get)(const char *url, uint32_t max_body_bytes,
                          void *out_resp);
  void (*nettls_free_response)(void *resp);
  const void *ext_api;
} kernel_api_t;

// Parametri e risultati Bootscreen
typedef struct {
  int duration_ms;
  bool allow_skip;
} bootscreen_params_t;

typedef enum {
  BOOTSCREEN_OK = 0,
  BOOTSCREEN_SKIPPED = 1,
  BOOTSCREEN_ERROR = -1
} bootscreen_result_t;

// Parametri e risultati Loginscreen
typedef struct {
  char default_user[32];
  bool allow_guest;
} loginscreen_params_t;

typedef enum {
  LOGIN_SUCCESS = 0,
  LOGIN_GUEST = 1,
  LOGIN_SHUTDOWN = 2,
  LOGIN_REBOOT = 3,
  LOGIN_CANCELLED = 4
} loginscreen_result_t;

// Parametri e risultati Logoutscreen
typedef enum {
  LOGOUT_MODE_SHUTDOWN = 1,      /* Shutdown: salva, SafeCheck, SYSTEM.PAK, spegni */
  LOGOUT_MODE_CHANGE_USER = 2,   /* ChangeUser: lascia sessione attiva, apri Login */
  LOGOUT_MODE_CLOSE_SESSION = 3, /* CloseUserSession: chiudi sessione attiva, apri Login */
  LOGOUT_MODE_REBOOT = 4,        /* Reboot: salva, SafeCheck, SYSTEM.PAK, riavvia */
  LOGOUT_MODE_SUSPEND = 5        /* Suspend: standby schermo */
} logout_mode_t;

typedef struct {
  const char *current_user;
  logout_mode_t mode;
} logoutscreen_params_t;

typedef enum {
  LOGOUT_ACTION_DISCONNECT = 0,  /* CloseUserSession: chiude sessione utente attivo */
  LOGOUT_ACTION_SHUTDOWN = 1,    /* Shutdown: salva, verifica, ripara da SYSTEM.PAK, spegne */
  LOGOUT_ACTION_REBOOT = 2,
  LOGOUT_ACTION_SUSPEND = 3,
  LOGOUT_ACTION_CANCEL = 4,
  LOGOUT_ACTION_CHANGE_USER = 5  /* ChangeUser: lascia sessione attiva, login altro utente */
} logoutscreen_result_t;

// Parametri e risultati BootSetup (Installer)
typedef struct {
  const char *package_path;
  bool auto_proceed_oobe;
} bootsetup_params_t;

typedef enum {
  BOOTSETUP_SUCCESS = 0,
  BOOTSETUP_CANCELLED = 1,
  BOOTSETUP_ERROR = -1
} bootsetup_result_t;

// Parametri e risultati Update Module (UPDATE.MOD)
typedef struct {
  const char *package_path;
  const char *reg_update_path;
  bool force_unattended;
} update_params_t;

typedef enum {
  UPDATE_SUCCESS = 0,
  UPDATE_ROLLBACK = 1,
  UPDATE_ERROR = -1
} update_result_t;

// Tipi di funzione per Entry Point dei moduli protetti
typedef int (*bootscreen_entry_fn)(const kernel_api_t *api,
                                   const bootscreen_params_t *params);
typedef int (*loginscreen_entry_fn)(const kernel_api_t *api,
                                    const loginscreen_params_t *params);
typedef int (*logoutscreen_entry_fn)(const kernel_api_t *api,
                                     const logoutscreen_params_t *params);
typedef int (*bootsetup_entry_fn)(const kernel_api_t *api,
                                  const bootsetup_params_t *params);
typedef int (*update_entry_fn)(const kernel_api_t *api,
                               const update_params_t *params);

// Parametri e risultati OOBE (OOBE.MOD)
typedef struct {
  bool allow_skip;
} oobe_params_t;

typedef enum {
  OOBE_SUCCESS = 0,
  OOBE_SHUTDOWN = 1,
  OOBE_REBOOT = 2,
  OOBE_ERROR = -1
} oobe_result_t;

typedef int (*oobe_entry_fn)(const kernel_api_t *api,
                             const oobe_params_t *params);

// Parametri e risultati Recovery Modules (RECBASIC.MOD / RECADV.MOD)
typedef struct {
  int trigger_source; // 0 = BootMenu, 1 = BootIniAuto
  bool auto_execute;
} recovery_params_t;

typedef enum {
  RECOVERY_CONTINUE = 0,
  RECOVERY_REBOOT = 1,
  RECOVERY_SHUTDOWN = 2,
  RECOVERY_ERROR = -1
} recovery_result_t;

typedef int (*recovery_entry_fn)(const kernel_api_t *api,
                                 const recovery_params_t *params);

#endif // SCREEN_MODULES_H
