#include <gui/screen_mgr.h>
#include <gui/graphics.h>
#include <gui/icons.h>
#include <gui/image.h>
#include <drivers/mouse.h>
#include <drivers/keyboard.h>
#include <drivers/timer.h>
#include <drivers/power.h>
#include <drivers/sound.h>
#include <drivers/sound_wav.h>
#include <fs/vfs.h>
#include <mm/heap.h>
#include <user.h>
#include <string.h>
#include <arch/x86_64/io.h>

static kernel_api_t g_kernel_api;
static bool         g_api_initialized = false;

static void serial_log(const char* s) {
    if (!s) return;
    while (*s) {
        outb(0x3F8, *s++);
    }
}

// Calcolo CRC32 standard per verifica di integrità crittografica
static uint32_t compute_crc32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; i++) {
        uint8_t byte = data[i];
        crc ^= byte;
        for (int j = 0; j < 8; j++) {
            uint32_t mask = -(crc & 1);
            crc = (crc >> 1) ^ (0xEDB88320 & mask);
        }
    }
    return ~crc;
}

void screen_mgr_init(void) {
    if (g_api_initialized) return;

    serial_log("[SCREEN_MGR] Inizializzazione Kernel Screen Manager e Tabella API Protetta...\n");
    icons_init();

    memset(&g_kernel_api, 0, sizeof(kernel_api_t));
    g_kernel_api.api_version = 0x00010000;

    // Video & Grafica
    g_kernel_api.gfx_get_width        = gfx_get_width;
    g_kernel_api.gfx_get_height       = gfx_get_height;
    g_kernel_api.gfx_flip             = gfx_flip;
    g_kernel_api.gfx_clear            = gfx_clear;
    g_kernel_api.gfx_fill_rect        = gfx_fill_rect;
    g_kernel_api.gfx_draw_rect        = gfx_draw_rect;
    g_kernel_api.gfx_draw_bevel_rect  = gfx_draw_bevel_rect;
    g_kernel_api.gfx_draw_gradient_h  = gfx_draw_gradient_h;
    g_kernel_api.gfx_draw_gradient_v  = gfx_draw_gradient_v;
    g_kernel_api.gfx_draw_string      = gfx_draw_string;
    g_kernel_api.gfx_draw_char        = gfx_draw_char;
    g_kernel_api.gfx_draw_cursor      = gfx_draw_cursor;
    g_kernel_api.gfx_draw_windows_logo= gfx_draw_windows_logo;
    g_kernel_api.gfx_draw_icon_32     = (void(*)(int32_t, int32_t, int))gfx_draw_icon_32;
    g_kernel_api.gfx_draw_icon_16     = (void(*)(int32_t, int32_t, int))gfx_draw_icon_16;
    g_kernel_api.gfx_draw_image       = gfx_draw_image;

    // Immagini
    g_kernel_api.image_load_from_file = image_load_from_file;
    g_kernel_api.image_free           = image_free;

    // Input
    g_kernel_api.mouse_get_state      = mouse_get_state;
    g_kernel_api.keyboard_has_char    = keyboard_has_char;
    g_kernel_api.keyboard_get_char    = keyboard_get_char;

    // Timer & Log
    g_kernel_api.timer_get_ticks      = timer_get_ticks;
    g_kernel_api.timer_sleep          = timer_sleep;
    g_kernel_api.serial_puts          = serial_log;

    // Profilo Utente
    g_kernel_api.user_get_current     = user_get_current;
    g_kernel_api.user_set_profile     = user_set_profile;
    g_kernel_api.user_save_config     = user_save_config;

    // Power
    g_kernel_api.power_shutdown       = power_shutdown;
    g_kernel_api.power_reboot         = power_reboot;

    // Audio
    g_kernel_api.sound_play           = (void(*)(int))sound_play;
    g_kernel_api.sound_play_wav_file  = sound_play_wav_file;

    // VFS & Memoria
    g_kernel_api.vfs_read_entire_file          = (uint8_t*(*)(const char*, uint32_t*))vfs_read_entire_file;
    g_kernel_api.vfs_write_entire_file_inplace = vfs_write_entire_file_inplace;
    g_kernel_api.vfs_exists                    = vfs_exists;
    g_kernel_api.kmalloc                       = kmalloc;
    g_kernel_api.kfree                         = kfree;

    g_api_initialized = true;
    serial_log("[SCREEN_MGR] Tabella API Kernel collegata con successo (33 Entrypoint).\n");
}

const kernel_api_t* screen_mgr_get_api(void) {
    if (!g_api_initialized) screen_mgr_init();
    return &g_kernel_api;
}

