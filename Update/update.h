#ifndef UPDATE_H
#define UPDATE_H

#include <screen_modules.h>
#include "../BootSetup/aes256.h"

// Colori UI stile Classic NewOS 32bpp
#define COLOR_DESKTOP_BG           0x004E98
#define COLOR_BTNFACE              0xD4D0C8
#define COLOR_WINDOW               0xFFFFFF
#define COLOR_WINDOWTEXT           0x000000
#define COLOR_ACTIVECAPTION_LEFT   0x0A246A
#define COLOR_ACTIVECAPTION_RIGHT  0xA6CAF0
#define COLOR_CAPTIONTEXT          0xFFFFFF
#define COLOR_BTNSHADOW            0x808080
#define COLOR_BTNHIGHLIGHT         0xFFFFFF
#define COLOR_PROGRESS_BAR         0x002FA7
#define COLOR_PROGRESS_BG          0xE0E0E0
#define COLOR_SUCCESS_GREEN        0x008000
#define COLOR_ALERT_RED            0xC00000

// Magic Header archivio protetto v2 (Encrypt-then-MAC)
#define UPDATE_PAK_MAGIC "NOSPAK20"

// Header a 96 byte del pacchetto cifrato UPDATE.PAK (v2.0)
typedef struct __attribute__((packed)) {
    char     magic[8];            // "NOSPAK20"
    uint32_t header_size;         // 96
    uint32_t total_files;         // Numero di file contenuti
    uint32_t total_uncompressed;  // Dimensione payload decifrato in byte
    uint32_t encrypted_size;      // Dimensione payload cifrato (multiplo di 16)
    uint8_t  aes_iv[16];          // Vettore di inizializzazione crittografico 128-bit
    uint8_t  hmac_sha256[32];     // AuthTag HMAC-SHA256 (Verify-Before-Decrypt)
    uint32_t payload_crc32;       // Checksum CRC32 del ciphertext
    uint32_t flags;               // Bit 0 = HMAC, Bit 1 = Double-Buffered
    uint8_t  reserved[16];        // Riservato
} update_pak_header_t;

// Record per ciascun file nel payload decifrato
typedef struct __attribute__((packed)) {
    uint16_t path_len;
    // char path[path_len];
    // uint32_t size;
    // uint32_t crc32;
    // uint8_t  sha256[32];
    // uint8_t  data[size];
} update_file_record_t;

// Stati della transazione Micro-Journaling (transact.jrn)
typedef enum {
    STAGE_NONE = 0,
    STAGE_VERIFIED,
    STAGE_BACKUP_DONE,
    STAGE_NEW_WRITTEN,
    STAGE_COMMITTED,
    STAGE_REGISTRY_PATCHING,
    STAGE_ALL_DONE
} transaction_stage_t;

// Tipo modulo per UPDATE.MOD
#ifndef MODULE_TYPE_UPDATE
#define MODULE_TYPE_UPDATE 6
#endif

// I tipi update_params_t, update_result_t e update_entry_fn sono ereditati da screen_modules.h

#endif // UPDATE_H
