#include <fs/ext4.h>
#include <drivers/ata.h>
#include <mm/heap.h>
#include <string.h>

typedef struct __attribute__((packed)) {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count_lo;
    uint32_t s_r_blocks_count_lo;
    uint32_t s_free_blocks_count_lo;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_log_block_size;
    uint32_t s_log_cluster_size;
    uint32_t s_blocks_per_group;
    uint32_t s_clusters_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;
    uint32_t s_wtime;
    uint16_t s_mnt_count;
    uint16_t s_max_mnt_count;
    uint16_t s_magic;
    uint16_t s_state;
    uint16_t s_errors;
    uint16_t s_minor_rev_level;
    uint32_t s_lastcheck;
    uint32_t s_checkinterval;
    uint32_t s_creator_os;
    uint32_t s_rev_level;
    uint16_t s_def_resuid;
    uint16_t s_def_resgid;
    uint32_t s_first_ino;
    uint16_t s_inode_size;
    uint16_t s_block_group_nr;
    uint32_t s_feature_compat;
    uint32_t s_feature_incompat;
    uint32_t s_feature_ro_compat;
    uint8_t  s_uuid[16];
    char     s_volume_name[16];
    char     s_last_mounted[64];
    uint32_t s_algorithm_usage_bitmap;
    uint8_t  s_prealloc_blocks;
    uint8_t  s_prealloc_dir_blocks;
    uint16_t s_reserved_gdt_blocks;
    uint8_t  s_journal_uuid[16];
    uint32_t s_journal_inum;
    uint32_t s_journal_dev;
    uint32_t s_last_orphan;
    uint32_t s_hash_seed[4];
    uint8_t  s_def_hash_version;
    uint8_t  s_jnl_backup_type;
    uint16_t s_desc_size;
    uint32_t s_default_mount_opts;
    uint32_t s_first_meta_bg;
    uint32_t s_mkfs_time;
    uint32_t s_jnl_blocks[17];
    uint32_t s_blocks_count_hi;
    uint32_t s_r_blocks_count_hi;
    uint32_t s_free_blocks_count_hi;
} ext4_superblock_t;

typedef struct __attribute__((packed)) {
    uint32_t bg_block_bitmap_lo;
    uint32_t bg_inode_bitmap_lo;
    uint32_t bg_inode_table_lo;
    uint16_t bg_free_blocks_count_lo;
    uint16_t bg_free_inodes_count_lo;
    uint16_t bg_used_dirs_count_lo;
    uint16_t bg_flags;
    uint32_t bg_exclude_bitmap_lo;
    uint16_t bg_block_bitmap_csum_lo;
    uint16_t bg_inode_bitmap_csum_lo;
    uint16_t bg_itable_unused_lo;
    uint16_t bg_checksum;
    uint32_t bg_block_bitmap_hi;
    uint32_t bg_inode_bitmap_hi;
    uint32_t bg_inode_table_hi;
} ext4_group_desc_t;

typedef struct __attribute__((packed)) {
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size_lo;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_gid;
    uint16_t i_links_count;
    uint32_t i_blocks_lo;
    uint32_t i_flags;
    uint32_t i_osd1;
    uint32_t i_block[15];
    uint32_t i_generation;
    uint32_t i_file_acl_lo;
    uint32_t i_size_high;
} ext4_inode_t;

typedef struct __attribute__((packed)) {
    uint16_t eh_magic;      // 0xF30A
    uint16_t eh_entries;
    uint16_t eh_max;
    uint16_t eh_depth;
    uint32_t eh_generation;
} ext4_extent_header_t;

typedef struct __attribute__((packed)) {
    uint32_t ee_block;
    uint16_t ee_len;
    uint16_t ee_start_hi;
    uint32_t ee_start_lo;
} ext4_extent_t;

typedef struct __attribute__((packed)) {
    uint32_t ei_block;
    uint32_t ei_leaf_lo;
    uint16_t ei_leaf_hi;
    uint16_t ei_unused;
} ext4_extent_idx_t;

typedef struct __attribute__((packed)) {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    char     name[255];
} ext4_dir_entry_t;

typedef struct {
    const ata_device_t* dev;
    uint64_t part_lba;
    uint32_t block_size;
    uint32_t sectors_per_block;
    uint32_t inodes_per_group;
    uint32_t inode_size;
    uint32_t desc_size;
    uint32_t first_data_block;
    uint64_t total_blocks;
    uint64_t free_blocks;
} ext4_mount_state_t;

