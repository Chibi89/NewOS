#ifndef FS_VFS_MOUNT_H
#define FS_VFS_MOUNT_H

#include <types.h>
#include <fs/vfs.h>

#define VFS_MAX_DRIVES       16
#define VFS_MAX_FS_DRIVERS   8
#define VFS_FS_NAME_MAX      16

// Operazioni per Seek
#define VFS_SEEK_SET 0
#define VFS_SEEK_CUR 1
#define VFS_SEEK_END 2

struct vfs_mount;
struct vfs_driver;

// Struttura puntatori a funzione per driver di File System
typedef struct vfs_driver {
    char name[VFS_FS_NAME_MAX]; // es. "exFAT", "ISO9660", "RamFS"

    bool     (*mount)(struct vfs_mount* mnt);
    void     (*unmount)(struct vfs_mount* mnt);

    bool     (*open)(struct vfs_mount* mnt, const char* rel_path, vfs_file_t* file);
    int32_t  (*read)(struct vfs_mount* mnt, vfs_file_t* file, void* buf, uint32_t count);
    int32_t  (*write)(struct vfs_mount* mnt, vfs_file_t* file, const void* buf, uint32_t count);
    void     (*close)(struct vfs_mount* mnt, vfs_file_t* file);
    int32_t  (*seek)(struct vfs_mount* mnt, vfs_file_t* file, int32_t offset, int whence);

    int32_t  (*readdir)(struct vfs_mount* mnt, const char* rel_path, vfs_dirent_t* entries, int32_t max_entries);
    bool     (*create)(struct vfs_mount* mnt, const char* rel_path, uint16_t attributes);
    bool     (*mkdir)(struct vfs_mount* mnt, const char* rel_path);
    bool     (*unlink)(struct vfs_mount* mnt, const char* rel_path);
    bool     (*rename)(struct vfs_mount* mnt, const char* old_path, const char* new_path);
    bool     (*stat)(struct vfs_mount* mnt, const char* rel_path, vfs_dirent_t* out_stat);
} vfs_driver_t;

// Punto di montaggio (Unità logica)
typedef struct vfs_mount {
    char                drive_letter;   // 'A', 'C', 'D', 'R', ecc.
    bool                is_mounted;
    bool                read_only;
    char                label[32];
    uint32_t            total_mb;
    uint32_t            free_mb;
    uint32_t            device_id;      // Indice periferica (ATA master = 0, slave = 1, ecc.)
    uint64_t            partition_lba;  // LBA di partenza sul disco (64-bit per GPT)
    const vfs_driver_t* driver;         // Driver assegnato dinamicamente
    void*               priv_data;      // Dati specifici del driver montato
} vfs_mount_t;

// Informazioni su una specifica unità per Explorer e UI
typedef struct {
    char     drive_letter;
    char     fs_name[16];
    char     label[32];
    bool     read_only;
    uint32_t total_mb;
    uint32_t free_mb;
} vfs_drive_info_t;

// Inizializza il sottosistema di montaggio dinamico
void vfs_mount_init(void);

// Registra un nuovo driver di file system
bool vfs_register_driver(const vfs_driver_t* driver);

// Cerca un driver per nome
const vfs_driver_t* vfs_find_driver(const char* name);

// Monta un'unità con associazione dinamica del driver
bool vfs_mount_drive(char drive_letter, const char* fs_name, const char* label, uint32_t dev_id, uint64_t lba, bool read_only, void* priv);

// Smonta un'unità
bool vfs_unmount_drive(char drive_letter);

// Risolve la lettera di unità e restituisce il mount point e il percorso relativo
vfs_mount_t* vfs_resolve_path_mount(const char* path, const char** out_rel_path);

// Restituisce informazioni su un'unità
bool vfs_get_drive_info(char drive_letter, vfs_drive_info_t* out_info);

// Controlla se una specifica lettera di unità è montata
bool vfs_is_drive_mounted(char drive_letter);

// Trova la prima lettera di unità libera non allocata
char vfs_get_first_free_letter(void);

// Elenco lettere unità attive (es. "CDR")
int vfs_list_drives(char* out_letters, int max_drives);

#endif // FS_VFS_MOUNT_H
