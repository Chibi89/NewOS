#include <fs/ramfs.h>
#include <mm/heap.h>
#include <string.h>

static ramfs_node_t g_nodes_pool[RAMFS_MAX_NODES];
static int          g_nodes_count = 0;
static ramfs_node_t* g_root_node = NULL;

static ramfs_node_t* alloc_node(const char* name, bool is_dir, ramfs_node_t* parent) {
    if (g_nodes_count >= RAMFS_MAX_NODES) return NULL;
    ramfs_node_t* node = &g_nodes_pool[g_nodes_count++];
    memset(node, 0, sizeof(ramfs_node_t));
    strncpy(node->name, name, sizeof(node->name) - 1);
    node->is_directory = is_dir;
    node->parent = parent;

    if (parent && parent->num_children < RAMFS_MAX_CHILDREN) {
        parent->children[parent->num_children++] = node;
    }
    return node;
}

static ramfs_node_t* resolve_ramfs_node(const char* rel_path) {
    if (!g_root_node) return NULL;
    if (!rel_path || rel_path[0] == '\0') return g_root_node;

    char temp[VFS_MAX_PATH];
    strncpy(temp, rel_path, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';

    ramfs_node_t* cur = g_root_node;
    char* token = temp;
    char* next = NULL;

    while (token && *token) {
        next = strstr(token, "/");
        if (next) {
            *next = '\0';
            next++;
        }

        bool found = false;
        for (int i = 0; i < cur->num_children; i++) {
            if (strcasecmp(cur->children[i]->name, token) == 0) {
                cur = cur->children[i];
                found = true;
                break;
            }
        }

        if (!found) return NULL;
        token = next;
    }

    return cur;
}

static bool ramfs_vfs_mount(vfs_mount_t* mnt) {
    if (!mnt) return false;
    mnt->read_only = false;
    mnt->total_mb = 16;
    mnt->free_mb = 16;
    return true;
}

static bool ramfs_vfs_open(vfs_mount_t* mnt, const char* rel_path, vfs_file_t* file) {
    (void)mnt;
    if (!file) return false;

    ramfs_node_t* node = resolve_ramfs_node(rel_path);
    if (!node) return false;

    strncpy(file->name, node->name, sizeof(file->name) - 1);
    file->name[sizeof(file->name) - 1] = '\0';
    file->size = node->size;
    file->first_cluster = (uint32_t)(uintptr_t)node; // Memorizza puntatore al nodo
    file->current_pos = 0;
    file->is_directory = node->is_directory;
    file->no_fat_chain = true;
    file->is_open = true;
    return true;
}

static int32_t ramfs_vfs_read(vfs_mount_t* mnt, vfs_file_t* file, void* buf, uint32_t count) {
    (void)mnt;
    if (!file || !buf || count == 0) return 0;

    ramfs_node_t* node = (ramfs_node_t*)(uintptr_t)file->first_cluster;
    if (!node || node->is_directory || !node->data) return 0;

    if (file->current_pos >= node->size) return 0;
    if (file->current_pos + count > node->size) {
        count = node->size - file->current_pos;
    }

    memcpy(buf, node->data + file->current_pos, count);
    file->current_pos += count;
    return (int32_t)count;
}

static int32_t ramfs_vfs_write(vfs_mount_t* mnt, vfs_file_t* file, const void* buf, uint32_t count) {
    (void)mnt;
    if (!file || !buf || count == 0) return 0;

    ramfs_node_t* node = (ramfs_node_t*)(uintptr_t)file->first_cluster;
    if (!node || node->is_directory) return 0;

    uint32_t needed = file->current_pos + count;
    if (needed > node->capacity) {
        uint32_t new_cap = (needed < 512) ? 512 : (needed * 2);
        uint8_t* new_data = (uint8_t*)kmalloc(new_cap);
        if (!new_data) return 0;

        if (node->data && node->size > 0) {
            memcpy(new_data, node->data, node->size);
            kfree(node->data);
        }
        node->data = new_data;
        node->capacity = new_cap;
    }

    memcpy(node->data + file->current_pos, buf, count);
    file->current_pos += count;
    if (file->current_pos > node->size) {
        node->size = file->current_pos;
    }
    file->size = node->size;

    return (int32_t)count;
}

static void ramfs_vfs_close(vfs_mount_t* mnt, vfs_file_t* file) {
    (void)mnt;
    if (file) file->is_open = false;
}

static int32_t ramfs_vfs_seek(vfs_mount_t* mnt, vfs_file_t* file, int32_t offset, int whence) {
    (void)mnt;
    if (!file) return -1;

    int32_t new_pos = (int32_t)file->current_pos;
    if (whence == VFS_SEEK_SET) new_pos = offset;
    else if (whence == VFS_SEEK_CUR) new_pos += offset;
    else if (whence == VFS_SEEK_END) new_pos = (int32_t)file->size + offset;

    if (new_pos < 0) new_pos = 0;
    if ((uint32_t)new_pos > file->size) new_pos = (int32_t)file->size;

    file->current_pos = (uint32_t)new_pos;
    return new_pos;
}

static int32_t ramfs_vfs_readdir(vfs_mount_t* mnt, const char* rel_path, vfs_dirent_t* entries, int32_t max_entries) {
    (void)mnt;
    if (!entries || max_entries <= 0) return 0;

    ramfs_node_t* dir = resolve_ramfs_node(rel_path);
    if (!dir || !dir->is_directory) return 0;

    int count = 0;
    for (int i = 0; i < dir->num_children && count < max_entries; i++) {
        ramfs_node_t* child = dir->children[i];
        strncpy(entries[count].name, child->name, sizeof(entries[count].name) - 1);
        entries[count].name[sizeof(entries[count].name) - 1] = '\0';
        entries[count].size = child->size;
        entries[count].is_directory = child->is_directory;
        entries[count].attributes = child->is_directory ? VFS_ATTR_DIRECTORY : VFS_ATTR_ARCHIVE;
        count++;
    }
    return count;
}

static bool ramfs_vfs_create(vfs_mount_t* mnt, const char* rel_path, uint16_t attributes) {
    (void)mnt;
    (void)attributes;
    if (!rel_path || !*rel_path) return false;

    // Se esiste già, fallisce o ritorna true
    if (resolve_ramfs_node(rel_path)) return true;

    char dir_part[VFS_MAX_PATH] = "";
    const char* last_slash = strrchr(rel_path, '/');
    const char* fname = rel_path;

    if (last_slash) {
        size_t dlen = last_slash - rel_path;
        strncpy(dir_part, rel_path, dlen);
        dir_part[dlen] = '\0';
        fname = last_slash + 1;
    }

    ramfs_node_t* parent = resolve_ramfs_node(dir_part);
    if (!parent || !parent->is_directory) return false;

    ramfs_node_t* node = alloc_node(fname, false, parent);
    if (!node) return false;

    node->capacity = 512;
    node->data = (uint8_t*)kmalloc(node->capacity);
    node->size = 0;
    return true;
}

static bool ramfs_vfs_mkdir(vfs_mount_t* mnt, const char* rel_path) {
    (void)mnt;
    if (!rel_path || !*rel_path) return false;
    if (resolve_ramfs_node(rel_path)) return true;

    char dir_part[VFS_MAX_PATH] = "";
    const char* last_slash = strrchr(rel_path, '/');
    const char* dname = rel_path;

    if (last_slash) {
        size_t dlen = last_slash - rel_path;
        strncpy(dir_part, rel_path, dlen);
        dir_part[dlen] = '\0';
        dname = last_slash + 1;
    }

    ramfs_node_t* parent = resolve_ramfs_node(dir_part);
    if (!parent || !parent->is_directory) return false;

    return (alloc_node(dname, true, parent) != NULL);
}

static bool ramfs_vfs_unlink(vfs_mount_t* mnt, const char* rel_path) {
    (void)mnt;
    if (!rel_path || !*rel_path) return false;

    ramfs_node_t* node = resolve_ramfs_node(rel_path);
    if (!node || !node->parent) return false;

    ramfs_node_t* parent = node->parent;
    for (int i = 0; i < parent->num_children; i++) {
        if (parent->children[i] == node) {
            // Rimuovi child
            for (int k = i; k < parent->num_children - 1; k++) {
                parent->children[k] = parent->children[k + 1];
            }
            parent->num_children--;

            if (node->data) {
                kfree(node->data);
                node->data = NULL;
            }
            return true;
        }
    }
    return false;
}

static bool ramfs_vfs_stat(vfs_mount_t* mnt, const char* rel_path, vfs_dirent_t* out_stat) {
    (void)mnt;
    if (!out_stat) return false;
    ramfs_node_t* node = resolve_ramfs_node(rel_path);
    if (!node) return false;

    strncpy(out_stat->name, node->name, sizeof(out_stat->name) - 1);
    out_stat->name[sizeof(out_stat->name) - 1] = '\0';
    out_stat->size = node->size;
    out_stat->is_directory = node->is_directory;
    out_stat->attributes = node->is_directory ? VFS_ATTR_DIRECTORY : VFS_ATTR_ARCHIVE;
    return true;
}

static vfs_driver_t g_ramfs_driver = {
    .name = "RamFS",
    .mount = ramfs_vfs_mount,
    .unmount = NULL,
    .open = ramfs_vfs_open,
    .read = ramfs_vfs_read,
    .write = ramfs_vfs_write,
    .close = ramfs_vfs_close,
    .seek = ramfs_vfs_seek,
    .readdir = ramfs_vfs_readdir,
    .create = ramfs_vfs_create,
    .mkdir = ramfs_vfs_mkdir,
    .unlink = ramfs_vfs_unlink,
    .stat = ramfs_vfs_stat
};

void ramfs_init(void) {
    memset(g_nodes_pool, 0, sizeof(g_nodes_pool));
    g_nodes_count = 0;

    g_root_node = alloc_node("/", true, NULL);

    // Crea sottocartelle e file di default su unita' R:
    ramfs_node_t* temp_dir = alloc_node("TEMP", true, g_root_node);
    (void)temp_dir;

    ramfs_node_t* readme = alloc_node("LEGGIMI.TXT", false, g_root_node);
    if (readme) {
        const char* msg = "Disco RAM ad altissima velocita' (RamFS 1.0) - NewOS V1\n"
                          "Spazio volatile di memoria per operazioni rapide e file temporanei.\n";
        size_t len = strlen(msg);
        readme->data = (uint8_t*)kmalloc(len + 1);
        if (readme->data) {
            memcpy(readme->data, msg, len);
            readme->data[len] = '\0';
            readme->size = (uint32_t)len;
            readme->capacity = (uint32_t)(len + 1);
        }
    }

    vfs_register_driver(&g_ramfs_driver);
}

const vfs_driver_t* ramfs_get_vfs_driver(void) {
    return &g_ramfs_driver;
}