static bool ext4_read_block(const ext4_mount_state_t* st, uint64_t blk, void* buf) {
    if (!st || !st->dev || !buf) return false;
    uint64_t lba = st->part_lba + blk * (uint64_t)st->sectors_per_block;
    return ata_read_device_sectors(st->dev, lba, st->sectors_per_block, buf);
}

static bool ext4_read_inode(const ext4_mount_state_t* st, uint32_t ino, ext4_inode_t* out_ino) {
    if (!st || ino == 0 || !out_ino) return false;
    uint32_t grp = (ino - 1) / st->inodes_per_group;
    uint32_t idx = (ino - 1) % st->inodes_per_group;

    uint64_t gdt_blk = (st->block_size == 1024) ? 2ULL : 1ULL;
    uint64_t desc_byte = (uint64_t)grp * (uint64_t)st->desc_size;
    uint64_t desc_blk = gdt_blk + desc_byte / st->block_size;
    uint32_t desc_off = (uint32_t)(desc_byte % st->block_size);

    uint8_t* blk_buf = (uint8_t*)kmalloc(st->block_size);
    if (!blk_buf) return false;

    if (!ext4_read_block(st, desc_blk, blk_buf)) {
        kfree(blk_buf);
        return false;
    }

    const ext4_group_desc_t* gd = (const ext4_group_desc_t*)(blk_buf + desc_off);
    uint64_t itable_blk = (uint64_t)gd->bg_inode_table_lo;
    if (st->desc_size >= 64) {
        itable_blk |= ((uint64_t)gd->bg_inode_table_hi << 32);
    }

    uint64_t ino_byte = (uint64_t)idx * (uint64_t)st->inode_size;
    uint64_t ino_blk = itable_blk + ino_byte / st->block_size;
    uint32_t ino_off = (uint32_t)(ino_byte % st->block_size);

    bool ok = ext4_read_block(st, ino_blk, blk_buf);
    if (ok) {
        memcpy(out_ino, blk_buf + ino_off, sizeof(ext4_inode_t));
    }
    kfree(blk_buf);
    return ok;
}

static uint64_t ext4_extent_lookup_lblock(const ext4_mount_state_t* st, const ext4_extent_header_t* eh, uint32_t lblock) {
    if (!eh || eh->eh_magic != 0xF30A) return 0;
    if (eh->eh_depth == 0) {
        const ext4_extent_t* ext = (const ext4_extent_t*)((const uint8_t*)eh + sizeof(ext4_extent_header_t));
        for (uint16_t i = 0; i < eh->eh_entries; i++) {
            uint32_t len = ext[i].ee_len & 0x7FFFU; // Bit 15 = unwritten
            if (lblock >= ext[i].ee_block && lblock < ext[i].ee_block + len) {
                if (ext[i].ee_len & 0x8000U) return 0;
                uint64_t pstart = ((uint64_t)ext[i].ee_start_hi << 32) | (uint64_t)ext[i].ee_start_lo;
                return pstart + (uint64_t)(lblock - ext[i].ee_block);
            }
        }
        return 0;
    }

    const ext4_extent_idx_t* idx = (const ext4_extent_idx_t*)((const uint8_t*)eh + sizeof(ext4_extent_header_t));
    int chosen = -1;
    for (uint16_t i = 0; i < eh->eh_entries; i++) {
        if (lblock >= idx[i].ei_block) chosen = (int)i;
        else break;
    }
    if (chosen < 0) return 0;

    uint64_t child_blk = ((uint64_t)idx[chosen].ei_leaf_hi << 32) | (uint64_t)idx[chosen].ei_leaf_lo;
    uint8_t* blk_buf = (uint8_t*)kmalloc(st->block_size);
    if (!blk_buf) return 0;

    uint64_t res = 0;
    if (ext4_read_block(st, child_blk, blk_buf)) {
        res = ext4_extent_lookup_lblock(st, (const ext4_extent_header_t*)blk_buf, lblock);
    }
    kfree(blk_buf);
    return res;
}

