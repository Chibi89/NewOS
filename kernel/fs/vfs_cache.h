#ifndef FS_VFS_CACHE_H
#define FS_VFS_CACHE_H

#include <types.h>

#define VFS_CACHE_SECTORS_MAX  64
#define VFS_DENTRY_CACHE_MAX   32

typedef struct {
    uint32_t dev_id;
    uint32_t lba;
    uint32_t age;
    bool     valid;
    bool     dirty;
    uint8_t  data[512];
} vfs_sector_cache_entry_t;

typedef struct {
    char     drive_letter;
    char     path[128];
    uint32_t cluster;
    uint32_t size;
    uint16_t attributes;
    bool     is_dir;
    bool     valid;
    uint32_t age;
} vfs_dentry_cache_entry_t;

// Inizializza il sottosistema di cache VFS
void vfs_cache_init(void);

// Lettura e scrittura a livello di settore con cache LRU integrata
bool vfs_cache_read_sector(uint32_t dev_id, uint32_t lba, void* buffer);
bool vfs_cache_write_sector(uint32_t dev_id, uint32_t lba, const void* buffer);
void vfs_cache_flush(void);

// Ricerca e inserimento nella Dentry Path Cache
bool vfs_dentry_lookup(char drive_letter, const char* clean_path, uint32_t* out_cluster, uint32_t* out_size, uint16_t* out_attr, bool* out_is_dir);
void vfs_dentry_insert(char drive_letter, const char* clean_path, uint32_t cluster, uint32_t size, uint16_t attr, bool is_dir);
void vfs_dentry_invalidate(char drive_letter, const char* clean_path);

#endif // FS_VFS_CACHE_H