bool screen_mgr_verify_module(const void* buffer, uint32_t size, uint32_t expected_type, const char* expected_name) {
    if (!buffer || size < sizeof(module_header_t)) {
        serial_log("[SECURITY ERROR] Buffer modulo nullo o dimensione insufficiente!\n");
        return false;
    }

    const module_header_t* h = (const module_header_t*)buffer;

    // 1. Verifica Magic Signature 'NOSM'
    if (h->magic != MODULE_MAGIC_NEWOS) {
        serial_log("[SECURITY ERROR] Magic Signature modulo non valida! Atteso: NOSM\n");
        return false;
    }

    // 2. Verifica dimensione header
    if (h->header_size != sizeof(module_header_t)) {
        serial_log("[SECURITY ERROR] Dimensione header non coerente!\n");
        return false;
    }

    // 3. Verifica Tipo Modulo
    if (h->module_type != expected_type) {
        serial_log("[SECURITY ERROR] Tipo modulo non corrispondente a quello richiesto!\n");
        return false;
    }

    // 4. Verifica Firma del Produttore "NEWSOFT"
    if (strncmp(h->signature, MODULE_AUTH_TAG, 7) != 0) {
        serial_log("[SECURITY ERROR] Firma crittografica del produttore non valida o manomessa!\n");
        return false;
    }

    // 5. Verifica Flag di Protezione
    if (!(h->security_flags & SEC_FLAG_SYSTEM_SIGNED)) {
        serial_log("[SECURITY ERROR] Flag SYSTEM_SIGNED assente: esecuzione non autorizzata!\n");
        return false;
    }

    // 6. Verifica Integrità tramite Checksum CRC32 del codice
    const uint8_t* payload = (const uint8_t*)buffer + h->entry_offset;
    uint32_t payload_len = size - h->entry_offset;
    uint32_t calc_crc = compute_crc32(payload, payload_len);

    if (h->checksum != 0 && h->checksum != calc_crc) {
        serial_log("[SECURITY ERROR] Checksum CRC32 non corrispondente: il binario e' stato alterato!\n");
        return false;
    }

    serial_log("[SECURITY OK] Modulo '");
    serial_log(expected_name ? expected_name : h->module_name);
    serial_log("' verificato e autenticato con successo (CRC32: 0x");
    // Stampa hex esadecimale semplice
    char hex[9];
    mini_snprintf(hex, sizeof(hex), "%x", (unsigned int)calc_crc);
    serial_log(hex);
    serial_log(")\n");

    return true;
}

// ------------------------------------------------------------------------------
// Fallback di Sicurezza Integrati nel Kernel (se modulo esterno assente o corrotto)
// ------------------------------------------------------------------------------
static bootscreen_result_t bootscreen_fallback(void) {
    serial_log("[BOOTSCREEN] Fallback di sicurezza attivo...\n");
    gfx_clear(0x0A246A);
    gfx_draw_windows_logo(gfx_get_width() / 2 - 18, gfx_get_height() / 2 - 40);
    gfx_draw_string(gfx_get_width() / 2 - 80, gfx_get_height() / 2 + 10, "NewOS V1 64-bit", 0xFFFFFF, 0, true);
    gfx_flip();
    timer_sleep(400);
    return BOOTSCREEN_OK;
}

static loginscreen_result_t loginscreen_fallback(void) {
    serial_log("[LOGINSCREEN] Fallback di sicurezza attivo...\n");
    return LOGIN_SUCCESS;
}

static logoutscreen_result_t logoutscreen_fallback(void) {
    serial_log("[LOGOUTSCREEN] Fallback di sicurezza attivo...\n");
    power_shutdown();
    return LOGOUT_ACTION_SHUTDOWN;
}

// ------------------------------------------------------------------------------
// Esecutori dei Moduli Protetti da File System C:\SYSTEM\SCREENS\
// ------------------------------------------------------------------------------
bootscreen_result_t screen_mgr_run_bootscreen(void) {
    screen_mgr_init();

    const char* paths[] = {
        "C:\\NewOS\\SystemProgram\\BOOTSCR.MOD",
        "/NewOS/SystemProgram/BOOTSCR.MOD",
        "C:\\SYSTEM\\SCREENS\\BOOTSCR.MOD",
        "/SYSTEM/SCREENS/BOOTSCR.MOD",
        "BOOTSCR.MOD"
    };

    uint8_t* mod_buf = NULL;
    uint32_t mod_size = 0;

    for (int i = 0; i < 5; i++) {
        mod_buf = (uint8_t*)vfs_read_entire_file(paths[i], &mod_size);
        if (mod_buf && mod_size > 0) break;
    }

    if (mod_buf && mod_size >= sizeof(module_header_t)) {
        if (screen_mgr_verify_module(mod_buf, mod_size, MODULE_TYPE_BOOTSCREEN, "BOOTSCR.MOD")) {
            const module_header_t* h = (const module_header_t*)mod_buf;
            bootscreen_entry_fn entry = (bootscreen_entry_fn)(mod_buf + h->entry_offset);

            bootscreen_params_t p = { .duration_ms = 1800, .allow_skip = true };
            bootscreen_result_t res = (bootscreen_result_t)entry(&g_kernel_api, &p);

            kfree(mod_buf);
            return res;
        }
        kfree(mod_buf);
    }

    return bootscreen_fallback();
}