static uint64_t ext4_inode_map_block(const ext4_mount_state_t* st, const ext4_inode_t* ino, uint32_t lblock) {
    if (!ino) return 0;
    if (ino->i_flags & 0x00080000U) { // EXT4_EXTENTS_FL
        return ext4_extent_lookup_lblock(st, (const ext4_extent_header_t*)ino->i_block, lblock);
    }
    if (lblock < 12) return (uint64_t)ino->i_block[lblock];
    uint32_t ptrs_per_blk = st->block_size / 4U;
    if (lblock < 12 + ptrs_per_blk && ino->i_block[12] != 0) {
        uint8_t* buf = (uint8_t*)kmalloc(st->block_size);
        if (!buf) return 0;
        uint64_t p = 0;
        if (ext4_read_block(st, ino->i_block[12], buf)) {
            p = ((const uint32_t*)buf)[lblock - 12];
        }
        kfree(buf);
        return p;
    }
    return 0;
}

static int32_t ext4_read_inode_data(const ext4_mount_state_t* st, const ext4_inode_t* ino, uint64_t offset, void* out_buf, uint32_t count) {
    uint64_t fsize = ((uint64_t)ino->i_size_high << 32) | (uint64_t)ino->i_size_lo;
    if (offset >= fsize || count == 0) return 0;
    if (offset + (uint64_t)count > fsize) count = (uint32_t)(fsize - offset);

    uint8_t* blk_buf = (uint8_t*)kmalloc(st->block_size);
    if (!blk_buf) return 0;

    uint32_t done = 0;
    uint8_t* dst = (uint8_t*)out_buf;
    while (done < count) {
        uint32_t lblock = (uint32_t)((offset + done) / st->block_size);
        uint32_t boff = (uint32_t)((offset + done) % st->block_size);
        uint32_t can = st->block_size - boff;
        uint32_t chunk = (count - done < can) ? (count - done) : can;

        uint64_t pblk = ext4_inode_map_block(st, ino, lblock);
        if (pblk == 0) {
            memset(dst + done, 0, chunk); // Sparse block
        } else {
            if (!ext4_read_block(st, pblk, blk_buf)) break;
            memcpy(dst + done, blk_buf + boff, chunk);
        }
        done += chunk;
    }
    kfree(blk_buf);
    return (int32_t)done;
}

