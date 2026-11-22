#ifndef FS_VFS_H
#define FS_VFS_H

#include <types.h>

#define VFS_MAX_PATH     256
#define VFS_MAX_FILENAME 128

#define VFS_ATTR_READONLY  0x01
#define VFS_ATTR_HIDDEN    0x02
#define VFS_ATTR_SYSTEM    0x04
#define VFS_ATTR_DIRECTORY 0x10
#define VFS_ATTR_ARCHIVE   0x20

#define VFS_SEEK_SET 0
#define VFS_SEEK_CUR 1
#define VFS_SEEK_END 2

typedef struct {
    char     name[VFS_MAX_FILENAME];
    uint64_t size;
    uint16_t attributes;
    bool     is_directory;
} vfs_dirent_t;

// Nomenclatura standard del bus fisico sottostante un'unita' montata
typedef enum {
    VFS_BUS_UNKNOWN   = 0,
    VFS_BUS_SATA_AHCI = 1,
    VFS_BUS_ATA_IDE   = 2,
    VFS_BUS_ATAPI_CD  = 3,
    VFS_BUS_USB_XHCI  = 4,
    VFS_BUS_USB_EHCI  = 5,
    VFS_BUS_USB_UHCI  = 6,
    VFS_BUS_FDC       = 7,
    VFS_BUS_RAMFS     = 8,
    VFS_BUS_NET_ETH   = 9,
    VFS_BUS_NVME      = 10
} vfs_bus_type_t;

typedef struct {
    uint8_t  drive_letter;
    uint8_t  bus_type;       // vfs_bus_type_t
    uint16_t reserved;
    uint32_t bus_max_kbps;   // Banda nominale max del bus in KB/s
    char     controller[32]; // Es. "SATA AHCI", "USB xHCI", "ATAPI", "RAMFS"
} vfs_bus_info_t;

typedef struct {
    char     name[VFS_MAX_FILENAME];
    uint64_t size;
    uint32_t first_cluster;
    uint64_t current_pos;
    bool     is_directory;
    bool     no_fat_chain;
    bool     is_open;
    char     drive_letter;
    void*    driver_data;
    uint32_t dir_cluster;
} vfs_file_t;

// Inizializza il layer VFS, monta la partizione primaria exFAT C:, il CD-ROM D: e il RamFS R:
bool vfs_init(void);

// Apre un file dato il percorso assoluto o relativo (es. "C:\SYSTEM\ICONS.BIN", "D:\SETUP.EXE", "R:\TEMP\TEST.TXT", "/DOCS/LEGGIMI.TXT")
vfs_file_t* vfs_open(const char* path);

// Legge 'count' byte dal file corrente
int32_t vfs_read(vfs_file_t* file, void* buf, uint32_t count);

// Scrive 'count' byte sul file corrente
int32_t vfs_write(vfs_file_t* file, const void* buf, uint32_t count);

// Sposta il cursore di lettura/scrittura all'interno del file (supporto > 4GB)
int64_t vfs_seek(vfs_file_t* file, int64_t offset, int whence);

// Restituisce la posizione corrente del cursore
uint64_t vfs_tell(vfs_file_t* file);

// Chiude il file aperto
void vfs_close(vfs_file_t* file);

// Elenca gli elementi in una directory specificata dal percorso
int32_t vfs_readdir(const char* dir_path, vfs_dirent_t* entries, int32_t max_entries);

// Verifica se un file o cartella esiste
bool vfs_exists(const char* path);

// Restituisce la dimensione del file in byte (0 se non esiste, supporto > 4GB)
uint64_t vfs_file_size(const char* path);

// Crea un nuovo file vuoto
bool vfs_create(const char* path);

// Crea una nuova cartella
bool vfs_mkdir(const char* path);

// Rimuove un file o cartella
bool vfs_unlink(const char* path);

// Rinomina o sposta un file o cartella
bool vfs_rename(const char* old_path, const char* new_path);

// Utility: alloca memoria nell'heap e carica l'intero contenuto del file
void* vfs_read_entire_file(const char* path, uint32_t* out_size);

// Utility: scrive dati in-place su un file esistente su disco exFAT
int32_t vfs_write_entire_file_inplace(const char* path, const void* buf, uint32_t count);

// Controllo di protezione gerarchica del filesystem (WAF / Access Control)
bool vfs_is_protected(const char* path);
bool vfs_is_program_file_path(const char* path);

// Corrispondenza pattern con caratteri jolly (* e ?)
bool vfs_wildcard_match(const char* pattern, const char* str);

// Sincronizza tutti i buffer sporchi della cache del filesystem su supporto fisico
void vfs_flush(void);

#endif // FS_VFS_H