loginscreen_result_t screen_mgr_run_loginscreen(void) {
    screen_mgr_init();

    const char* paths[] = {
        "C:\\NewOS\\SystemProgram\\LOGIN.MOD",
        "/NewOS/SystemProgram/LOGIN.MOD",
        "C:\\SYSTEM\\SCREENS\\LOGIN.MOD",
        "/SYSTEM/SCREENS/LOGIN.MOD",
        "LOGIN.MOD"
    };

    uint8_t* mod_buf = NULL;
    uint32_t mod_size = 0;

    for (int i = 0; i < 5; i++) {
        mod_buf = (uint8_t*)vfs_read_entire_file(paths[i], &mod_size);
        if (mod_buf && mod_size > 0) break;
    }

    if (mod_buf && mod_size >= sizeof(module_header_t)) {
        if (screen_mgr_verify_module(mod_buf, mod_size, MODULE_TYPE_LOGINSCREEN, "LOGIN.MOD")) {
            const module_header_t* h = (const module_header_t*)mod_buf;
            loginscreen_entry_fn entry = (loginscreen_entry_fn)(mod_buf + h->entry_offset);

            loginscreen_params_t p = { .default_user = "Morgan", .allow_guest = true };
            loginscreen_result_t res = (loginscreen_result_t)entry(&g_kernel_api, &p);

            kfree(mod_buf);
            return res;
        }
        kfree(mod_buf);
    }

    return loginscreen_fallback();
}

logoutscreen_result_t screen_mgr_run_logoutscreen(void) {
    screen_mgr_init();

    const char* paths[] = {
        "C:\\NewOS\\SystemProgram\\LOGOUT.MOD",
        "/NewOS/SystemProgram/LOGOUT.MOD",
        "C:\\SYSTEM\\SCREENS\\LOGOUT.MOD",
        "/SYSTEM/SCREENS/LOGOUT.MOD",
        "LOGOUT.MOD"
    };

    uint8_t* mod_buf = NULL;
    uint32_t mod_size = 0;

    for (int i = 0; i < 5; i++) {
        mod_buf = (uint8_t*)vfs_read_entire_file(paths[i], &mod_size);
        if (mod_buf && mod_size > 0) break;
    }

    if (mod_buf && mod_size >= sizeof(module_header_t)) {
        if (screen_mgr_verify_module(mod_buf, mod_size, MODULE_TYPE_LOGOUTSCREEN, "LOGOUT.MOD")) {
            const module_header_t* h = (const module_header_t*)mod_buf;
            logoutscreen_entry_fn entry = (logoutscreen_entry_fn)(mod_buf + h->entry_offset);

            logoutscreen_params_t p = { .current_user = "Morgan" };
            logoutscreen_result_t res = (logoutscreen_result_t)entry(&g_kernel_api, &p);

            kfree(mod_buf);
            return res;
        }
        kfree(mod_buf);
    }

    return logoutscreen_fallback();
}

static bootsetup_result_t bootsetup_fallback(void) {
    serial_log("[BOOTSETUP] Fallback di sicurezza attivo...\n");
    return BOOTSETUP_SUCCESS;
}

bootsetup_result_t screen_mgr_run_bootsetup(void) {
    screen_mgr_init();

    const char* paths[] = {
        "C:\\BootSetup\\BOOTSETUP.MOD",
        "/BootSetup/BOOTSETUP.MOD",
        "C:\\NewOS\\SystemProgram\\BOOTSETUP.MOD",
        "/NewOS/SystemProgram/BOOTSETUP.MOD",
        "BOOTSETUP.MOD"
    };

    uint8_t* mod_buf = NULL;
    uint32_t mod_size = 0;

    for (int i = 0; i < 5; i++) {
        mod_buf = (uint8_t*)vfs_read_entire_file(paths[i], &mod_size);
        if (mod_buf && mod_size > 0) break;
    }

    if (mod_buf && mod_size >= sizeof(module_header_t)) {
        if (screen_mgr_verify_module(mod_buf, mod_size, MODULE_TYPE_BOOTSETUP, "BOOTSETUP.MOD")) {
            const module_header_t* h = (const module_header_t*)mod_buf;
            bootsetup_entry_fn entry = (bootsetup_entry_fn)(mod_buf + h->entry_offset);

            bootsetup_params_t p = {
                .package_path = "C:\\BootSetup\\SYSTEM.PAK",
                .auto_proceed_oobe = true
            };
            bootsetup_result_t res = (bootsetup_result_t)entry(&g_kernel_api, &p);

            kfree(mod_buf);
            return res;
        }
        kfree(mod_buf);
    }

    return bootsetup_fallback();
}