static uint32_t ext4_resolve_path_inode(const ext4_mount_state_t* st, const char* rel_path, ext4_inode_t* out_ino) {
    uint32_t cur_ino = 2; // Root inode = 2
    ext4_inode_t ino;
    if (!ext4_read_inode(st, cur_ino, &ino)) return 0;
    if (!rel_path || !*rel_path) {
        if (out_ino) *out_ino = ino;
        return cur_ino;
    }

    char copy[VFS_MAX_PATH];
    strncpy(copy, rel_path, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';

    char* p = copy;
    while (*p) {
        while (*p == '/' || *p == '\\') p++;
        if (!*p) break;
        char* sep = p;
        while (*sep && *sep != '/' && *sep != '\\') sep++;
        if (*sep) *sep++ = '\0';

        uint64_t dsize = ino.i_size_lo;
        if (dsize > 65536) dsize = 65536;
        uint8_t* dbuf = (uint8_t*)kmalloc((size_t)dsize);
        if (!dbuf) return 0;

        int32_t rb = ext4_read_inode_data(st, &ino, 0, dbuf, (uint32_t)dsize);
        uint32_t found_ino = 0;
        uint32_t off = 0;
        while (off + 8 <= (uint32_t)rb) {
            const ext4_dir_entry_t* de = (const ext4_dir_entry_t*)(dbuf + off);
            if (de->rec_len < 8) break;
            if (de->inode != 0 && de->name_len > 0 && de->name_len < 255) {
                char nm[256];
                memcpy(nm, de->name, de->name_len);
                nm[de->name_len] = '\0';
                if (strcasecmp(nm, p) == 0) {
                    found_ino = de->inode;
                    break;
                }
            }
            off += de->rec_len;
        }
        kfree(dbuf);
        if (!found_ino || !ext4_read_inode(st, found_ino, &ino)) return 0;
        cur_ino = found_ino;
        p = sep;
    }
    if (out_ino) *out_ino = ino;
    return cur_ino;
}

static bool ext4_vfs_mount(vfs_mount_t* mnt) {
    if (!mnt || !mnt->priv_data) return false;
    const ata_device_t* dev = (const ata_device_t*)mnt->priv_data;
    uint8_t sb_buf[1024];
    if (!ata_read_device_sectors(dev, mnt->partition_lba + 2, 2, sb_buf)) return false;

    const ext4_superblock_t* sb = (const ext4_superblock_t*)sb_buf;
    if (sb->s_magic != EXT4_SUPER_MAGIC) return false;

    ext4_mount_state_t* st = (ext4_mount_state_t*)kmalloc(sizeof(ext4_mount_state_t));
    if (!st) return false;
    memset(st, 0, sizeof(ext4_mount_state_t));
    st->dev = dev;
    st->part_lba = mnt->partition_lba;
    st->block_size = 1024U << sb->s_log_block_size;
    if (st->block_size < 1024 || st->block_size > 65536) st->block_size = 4096;
    st->sectors_per_block = st->block_size / 512U;
    st->inodes_per_group = sb->s_inodes_per_group ? sb->s_inodes_per_group : 8192;
    st->inode_size = (sb->s_rev_level >= 1 && sb->s_inode_size >= 128) ? sb->s_inode_size : 128;
    st->desc_size = (sb->s_feature_incompat & 0x80U) && sb->s_desc_size >= 64 ? sb->s_desc_size : 32;
    st->first_data_block = sb->s_first_data_block;
    st->total_blocks = ((uint64_t)sb->s_blocks_count_hi << 32) | (uint64_t)sb->s_blocks_count_lo;
    st->free_blocks = ((uint64_t)sb->s_free_blocks_count_hi << 32) | (uint64_t)sb->s_free_blocks_count_lo;

    mnt->priv_data = st;
    mnt->read_only = true;
    mnt->total_bytes = st->total_blocks * (uint64_t)st->block_size;
    mnt->free_bytes = st->free_blocks * (uint64_t)st->block_size;
    mnt->total_mb = (uint32_t)(mnt->total_bytes / (1024ULL * 1024ULL));
    mnt->free_mb = (uint32_t)(mnt->free_bytes / (1024ULL * 1024ULL));
    return true;
}

static void ext4_vfs_unmount(vfs_mount_t* mnt) {
    if (mnt && mnt->priv_data) {
        kfree(mnt->priv_data);
        mnt->priv_data = NULL;
    }
}

static bool ext4_vfs_open(vfs_mount_t* mnt, const char* rel_path, vfs_file_t* file) {
    if (!mnt || !mnt->priv_data || !file) return false;
    const ext4_mount_state_t* st = (const ext4_mount_state_t*)mnt->priv_data;
    ext4_inode_t ino;
    uint32_t inum = ext4_resolve_path_inode(st, rel_path, &ino);
    if (!inum) return false;

    memset(file, 0, sizeof(vfs_file_t));
    const char* bn = strrchr(rel_path ? rel_path : "", '/');
    strncpy(file->name, bn ? (bn + 1) : (rel_path ? rel_path : "/"), sizeof(file->name) - 1);
    file->first_cluster = inum;
    file->size = ((uint64_t)ino.i_size_high << 32) | (uint64_t)ino.i_size_lo;
    file->is_directory = ((ino.i_mode & 0xF000U) == 0x4000U);
    file->is_open = true;
    file->drive_letter = mnt->drive_letter;
    file->driver_data = mnt;
    return true;
}

static int32_t ext4_vfs_read(vfs_mount_t* mnt, vfs_file_t* file, void* buf, uint32_t count) {
    if (!mnt || !mnt->priv_data || !file || !buf) return 0;
    const ext4_mount_state_t* st = (const ext4_mount_state_t*)mnt->priv_data;
    ext4_inode_t ino;
    if (!ext4_read_inode(st, file->first_cluster, &ino)) return 0;
    int32_t rb = ext4_read_inode_data(st, &ino, file->current_pos, buf, count);
    if (rb > 0) file->current_pos += (uint64_t)rb;
    return rb;
}

static int64_t ext4_vfs_seek(vfs_mount_t* mnt, vfs_file_t* file, int64_t offset, int whence) {
    (void)mnt;
    if (!file || !file->is_open) return -1;
    int64_t np = (int64_t)file->current_pos;
    if (whence == VFS_SEEK_SET) np = offset;
    else if (whence == VFS_SEEK_CUR) np += offset;
    else if (whence == VFS_SEEK_END) np = (int64_t)file->size + offset;
    if (np < 0) np = 0;
    if ((uint64_t)np > file->size) np = (int64_t)file->size;
    file->current_pos = (uint64_t)np;
    return np;
}

static int32_t ext4_vfs_readdir(vfs_mount_t* mnt, const char* rel_path, vfs_dirent_t* entries, int32_t max_entries) {
    if (!mnt || !mnt->priv_data || !entries || max_entries <= 0) return 0;
    const ext4_mount_state_t* st = (const ext4_mount_state_t*)mnt->priv_data;
    ext4_inode_t dino;
    uint32_t dinum = ext4_resolve_path_inode(st, rel_path, &dino);
    if (!dinum || (dino.i_mode & 0xF000U) != 0x4000U) return 0;

    uint64_t dsize = dino.i_size_lo;
    if (dsize > 65536) dsize = 65536;
    uint8_t* dbuf = (uint8_t*)kmalloc((size_t)dsize);
    if (!dbuf) return 0;

    int32_t rb = ext4_read_inode_data(st, &dino, 0, dbuf, (uint32_t)dsize);
    int32_t cnt = 0;
    uint32_t off = 0;
    while (off + 8 <= (uint32_t)rb && cnt < max_entries) {
        const ext4_dir_entry_t* de = (const ext4_dir_entry_t*)(dbuf + off);
        if (de->rec_len < 8) break;
        if (de->inode != 0 && de->name_len > 0 && de->name_len < 255) {
            char nm[256];
            memcpy(nm, de->name, de->name_len);
            nm[de->name_len] = '\0';
            if (strcmp(nm, ".") != 0 && strcmp(nm, "..") != 0) {
                strncpy(entries[cnt].name, nm, sizeof(entries[cnt].name) - 1);
                entries[cnt].name[sizeof(entries[cnt].name) - 1] = '\0';
                ext4_inode_t child;
                if (ext4_read_inode(st, de->inode, &child)) {
                    entries[cnt].size = ((uint64_t)child.i_size_high << 32) | (uint64_t)child.i_size_lo;
                    entries[cnt].is_directory = ((child.i_mode & 0xF000U) == 0x4000U);
                    entries[cnt].mtime = child.i_mtime;
                    entries[cnt].permissions = (uint16_t)(child.i_mode & 0x7U);
                } else {
                    entries[cnt].size = 0;
                    entries[cnt].is_directory = (de->file_type == 2);
                }
                entries[cnt].attributes = entries[cnt].is_directory ? VFS_ATTR_DIRECTORY : VFS_ATTR_ARCHIVE;
                cnt++;
            }
        }
        off += de->rec_len;
    }
    kfree(dbuf);
    return cnt;
}

static bool ext4_vfs_stat(vfs_mount_t* mnt, const char* rel_path, vfs_dirent_t* out_stat) {
    if (!out_stat) return false;
    vfs_file_t f;
    if (!ext4_vfs_open(mnt, rel_path, &f)) return false;
    strncpy(out_stat->name, f.name, sizeof(out_stat->name) - 1);
    out_stat->name[sizeof(out_stat->name) - 1] = '\0';
    out_stat->size = f.size;
    out_stat->is_directory = f.is_directory;
    out_stat->attributes = f.is_directory ? VFS_ATTR_DIRECTORY : VFS_ATTR_ARCHIVE;
    out_stat->permissions = VFS_PERM_READ | VFS_PERM_EXEC;
    out_stat->mtime = 0;
    return true;
}

static vfs_driver_t g_ext4_driver = {
    .name = "EXT4",
    .mount = ext4_vfs_mount,
    .unmount = ext4_vfs_unmount,
    .open = ext4_vfs_open,
    .read = ext4_vfs_read,
    .write = NULL,
    .close = NULL,
    .seek = ext4_vfs_seek,
    .readdir = ext4_vfs_readdir,
    .create = NULL,
    .mkdir = NULL,
    .unlink = NULL,
    .rename = NULL,
    .stat = ext4_vfs_stat,
    .truncate = NULL,
    .fsync = NULL
};

void ext4_init(void) {
    vfs_register_driver(&g_ext4_driver);
}

const vfs_driver_t* ext4_get_vfs_driver(void) {
    return &g_ext4_driver;
}
