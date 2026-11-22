#ifndef FS_EXT4_H
#define FS_EXT4_H

#include <types.h>
#include <fs/vfs_mount.h>

#define EXT4_SUPER_MAGIC 0xEF53

void ext4_init(void);
const vfs_driver_t* ext4_get_vfs_driver(void);

#endif // FS_EXT4_H
