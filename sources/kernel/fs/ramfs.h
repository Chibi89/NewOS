#ifndef FS_RAMFS_H
#define FS_RAMFS_H

#include <types.h>
#include <fs/vfs.h>
#include <fs/vfs_mount.h>

#define RAMFS_MAX_CHILDREN 32
#define RAMFS_MAX_NODES    64

typedef struct ramfs_node {
    char                name[VFS_MAX_FILENAME];
    bool                is_directory;
    uint32_t            size;
    uint32_t            capacity;
    uint8_t*            data;
    struct ramfs_node*  parent;
    struct ramfs_node*  children[RAMFS_MAX_CHILDREN];
    int                 num_children;
} ramfs_node_t;

// Inizializza il file system in memoria RAM
void ramfs_init(void);

// Restituisce il driver VFS per RamFS
const vfs_driver_t* ramfs_get_vfs_driver(void);

#endif // FS_RAMFS_H
