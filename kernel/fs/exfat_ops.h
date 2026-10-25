#ifndef FS_EXFAT_OPS_H
#define FS_EXFAT_OPS_H

#include <types.h>
#include <fs/exfat.h>
#include <fs/vfs_mount.h>

// Inizializza il modulo operativo avanzato exFAT
void exfat_ops_init(void);

// Restituisce il driver VFS per exFAT
const vfs_driver_t* exfat_get_vfs_driver(void);

// Funzioni operative avanzate per allocazione cluster e modifica directory
uint32_t exfat_ops_allocate_cluster(void);
bool     exfat_ops_link_clusters(uint32_t prev_cluster, uint32_t next_cluster);
bool     exfat_ops_create_entry(uint32_t dir_cluster, const char* name, uint32_t first_cluster, uint32_t initial_size, uint16_t attributes);
bool     exfat_ops_delete_entry(uint32_t dir_cluster, const char* name);
bool     exfat_ops_update_file_entry(uint32_t dir_cluster, const char* name, uint32_t new_size, uint32_t first_cluster, bool no_fat_chain);
bool     exfat_ops_update_file_size(uint32_t dir_cluster, const char* name, uint32_t new_size);

#endif // FS_EXFAT_OPS_H
