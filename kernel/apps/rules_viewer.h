#ifndef APPS_RULES_VIEWER_H
#define APPS_RULES_VIEWER_H

#include <types.h>

#define NOR_MAGIC            "NOSR"
#define NOR_VERSION          0x00010000
#define NOR_HEADER_SIZE      128
#define NOR_FLAG_DESKTOP_ONLY 0x00000001

#pragma pack(push, 1)
typedef struct {
    char     magic[4];       // "NOSR" (NewOS System Rule)
    uint32_t version;        // 0x00010000 (v1.0)
    uint32_t header_size;    // 128 byte esatti
    uint32_t flags;          // 0x00000001 = NOR_FLAG_DESKTOP_ONLY
    char     rule_id[32];    // Es. "01_SYSTEM_HIERARCHY"
    char     category[32];   // Es. "Architettura & File System"
    char     severity[16];   // Es. "CRITICA", "ALTA", "SICUREZZA"
    char     auth_tag[16];   // "NEWSOFT-RULE-V1"
    uint32_t timestamp;      // Es. 20261018
    uint32_t payload_crc32;  // CRC32 del testo in chiaro
    uint32_t payload_size;   // Dimensione del payload in byte
    uint32_t reserved;       // 0
} nor_header_t;
#pragma pack(pop)

// Avvio del Visualizzatore Ufficiale delle Regole di Sistema NewOS (.NOR)
void app_rules_viewer_launch(void);

// Apertura diretta di uno specifico file di regola .NOR
void app_rules_viewer_open_file(const char* filepath);

#endif // APPS_RULES_VIEWER_H
