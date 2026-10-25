#ifndef SCREEN_MODULES_H
#define SCREEN_MODULES_H

#include <types.h>
#include <drivers/mouse.h>
#include <user.h>

// Magic signature: 'N', 'O', 'S', 'M' (NewOS Secure Module) = 0x4D534F4E in little endian
#define MODULE_MAGIC_NEWOS      0x4D534F4E
#define MODULE_VERSION_CURRENT  0x00010000 // v1.0

#define MODULE_TYPE_BOOTSCREEN   1
#define MODULE_TYPE_LOGINSCREEN  2
#define MODULE_TYPE_LOGOUTSCREEN 3
#define MODULE_TYPE_BOOTSETUP    4
#define MODULE_TYPE_CODEC        5
#define MODULE_TYPE_UPDATE       6

#define SEC_FLAG_PROTECTED       0x01
#define SEC_FLAG_SYSTEM_SIGNED   0x02
#define SEC_FLAG_VERIFIED        0x04

#define MODULE_AUTH_TAG          "NEWSOFT"

// Header di protezione e validazione del modulo (64 byte esatti)
typedef struct __attribute__((packed)) {
    uint32_t magic;             // MODULE_MAGIC_NEWOS (0x4D534F4E)
    uint32_t header_size;       // 64 byte
    uint32_t module_version;    // MODULE_VERSION_CURRENT (0x00010000)
    uint32_t module_type;       // 1 = Bootscreen, 2 = Loginscreen, 3 = Logoutscreen
    uint32_t code_size;         // Dimensione codice/dati dopo l'header
    uint32_t entry_offset;      // Offset dell'entry point (0x40 = 64 byte)
    uint32_t checksum;          // Checksum CRC32 del payload
    uint32_t security_flags;    // Flag di sicurezza (SEC_FLAG_xxx)
    char     module_name[16];   // "BOOTSCREEN", "LOGINSCREEN", "LOGOUTSCREEN"
    char     signature[8];      // "NEWSOFT\0"
    uint8_t  reserved[8];       // Padding a 64 byte
} module_header_t;

#ifndef _IMAGE_T_DEFINED
#define _IMAGE_T_DEFINED
typedef struct image_s {
    int32_t   width;
    int32_t   height;
    uint32_t* pixels;
} image_t;
#endif

// Tabella di Export Protetta delle API del Kernel (Kernel ABI)
typedef struct {
    uint32_t api_version;

    // Video & Primitives Grafiche
    uint32_t (*gfx_get_width)(void);
    uint32_t (*gfx_get_height)(void);
    void     (*gfx_flip)(void);
    void     (*gfx_clear)(uint32_t color);
    void     (*gfx_fill_rect)(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
    void     (*gfx_draw_rect)(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
    void     (*gfx_draw_bevel_rect)(int32_t x, int32_t y, int32_t w, int32_t h, bool sunken);
    void     (*gfx_draw_gradient_h)(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c1, uint32_t c2);
    void     (*gfx_draw_gradient_v)(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c1, uint32_t c2);
    void     (*gfx_draw_string)(int32_t x, int32_t y, const char* str, uint32_t color, uint32_t bg, bool transparent);
    void     (*gfx_draw_char)(int32_t x, int32_t y, char c, uint32_t color, uint32_t bg, bool transparent);
    void     (*gfx_draw_cursor)(int32_t x, int32_t y);
    void     (*gfx_draw_windows_logo)(int32_t x, int32_t y);
    void     (*gfx_draw_icon_32)(int32_t x, int32_t y, int icon_id);
    void     (*gfx_draw_icon_16)(int32_t x, int32_t y, int icon_id);
    void     (*gfx_draw_image)(int32_t x, int32_t y, const image_t* img);

    // Decodifica Immagini BMP/JPEG
    image_t* (*image_load_from_file)(const char* vfs_path);
    void     (*image_free)(image_t* img);

    // Input Mouse e Tastiera
    void     (*mouse_get_state)(mouse_state_t* state);
    bool     (*keyboard_has_char)(void);
    char     (*keyboard_get_char)(void);

    // Timer e Telemetria
    uint64_t (*timer_get_ticks)(void);
    void     (*timer_sleep)(uint32_t ms);
    void     (*serial_puts)(const char* str);

    // Profilo Utente e Autenticazione
    user_profile_t* (*user_get_current)(void);
    void     (*user_set_profile)(const char* username, const char* password, const char* comp_name, int theme, int power, bool installed);
    bool     (*user_save_config)(void);

    // Power Management
    void     (*power_shutdown)(void);
    void     (*power_reboot)(void);

    // Audio
    void     (*sound_play)(int sound_id);
    bool     (*sound_play_wav_file)(const char* path);

    // VFS e Memoria
    uint8_t* (*vfs_read_entire_file)(const char* path, uint32_t* out_size);
    int32_t  (*vfs_write_entire_file_inplace)(const char* path, const void* buf, uint32_t count);
    bool     (*vfs_exists)(const char* path);
    void*    (*kmalloc)(size_t size);
    void     (*kfree)(void* ptr);
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
typedef struct {
    const char* current_user;
} logoutscreen_params_t;

typedef enum {
    LOGOUT_ACTION_DISCONNECT = 0,
    LOGOUT_ACTION_SHUTDOWN = 1,
    LOGOUT_ACTION_REBOOT = 2,
    LOGOUT_ACTION_SUSPEND = 3,
    LOGOUT_ACTION_CANCEL = 4
} logoutscreen_result_t;

// Parametri e risultati BootSetup (Installer)
typedef struct {
    const char* package_path;
    bool auto_proceed_oobe;
} bootsetup_params_t;

typedef enum {
    BOOTSETUP_SUCCESS = 0,
    BOOTSETUP_CANCELLED = 1,
    BOOTSETUP_ERROR = -1
} bootsetup_result_t;

// Parametri e risultati Update Module (UPDATE.MOD)
typedef struct {
    const char* package_path;
    const char* reg_update_path;
    bool force_unattended;
} update_params_t;

typedef enum {
    UPDATE_SUCCESS = 0,
    UPDATE_ROLLBACK = 1,
    UPDATE_ERROR = -1
} update_result_t;

// Tipi di funzione per Entry Point dei moduli protetti
typedef int (*bootscreen_entry_fn)(const kernel_api_t* api, const bootscreen_params_t* params);
typedef int (*loginscreen_entry_fn)(const kernel_api_t* api, const loginscreen_params_t* params);
typedef int (*logoutscreen_entry_fn)(const kernel_api_t* api, const logoutscreen_params_t* params);
typedef int (*bootsetup_entry_fn)(const kernel_api_t* api, const bootsetup_params_t* params);
typedef int (*update_entry_fn)(const kernel_api_t* api, const update_params_t* params);

#endif // SCREEN_MODULES_H
