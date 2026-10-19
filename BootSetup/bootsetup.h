#ifndef BOOTSETUP_H
#define BOOTSETUP_H

#include <screen_modules.h>
#include "aes256.h"

// Colori UI stile Classic Windows 2000
#define COLOR_BTNFACE              0xD4D0C8
#define COLOR_WINDOW               0xFFFFFF
#define COLOR_WINDOWTEXT           0x000000
#define COLOR_ACTIVECAPTION_LEFT   0x0A246A
#define COLOR_ACTIVECAPTION_RIGHT  0xA6CAF0
#define COLOR_CAPTIONTEXT          0xFFFFFF
#define COLOR_BTNSHADOW            0x808080
#define COLOR_BTNHIGHLIGHT         0xFFFFFF

// ID Icone di sistema (sincronizzate con kernel/gui/icons.h)
typedef enum {
    ICON_THIS_PC             = 0,
    ICON_CESTINO             = 1,
    ICON_CESTINO_PIENO       = 2,
    ICON_FOLDER              = 3,
    ICON_FOLDER_VOID         = 4,
    ICON_DOCUMENT            = 5,
    ICON_CONTROL_PANEL       = 6,
    ICON_SOUND               = 7,
    ICON_RUN                 = 8,
    ICON_SEARCH              = 9,
    ICON_DESKTOP             = 10,
    ICON_HD                  = 11,
    ICON_FLOPPY              = 12,
    ICON_CD                  = 13,
    ICON_NET                 = 14,
    ICON_INFO                = 15,
    ICON_KEYBOARD            = 16,
    ICON_MOUSE               = 17,
    ICON_REGISTRY            = 18,
    ICON_ACCESSIBILITY       = 19,
    ICON_DATABASE            = 20,
    ICON_FONT                = 21,
    ICON_IMAGE               = 22,
    ICON_IMAGE_PNG           = 23,
    ICON_PLAY                = 24,
    ICON_ENERGY              = 25,
    ICON_MAINTENANCE         = 26,
    ICON_NET_ON              = 27,
    ICON_NET_OFF             = 28
} bootsetup_icon_id_t;

// Magic del contenitore cifrato NewOS Package V1.0
#define PAK_MAGIC_STRING "NOSPAK10"

// Header a 64 byte del pacchetto cifrato SYSTEM.PAK
typedef struct __attribute__((packed)) {
    char     magic[8];            // "NOSPAK10"
    uint32_t header_size;         // 64
    uint32_t total_files;         // Numero di file contenuti
    uint32_t total_uncompressed;  // Dimensione payload decifrato in byte
    uint32_t encrypted_size;      // Dimensione payload cifrato (multiplo di 16)
    uint8_t  aes_iv[16];          // Vettore di inizializzazione crittografico 128-bit
    uint32_t payload_crc32;       // Checksum CRC32 del payload cifrato
    uint8_t  reserved[20];        // Riservato / padding a 0
} pak_header_t;

// Record di ciascun file all'interno del payload decifrato:
// uint16_t path_len;
// char     path[path_len]; (relativo, es: "NewOS/Driver/VGA.SYS")
// uint32_t file_size;
// uint32_t file_crc32;
// uint8_t  data[file_size];

// Entry point ufficiale del Modulo Installer BootSetup
int bootsetup_main(const kernel_api_t* api, const bootsetup_params_t* params);

#endif // BOOTSETUP_H
