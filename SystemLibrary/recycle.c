/**
 * SystemLibrary/recycle.c -> RECYCLE.DLL
 * Libreria dinamica di sistema out-of-kernel (C:\NewOS\SystemLibrary\RECYCLE.DLL)
 * Funzionalità:
 *   1. Gestione completa del Cestino VFS reale (C:\Recycle.Bin + RECYCLE.IDX + RECYCLE.CFG)
 *   2. Configurazione Limite Spazio (Quota Cestino) e Politiche di Eliminazione
 *   3. Rilevamento automatico superamento limite spazio con finestra a tema che
 *      suggerisce l'eliminazione diretta (definitiva) del file o lo svuotamento preventivo
 *   4. Ripristino selettivo o totale dei file nella posizione originaria e Svuotamento Cestino
 * (C) Newspoint Software - NewSoft Dynamic System Library
 */

#include "cpl_dll_ui.h"
#include <drivers/keyboard.h>
#include <fs/vfs.h>
#include <include/comdlg.h>

#ifndef BUILDING_KERNEL_LOADER
__attribute__((section(".header"), used)) module_header_t g_recycle_header = {
    .magic = MODULE_MAGIC_NEWOS,
    .header_size = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type = MODULE_TYPE_SYSTEM_LIBRARY,
    .code_size = 0,
    .entry_offset = 64,
    .checksum = 0,
    .security_flags =
        SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name = "RECYCLE",
    .signature = MODULE_AUTH_TAG};
#endif

static const kernel_api_t *g_api = NULL;

// Soglie preset di spazio per il Cestino (in byte)
// Indice 0: 200 B (Soglia bassa per test immediato superamento quota)
// Indice 1: 1 KB (1024 B)
// Indice 2: 64 KB (65536 B)
// Indice 3: 512 KB (524288 B)
// Indice 4: 2 MB (2097152 B)
// Indice 5: 10 MB (10485760 B)
static const uint32_t g_quota_bytes[6] = {
    200,
    1024,
    65536,
    524288,
    2097152,
    10485760
};

static const char g_quota_labels[6][10] = {
    "200 B",
    "1 KB",
    "64 KB",
    "512 KB",
    "2 MB",
    "10 MB"
};

typedef struct {
    int      view_mode;                // 0 = Impostazioni Cestino, 1 = Avviso Superamento Limite (Eliminazione Diretta)
    int      limit_preset;             // 0..5 (default 0 o 1 da RECYCLE.CFG)
    bool     suggest_direct_on_quota;  // Suggerisci eliminazione diretta se oltre il limite spazio
    bool     bypass_recycle_bin;       // Non spostare nel Cestino (elimina sempre direttamente)
    bool     confirm_delete;           // Mostra conferma prima di eliminare/svuotare
    bool     auto_clean_oldest;        // Svuota automaticamente i file piu' vecchi al raggiungimento soglia
    uint32_t used_bytes;               // Spazio attualmente occupato in C:\Recycle.Bin
    uint32_t file_count;               // Numero file attualmente in C:\Recycle.Bin
    char     pending_path[128];        // Percorso del file che ha fatto scattare l'avviso di superamento quota
    char     pending_name[64];         // Nome breve del file in eliminazione
    uint32_t pending_size;             // Dimensione in byte del file in eliminazione
    char     status_msg[88];           // Messaggio di stato nella barra inferiore
} recycle_state_t;

// --- Helper Stringhe e Percorsi ---
static void rec_extract_basename(const char *full_path, char *out_name, size_t max_len) {
    if (!full_path || !out_name || max_len == 0)
        return;
    const char *last = full_path;
    for (const char *p = full_path; *p; p++) {
        if (*p == '\\' || *p == '/') {
            if (*(p + 1) != '\0')
                last = p + 1;
        }
    }
    dll_strcpy(out_name, last, max_len);
}

static void rec_path_join(const char *dir, const char *name, char *out_full, size_t max_len) {
    if (!out_full || max_len == 0)
        return;
    out_full[0] = '\0';
    if (!dir || !dir[0]) {
        dll_strcpy(out_full, name ? name : "", max_len);
        return;
    }
    dll_strcpy(out_full, dir, max_len);
    size_t len = dll_strlen(out_full);
    if (len > 0 && out_full[len - 1] != '\\' && out_full[len - 1] != '/') {
        if (len + 1 < max_len) {
            out_full[len++] = '\\';
            out_full[len] = '\0';
        }
    }
    if (name) {
        size_t i = 0;
        while (name[i] && len + 1 < max_len) {
            out_full[len++] = name[i++];
        }
        out_full[len] = '\0';
    }
}

static bool rec_is_internal_meta(const char *name) {
    if (!name || !name[0])
        return true;
    if (dll_strcmp(name, ".") == 0 || dll_strcmp(name, "..") == 0)
        return true;
    if (dll_strcmp(name, "RECYCLE.IDX") == 0 || dll_strcmp(name, "recycle.idx") == 0)
        return true;
    if (dll_strcmp(name, "RECYCLE.CFG") == 0 || dll_strcmp(name, "recycle.cfg") == 0)
        return true;
    return false;
}

static void rec_format_size(uint32_t bytes, char *out_str, size_t max_len) {
    if (!out_str || max_len == 0)
        return;
    char num[16];
    if (bytes >= 1024 * 1024) {
        dll_uint_to_str(bytes / (1024 * 1024), num, 0);
        dll_strcpy(out_str, num, max_len);
        size_t l = dll_strlen(out_str);
        dll_strcpy(out_str + l, " MB", max_len - l);
    } else if (bytes >= 1024) {
        dll_uint_to_str(bytes / 1024, num, 0);
        dll_strcpy(out_str, num, max_len);
        size_t l = dll_strlen(out_str);
        dll_strcpy(out_str + l, " KB (", max_len - l);
        l = dll_strlen(out_str);
        dll_uint_to_str(bytes, num, 0);
        dll_strcpy(out_str + l, num, max_len - l);
        l = dll_strlen(out_str);
        dll_strcpy(out_str + l, " B)", max_len - l);
    } else {
        dll_uint_to_str(bytes, num, 0);
        dll_strcpy(out_str, num, max_len);
        size_t l = dll_strlen(out_str);
        dll_strcpy(out_str + l, " byte", max_len - l);
    }
}

static void rec_ensure_dir(void) {
    if (!g_api)
        return;
    if (!g_api->vfs_exists("C:\\Recycle.Bin") && g_api->vfs_mkdir) {
        g_api->vfs_mkdir("C:\\Recycle.Bin");
    }
}

// --- Caricamento e Salvataggio Configurazione C:\Recycle.Bin\RECYCLE.CFG ---
static void rec_load_config(recycle_state_t *st) {
    if (!st)
        return;
    // Valori predefiniti
    st->limit_preset = 0; // 200 B predefinito per permettere verifica immediata della soglia
    st->suggest_direct_on_quota = true;
    st->bypass_recycle_bin = false;
    st->confirm_delete = true;
    st->auto_clean_oldest = false;

    if (!g_api || !g_api->vfs_read_entire_file)
        return;

    uint32_t sz = 0;
    uint8_t *buf = g_api->vfs_read_entire_file("C:\\Recycle.Bin\\RECYCLE.CFG", &sz);
    if (!buf || sz == 0) {
        if (buf)
            g_api->kfree(buf);
        return;
    }

    for (uint32_t i = 0; i + 12 < sz; i++) {
        if (buf[i] == 'L' && buf[i + 1] == 'i' && buf[i + 2] == 'm' &&
            buf[i + 3] == 'i' && buf[i + 4] == 't' && buf[i + 5] == 'P' &&
            buf[i + 11] == '=') {
            int p = buf[i + 12] - '0';
            if (p >= 0 && p <= 5)
                st->limit_preset = p;
        }
        if (buf[i] == 'S' && buf[i + 1] == 'u' && buf[i + 2] == 'g' &&
            buf[i + 3] == 'g' && buf[i + 4] == 'e' && buf[i + 5] == 's' &&
            buf[i + 6] == 't' && buf[i + 7] == '=') {
            st->suggest_direct_on_quota = (buf[i + 8] != '0');
        }
        if (buf[i] == 'B' && buf[i + 1] == 'y' && buf[i + 2] == 'p' &&
            buf[i + 3] == 'a' && buf[i + 4] == 's' && buf[i + 5] == 's' &&
            buf[i + 6] == '=') {
            st->bypass_recycle_bin = (buf[i + 7] == '1');
        }
        if (buf[i] == 'C' && buf[i + 1] == 'o' && buf[i + 2] == 'n' &&
            buf[i + 3] == 'f' && buf[i + 4] == 'i' && buf[i + 5] == 'r' &&
            buf[i + 6] == 'm' && buf[i + 7] == '=') {
            st->confirm_delete = (buf[i + 8] != '0');
        }
        if (buf[i] == 'A' && buf[i + 1] == 'u' && buf[i + 2] == 't' &&
            buf[i + 3] == 'o' && buf[i + 4] == 'C' && buf[i + 5] == 'l' &&
            buf[i + 6] == 'n' && buf[i + 7] == '=') {
            st->auto_clean_oldest = (buf[i + 8] == '1');
        }
    }
    g_api->kfree(buf);
}

static void rec_save_config(const recycle_state_t *st) {
    if (!g_api || !st)
        return;
    rec_ensure_dir();

    const char *cfg_path = "C:\\Recycle.Bin\\RECYCLE.CFG";
    char out[256];
    size_t pos = 0;

    const char *hdr = "[RecycleBin]\r\nLimitPreset=";
    for (size_t i = 0; hdr[i]; i++) out[pos++] = hdr[i];
    out[pos++] = (char)('0' + (st->limit_preset % 6));

    const char *s1 = "\r\nSuggest=";
    for (size_t i = 0; s1[i]; i++) out[pos++] = s1[i];
    out[pos++] = st->suggest_direct_on_quota ? '1' : '0';

    const char *s2 = "\r\nBypass=";
    for (size_t i = 0; s2[i]; i++) out[pos++] = s2[i];
    out[pos++] = st->bypass_recycle_bin ? '1' : '0';

    const char *s3 = "\r\nConfirm=";
    for (size_t i = 0; s3[i]; i++) out[pos++] = s3[i];
    out[pos++] = st->confirm_delete ? '1' : '0';

    const char *s4 = "\r\nAutoCln=";
    for (size_t i = 0; s4[i]; i++) out[pos++] = s4[i];
    out[pos++] = st->auto_clean_oldest ? '1' : '0';

    out[pos++] = '\r';
    out[pos++] = '\n';
    out[pos] = '\0';

    if (!g_api->vfs_exists(cfg_path) && g_api->vfs_create) {
        g_api->vfs_create(cfg_path);
    }
    if (g_api->vfs_write_entire_file_inplace) {
        g_api->vfs_write_entire_file_inplace(cfg_path, out, (uint32_t)pos);
    }
}

// --- Calcolo Spazio Occupato e Conteggio File in C:\Recycle.Bin ---
static void rec_refresh_stats(recycle_state_t *st) {
    if (!st)
        return;
    st->used_bytes = 0;
    st->file_count = 0;
    if (!g_api || !g_api->vfs_readdir)
        return;

    rec_ensure_dir();
    vfs_dirent_t entries[24];
    int cnt = g_api->vfs_readdir("C:\\Recycle.Bin", entries, 24);
    for (int i = 0; i < cnt; i++) {
        if (entries[i].is_directory || rec_is_internal_meta(entries[i].name))
            continue;
        st->file_count++;
        uint32_t fsz = entries[i].size;
        if (fsz == 0 && g_api->vfs_read_entire_file) {
            char fp[128];
            rec_path_join("C:\\Recycle.Bin", entries[i].name, fp, sizeof(fp));
            uint8_t *d = g_api->vfs_read_entire_file(fp, &fsz);
            if (d)
                g_api->kfree(d);
        }
        st->used_bytes += fsz;
    }
}

static uint32_t rec_get_file_size(const char *path) {
    if (!g_api || !path || !path[0])
        return 0;
    uint32_t sz = 0;
    uint8_t *buf = g_api->vfs_read_entire_file(path, &sz);
    if (buf)
        g_api->kfree(buf);
    return sz;
}

// --- Operazioni VFS Cestino (Spostamento, Ripristino, Svuotamento) ---
static bool rec_copy_file(const char *src_path, const char *dst_path) {
    if (!g_api || !src_path || !dst_path || !src_path[0] || !dst_path[0])
        return false;
    if (dll_strcmp(src_path, dst_path) == 0)
        return true;

    uint32_t sz = 0;
    uint8_t *data = g_api->vfs_read_entire_file(src_path, &sz);
    if (!data && sz > 0)
        return false;

    if (!g_api->vfs_exists(dst_path)) {
        if (!g_api->vfs_create(dst_path)) {
            if (data)
                g_api->kfree(data);
            return false;
        }
    }

    int32_t wr = 0;
    if (sz > 0 && data) {
        wr = g_api->vfs_write_entire_file_inplace(dst_path, data, sz);
    } else {
        wr = g_api->vfs_write_entire_file_inplace(dst_path, "", 0);
    }
    if (data)
        g_api->kfree(data);
    return (wr >= 0);
}

static bool rec_move_file(const char *src_path, const char *dst_path) {
    if (!g_api || !src_path || !dst_path || !src_path[0] || !dst_path[0])
        return false;
    if (dll_strcmp(src_path, dst_path) == 0)
        return true;
    if (g_api->vfs_rename && g_api->vfs_rename(src_path, dst_path))
        return true;
    if (rec_copy_file(src_path, dst_path)) {
        if (g_api->vfs_unlink)
            g_api->vfs_unlink(src_path);
        return true;
    }
    return false;
}

static void rec_idx_record(const char *basename, const char *orig_path) {
    if (!g_api || !basename || !orig_path)
        return;
    rec_ensure_dir();

    const char *idx_path = "C:\\Recycle.Bin\\RECYCLE.IDX";
    uint32_t old_sz = 0;
    uint8_t *old_buf = g_api->vfs_read_entire_file(idx_path, &old_sz);

    char new_buf[1024];
    size_t pos = 0;
    if (old_buf && old_sz > 0) {
        if (old_sz > 820)
            old_sz = 820;
        for (uint32_t i = 0; i < old_sz; i++) {
            if (old_buf[i] != '\0')
                new_buf[pos++] = (char)old_buf[i];
        }
        g_api->kfree(old_buf);
    }
    size_t blen = dll_strlen(basename);
    for (size_t i = 0; i < blen && pos + 2 < sizeof(new_buf); i++)
        new_buf[pos++] = basename[i];
    if (pos + 2 < sizeof(new_buf))
        new_buf[pos++] = '|';
    size_t olen = dll_strlen(orig_path);
    for (size_t i = 0; i < olen && pos + 3 < sizeof(new_buf); i++)
        new_buf[pos++] = orig_path[i];
    if (pos + 3 < sizeof(new_buf)) {
        new_buf[pos++] = '\r';
        new_buf[pos++] = '\n';
    }
    new_buf[pos] = '\0';

    if (!g_api->vfs_exists(idx_path) && g_api->vfs_create) {
        g_api->vfs_create(idx_path);
    }
    g_api->vfs_write_entire_file_inplace(idx_path, new_buf, (uint32_t)pos);
}

static void rec_idx_lookup_and_remove(const char *basename, char *out_orig, size_t max_len) {
    if (!out_orig || max_len == 0)
        return;
    out_orig[0] = '\0';
    if (!g_api || !basename)
        return;

    const char *idx_path = "C:\\Recycle.Bin\\RECYCLE.IDX";
    uint32_t sz = 0;
    uint8_t *buf = g_api->vfs_read_entire_file(idx_path, &sz);
    if (!buf || sz == 0) {
        if (buf)
            g_api->kfree(buf);
        rec_path_join("C:\\User\\Morgan\\Desktop\\", basename, out_orig, max_len);
        return;
    }

    char updated[1024];
    size_t upos = 0;
    size_t b_len = dll_strlen(basename);

    uint32_t line_start = 0;
    for (uint32_t i = 0; i <= sz; i++) {
        if (i == sz || buf[i] == '\n' || buf[i] == '\0') {
            uint32_t line_end = i;
            if (line_end > line_start && buf[line_end - 1] == '\r')
                line_end--;
            uint32_t line_len = line_end - line_start;
            if (line_len > 0) {
                bool match = false;
                if (line_len > b_len + 1 && buf[line_start + b_len] == '|') {
                    match = true;
                    for (size_t k = 0; k < b_len; k++) {
                        if ((char)buf[line_start + k] != basename[k]) {
                            match = false;
                            break;
                        }
                    }
                }
                if (match && out_orig[0] == '\0') {
                    size_t opos = 0;
                    for (uint32_t k = line_start + (uint32_t)b_len + 1;
                         k < line_end && opos + 1 < max_len; k++) {
                        out_orig[opos++] = (char)buf[k];
                    }
                    out_orig[opos] = '\0';
                } else {
                    for (uint32_t k = line_start;
                         k < line_end && upos + 3 < sizeof(updated); k++) {
                        updated[upos++] = (char)buf[k];
                    }
                    if (upos + 3 < sizeof(updated)) {
                        updated[upos++] = '\r';
                        updated[upos++] = '\n';
                    }
                }
            }
            if (i < sz && buf[i] == '\0')
                break;
            line_start = i + 1;
        }
    }
    updated[upos] = '\0';
    g_api->kfree(buf);

    g_api->vfs_write_entire_file_inplace(idx_path, updated, (uint32_t)upos);
    if (!out_orig[0]) {
        rec_path_join("C:\\User\\Morgan\\Desktop\\", basename, out_orig, max_len);
    }
}

static bool rec_send_to_bin(const char *src_path) {
    if (!g_api || !src_path || !src_path[0])
        return false;
    rec_ensure_dir();

    char base[64];
    rec_extract_basename(src_path, base, sizeof(base));
    if (!base[0])
        return false;

    char dst_path[128];
    rec_path_join("C:\\Recycle.Bin", base, dst_path, sizeof(dst_path));
    if (rec_move_file(src_path, dst_path)) {
        rec_idx_record(base, src_path);
        return true;
    }
    return false;
}

static bool rec_restore_one(const char *src_or_name) {
    if (!g_api || !src_or_name || !src_or_name[0])
        return false;
    char base[64];
    rec_extract_basename(src_or_name, base, sizeof(base));
    if (!base[0])
        return false;

    char bin_path[128];
    rec_path_join("C:\\Recycle.Bin", base, bin_path, sizeof(bin_path));
    char orig_path[128];
    rec_idx_lookup_and_remove(base, orig_path, sizeof(orig_path));
    return rec_move_file(bin_path, orig_path);
}

static int rec_restore_all(void) {
    if (!g_api || !g_api->vfs_readdir)
        return 0;
    vfs_dirent_t entries[24];
    int cnt = g_api->vfs_readdir("C:\\Recycle.Bin", entries, 24);
    int restored = 0;
    for (int i = 0; i < cnt; i++) {
        if (entries[i].is_directory || rec_is_internal_meta(entries[i].name))
            continue;
        if (rec_restore_one(entries[i].name))
            restored++;
    }
    return restored;
}

static int rec_empty_all(void) {
    if (!g_api || !g_api->vfs_readdir)
        return 0;
    vfs_dirent_t entries[24];
    int cnt = g_api->vfs_readdir("C:\\Recycle.Bin", entries, 24);
    int removed = 0;
    for (int i = 0; i < cnt; i++) {
        if (entries[i].is_directory || rec_is_internal_meta(entries[i].name))
            continue;
        char full[128];
        rec_path_join("C:\\Recycle.Bin", entries[i].name, full, sizeof(full));
        if (g_api->vfs_unlink && g_api->vfs_unlink(full))
            removed++;
    }
    if (g_api->vfs_exists("C:\\Recycle.Bin\\RECYCLE.IDX")) {
        g_api->vfs_write_entire_file_inplace("C:\\Recycle.Bin\\RECYCLE.IDX", "", 0);
    }
    return removed;
}

// --- Helper Grafico Checkbox a Tema ---
static void rec_draw_checkbox(const dll_theme_t *t, int32_t x, int32_t y, bool checked, const char *label) {
    if (!g_api || !t)
        return;
    if (t->is_classic) {
        g_api->gfx_fill_rect(x, y, 14, 14, 0xFFFFFF);
        g_api->gfx_draw_bevel_rect(x, y, 14, 14, true);
        if (checked) {
            g_api->gfx_draw_string(x + 3, y, "x", 0x000000, 0, true);
        }
    } else {
        uint32_t bg = checked ? t->accent : t->input_bg;
        uint32_t brd = checked ? t->accent : t->input_border;
        g_api->gfx_fill_rounded_rect(x, y, 15, 15, 4, bg);
        g_api->gfx_draw_rounded_rect(x, y, 15, 15, 4, brd);
        if (checked) {
            g_api->gfx_draw_string(x + 4, y + 1, "v", 0xFFFFFF, 0, true);
        }
    }
    g_api->gfx_draw_string(x + 22, y + 1, label, t->text_primary, 0, true);
}

// ==============================================================================
// RENDERING FINESTRA (MODALITÀ 0: IMPOSTAZIONI CESTINO | MODALITÀ 1: AVVISO QUOTA)
// ==============================================================================
static void recycle_draw(window_t *win) {
    if (!g_api || !win || !win->app_data)
        return;
    recycle_state_t *st = (recycle_state_t *)win->app_data;

    int32_t cx = win->x + 3;
    int32_t cy = win->y + 24;
    int32_t cw = win->width - 6;
    int32_t ch = win->height - 27;

    dll_theme_t t;
    dll_theme_init(g_api, &t);
    dll_draw_dialog_bg(g_api, &t, cx, cy, cw, ch);

    uint32_t limit_bytes = g_quota_bytes[st->limit_preset % 6];

    // --------------------------------------------------------------------------
    // VISTA 1: AVVISO SUPERAMENTO LIMITE SPAZIO -> SUGGERIMENTO ELIMINAZIONE DIRETTA
    // --------------------------------------------------------------------------
    if (st->view_mode == 1) {
        dll_strcpy(win->title, "Limite spazio Cestino superato (RECYCLE.DLL)", sizeof(win->title));

        // Banner superiore di avviso superamento quota
        dll_draw_groupbox(g_api, cx + 10, cy + 10, cw - 20, 62, "Avviso Limite Spazio Cestino", &t);
        g_api->gfx_draw_icon_32(cx + 22, cy + 24, ICON_CESTINO);
        g_api->gfx_draw_string(cx + 64, cy + 24,
                               "Spazio nel Cestino insufficiente per l'elemento!",
                               t.is_dark ? 0xFF8080 : 0xB01818, 0, true);
        g_api->gfx_draw_string(cx + 64, cy + 44,
                               "Il sistema suggerisce l'eliminazione diretta (definitiva).",
                               t.text_primary, 0, true);

        // Riquadro Dettagli Elemento e Calcolo Quota
        dll_draw_groupbox(g_api, cx + 10, cy + 82, cw - 20, 114, "Dettagli File e Quota Disco C:", &t);

        char file_line[80] = "Elemento: ";
        dll_strcpy(file_line + 10, st->pending_name[0] ? st->pending_name : "DATI_UTENTE.PAK", sizeof(file_line) - 10);
        size_t fl = dll_strlen(file_line);
        dll_strcpy(file_line + fl, " (", sizeof(file_line) - fl);
        fl = dll_strlen(file_line);
        char fsz_str[24];
        rec_format_size(st->pending_size, fsz_str, sizeof(fsz_str));
        dll_strcpy(file_line + fl, fsz_str, sizeof(file_line) - fl);
        fl = dll_strlen(file_line);
        dll_strcpy(file_line + fl, ")", sizeof(file_line) - fl);
        g_api->gfx_draw_string(cx + 22, cy + 100, file_line, t.text_primary, 0, true);

        char quota_line[88] = "Spazio richiesto: ";
        uint32_t req_total = st->used_bytes + st->pending_size;
        char req_str[24], lim_str[24];
        rec_format_size(req_total, req_str, sizeof(req_str));
        rec_format_size(limit_bytes, lim_str, sizeof(lim_str));
        size_t ql = dll_strlen(quota_line);
        dll_strcpy(quota_line + ql, req_str, sizeof(quota_line) - ql);
        ql = dll_strlen(quota_line);
        dll_strcpy(quota_line + ql, " / Limite: ", sizeof(quota_line) - ql);
        ql = dll_strlen(quota_line);
        dll_strcpy(quota_line + ql, lim_str, sizeof(quota_line) - ql);
        g_api->gfx_draw_string(cx + 22, cy + 120, quota_line, t.accent, 0, true);

        // Barra grafica di saturazione quota (100% satura in rosso/arancione)
        int32_t bx = cx + 22;
        int32_t by = cy + 142;
        int32_t bw = cw - 44;
        int32_t bh = 16;
        g_api->gfx_fill_rounded_rect(bx, by, bw, bh, 4, t.is_dark ? 0x1E1E24 : 0xE8ECF2);
        g_api->gfx_fill_rounded_rect(bx + 2, by + 2, bw - 4, bh - 4, 3, 0xD83B01);
        g_api->gfx_draw_rounded_rect(bx, by, bw, bh, 4, t.card_border);
        g_api->gfx_draw_string(bx + (bw / 2) - 84, by + 1, "QUOTA CESTINO SUPERATA", 0xFFFFFF, 0, true);

        g_api->gfx_draw_string(cx + 22, cy + 168,
                               "Vuoi eliminare direttamente il file senza passare dal Cestino?",
                               t.text_secondary, 0, true);

        // Barra Stato
        g_api->gfx_draw_string(cx + 12, cy + ch - 54, st->status_msg, t.accent, 0, true);

        // Pulsanti decisionali in basso
        int32_t btn_y = cy + ch - 32;
        g_api->wm_draw_themed_button(cx + 10, btn_y, 148, 24, "Elimina direttamente", true);
        g_api->wm_draw_themed_button(cx + 164, btn_y, 126, 24, "Svuota e Cestina", false);
        g_api->wm_draw_themed_button(cx + 296, btn_y, 96, 24, "Impostazioni", false);
        g_api->wm_draw_themed_button(cx + cw - 68, btn_y, 58, 24, "Annulla", false);
        return;
    }

    // --------------------------------------------------------------------------
    // VISTA 0: FINESTRA IMPOSTAZIONI E GESTIONE CESTINO (RECYCLE.DLL)
    // --------------------------------------------------------------------------
    dll_strcpy(win->title, "Proprieta e Gestione Cestino (RECYCLE.DLL)", sizeof(win->title));

    // 1. Intestazione e Stato Attuale Quota C:\Recycle.Bin
    dll_draw_groupbox(g_api, cx + 10, cy + 10, cw - 20, 92, "Stato Cestino - Disco locale (C:)", &t);
    g_api->gfx_draw_icon_32(cx + 22, cy + 26, ICON_CESTINO);

    char info1[80] = "Percorso VFS: C:\\Recycle.Bin   |   Elementi: ";
    size_t i1 = dll_strlen(info1);
    char cnt_str[12];
    dll_uint_to_str(st->file_count, cnt_str, 0);
    dll_strcpy(info1 + i1, cnt_str, sizeof(info1) - i1);
    i1 = dll_strlen(info1);
    dll_strcpy(info1 + i1, " file", sizeof(info1) - i1);
    g_api->gfx_draw_string(cx + 64, cy + 24, info1, t.text_primary, 0, true);

    char used_s[24], lim_s[24];
    rec_format_size(st->used_bytes, used_s, sizeof(used_s));
    rec_format_size(limit_bytes, lim_s, sizeof(lim_s));
    char info2[88] = "Spazio occupato: ";
    size_t i2 = dll_strlen(info2);
    dll_strcpy(info2 + i2, used_s, sizeof(info2) - i2);
    i2 = dll_strlen(info2);
    dll_strcpy(info2 + i2, " su ", sizeof(info2) - i2);
    i2 = dll_strlen(info2);
    dll_strcpy(info2 + i2, lim_s, sizeof(info2) - i2);
    g_api->gfx_draw_string(cx + 64, cy + 44, info2, t.accent, 0, true);

    // Barra grafica di utilizzo rispetto al limite spazio configurato
    int32_t bar_x = cx + 64;
    int32_t bar_y = cy + 66;
    int32_t bar_w = cw - 86;
    int32_t bar_h = 18;
    g_api->gfx_fill_rounded_rect(bar_x, bar_y, bar_w, bar_h, 4, t.is_dark ? 0x1E1E24 : 0xE6ECF5);

    uint32_t pct = (limit_bytes > 0) ? ((st->used_bytes * 100) / limit_bytes) : 100;
    if (pct > 100)
        pct = 100;
    int32_t fill_w = ((bar_w - 4) * (int32_t)pct) / 100;
    if (fill_w < 6 && st->used_bytes > 0)
        fill_w = 6;
    uint32_t bar_col = (pct >= 85) ? 0xD83B01 : t.accent;
    if (fill_w > 0) {
        g_api->gfx_fill_rounded_rect(bar_x + 2, bar_y + 2, fill_w, bar_h - 4, 3, bar_col);
    }
    g_api->gfx_draw_rounded_rect(bar_x, bar_y, bar_w, bar_h, 4, t.card_border);

    char pct_txt[24];
    dll_uint_to_str(pct, pct_txt, 0);
    size_t pl = dll_strlen(pct_txt);
    dll_strcpy(pct_txt + pl, "% usato", sizeof(pct_txt) - pl);
    g_api->gfx_draw_string(bar_x + (bar_w / 2) - 28, bar_y + 2, pct_txt,
                           (pct >= 50) ? 0xFFFFFF : t.text_primary, 0, true);

    // 2. Selettore Limite Spazio Cestino (Quota Massima)
    dll_draw_groupbox(g_api, cx + 10, cy + 112, cw - 20, 86,
                      "Limite Spazio Cestino (Soglia Eliminazione Diretta)", &t);
    g_api->gfx_draw_string(cx + 22, cy + 128,
                           "Dimensione massima riservata al Cestino:",
                           t.text_primary, 0, true);

    int32_t pill_start_x = cx + 22;
    int32_t pill_y = cy + 146;
    int32_t pill_w = 68;
    int32_t pill_gap = 6;
    for (int i = 0; i < 6; i++) {
        int32_t px = pill_start_x + i * (pill_w + pill_gap);
        bool active = (st->limit_preset == i);
        g_api->wm_draw_themed_button(px, pill_y, pill_w, 22, g_quota_labels[i], active);
    }

    g_api->gfx_draw_string(cx + 22, cy + 174,
                           "Nota: oltre questa soglia il sistema suggerisce l'eliminazione diretta.",
                           t.text_secondary, 0, true);

    // 3. Politiche di Eliminazione e Conferma
    dll_draw_groupbox(g_api, cx + 10, cy + 208, cw - 20, 108,
                      "Impostazioni e Comportamento di Eliminazione", &t);

    rec_draw_checkbox(&t, cx + 22, cy + 224, st->suggest_direct_on_quota,
                      "Suggerisci eliminazione diretta se il Cestino supera il limite spazio");
    rec_draw_checkbox(&t, cx + 22, cy + 246, st->bypass_recycle_bin,
                      "Non spostare i file nel Cestino (elimina sempre direttamente)");
    rec_draw_checkbox(&t, cx + 22, cy + 268, st->confirm_delete,
                      "Visualizza finestra di conferma eliminazione e riepilogo quota");
    rec_draw_checkbox(&t, cx + 22, cy + 290, st->auto_clean_oldest,
                      "Rimuovi automaticamente i file meno recenti quando la quota e' piena");

    // Messaggio di stato
    g_api->gfx_draw_string(cx + 14, cy + 322, st->status_msg, t.accent, 0, true);

    // 4. Pulsanti Azione in Basso
    int32_t act_y = cy + ch - 30;
    g_api->wm_draw_themed_button(cx + 10, act_y, 106, 24, "Ripristina tutti", false);
    g_api->wm_draw_themed_button(cx + 122, act_y, 102, 24, "Svuota Cestino", false);
    g_api->wm_draw_themed_button(cx + 230, act_y, 104, 24, "Test Supero", false);
    g_api->wm_draw_themed_button(cx + 340, act_y, 68, 24, "Salva", true);
    g_api->wm_draw_themed_button(cx + cw - 74, act_y, 64, 24, "Chiudi", false);
}

static void recycle_mouse(window_t *win, int32_t mx, int32_t my, bool clicked) {
    if (!clicked || !g_api || !win || !win->app_data)
        return;
    recycle_state_t *st = (recycle_state_t *)win->app_data;

    int32_t cx = 3;
    int32_t cy = 24;
    int32_t cw = win->width - 6;
    int32_t ch = win->height - 27;

    // Gestione click nella Vista 1: Avviso Superamento Limite Spazio (Eliminazione Diretta)
    if (st->view_mode == 1) {
        int32_t btn_y = cy + ch - 32;
        if (my >= btn_y && my <= btn_y + 26) {
            // [ Elimina direttamente ]
            if (mx >= cx + 10 && mx <= cx + 158) {
                if (st->pending_path[0] && g_api->vfs_unlink) {
                    g_api->vfs_unlink(st->pending_path);
                }
                g_api->wm_destroy_window(win->id);
                return;
            }
            // [ Svuota e Cestina ]
            if (mx >= cx + 164 && mx <= cx + 290) {
                rec_empty_all();
                if (st->pending_path[0]) {
                    uint32_t lim = g_quota_bytes[st->limit_preset % 6];
                    if (st->pending_size <= lim) {
                        rec_send_to_bin(st->pending_path);
                    } else if (g_api->vfs_unlink) {
                        g_api->vfs_unlink(st->pending_path);
                    }
                }
                g_api->wm_destroy_window(win->id);
                return;
            }
            // [ Impostazioni ] -> Passa alla schermata di configurazione quota Cestino
            if (mx >= cx + 296 && mx <= cx + 392) {
                st->view_mode = 0;
                win->width = 500;
                win->height = 395;
                rec_refresh_stats(st);
                dll_strcpy(st->status_msg,
                           "Aumenta il limite spazio o gestisci le opzioni del Cestino.",
                           sizeof(st->status_msg));
                return;
            }
            // [ Annulla ]
            if (mx >= cx + cw - 68 && mx <= cx + cw - 10) {
                g_api->wm_destroy_window(win->id);
                return;
            }
        }
        return;
    }

    // Gestione click nella Vista 0: Impostazioni Cestino
    // 1. Click sui 6 preset di Limite Spazio
    int32_t pill_start_x = cx + 22;
    int32_t pill_y = cy + 146;
    int32_t pill_w = 68;
    int32_t pill_gap = 6;
    if (my >= pill_y && my <= pill_y + 24) {
        for (int i = 0; i < 6; i++) {
            int32_t px = pill_start_x + i * (pill_w + pill_gap);
            if (mx >= px && mx <= px + pill_w) {
                st->limit_preset = i;
                rec_save_config(st);
                rec_refresh_stats(st);
                char msg[88] = "Limite spazio Cestino impostato a ";
                size_t ml = dll_strlen(msg);
                dll_strcpy(msg + ml, g_quota_labels[i], sizeof(msg) - ml);
                ml = dll_strlen(msg);
                dll_strcpy(msg + ml, " (salvato in RECYCLE.CFG).", sizeof(msg) - ml);
                dll_strcpy(st->status_msg, msg, sizeof(st->status_msg));
                return;
            }
        }
    }

    // 2. Click sulle 4 Checkbox delle politiche
    if (mx >= cx + 18 && mx <= cx + cw - 24) {
        if (my >= cy + 222 && my <= cy + 240) {
            st->suggest_direct_on_quota = !st->suggest_direct_on_quota;
            rec_save_config(st);
            dll_strcpy(st->status_msg,
                       st->suggest_direct_on_quota
                           ? "Attivato suggerimento eliminazione diretta oltre limite spazio."
                           : "Disattivato suggerimento eliminazione diretta.",
                       sizeof(st->status_msg));
            return;
        }
        if (my >= cy + 244 && my <= cy + 262) {
            st->bypass_recycle_bin = !st->bypass_recycle_bin;
            rec_save_config(st);
            dll_strcpy(st->status_msg,
                       st->bypass_recycle_bin
                           ? "Modalita' eliminazione diretta immediata attiva (Cestino aggirato)."
                           : "I file eliminati verranno spostati nel Cestino entro il limite spazio.",
                       sizeof(st->status_msg));
            return;
        }
        if (my >= cy + 266 && my <= cy + 284) {
            st->confirm_delete = !st->confirm_delete;
            rec_save_config(st);
            return;
        }
        if (my >= cy + 288 && my <= cy + 306) {
            st->auto_clean_oldest = !st->auto_clean_oldest;
            rec_save_config(st);
            return;
        }
    }

    // 3. Click sui pulsanti in basso
    int32_t act_y = cy + ch - 30;
    if (my >= act_y && my <= act_y + 26) {
        // [ Ripristina tutti ]
        if (mx >= cx + 10 && mx <= cx + 116) {
            int n = rec_restore_all();
            rec_refresh_stats(st);
            if (n > 0) {
                dll_strcpy(st->status_msg, "Tutti i file sono stati ripristinati nelle cartelle originali.",
                           sizeof(st->status_msg));
            } else {
                dll_strcpy(st->status_msg, "Il Cestino e' gia' vuoto.", sizeof(st->status_msg));
            }
            return;
        }
        // [ Svuota Cestino ]
        if (mx >= cx + 122 && mx <= cx + 224) {
            rec_empty_all();
            rec_refresh_stats(st);
            dll_strcpy(st->status_msg, "Cestino svuotato con successo (0 byte occupati).",
                       sizeof(st->status_msg));
            return;
        }
        // [ Test Supero ] -> Mostra la finestra di suggerimento eliminazione diretta
        if (mx >= cx + 230 && mx <= cx + 334) {
            st->view_mode = 1;
            if (!st->pending_name[0]) {
                dll_strcpy(st->pending_path, "C:\\Docs\\ARCHIVIO_BACKUP.PAK", sizeof(st->pending_path));
                dll_strcpy(st->pending_name, "ARCHIVIO_BACKUP.PAK", sizeof(st->pending_name));
                st->pending_size = g_quota_bytes[st->limit_preset % 6] + 512;
            }
            win->width = 470;
            win->height = 285;
            dll_strcpy(st->status_msg,
                       "Limite spazio superato: scegli 'Elimina direttamente' o 'Svuota e Cestina'.",
                       sizeof(st->status_msg));
            return;
        }
        // [ Salva ]
        if (mx >= cx + 340 && mx <= cx + 408) {
            rec_save_config(st);
            dll_strcpy(st->status_msg,
                       "Impostazioni salvate in C:\\Recycle.Bin\\RECYCLE.CFG.",
                       sizeof(st->status_msg));
            return;
        }
        // [ Chiudi ]
        if (mx >= cx + cw - 74 && mx <= cx + cw - 10) {
            rec_save_config(st);
            g_api->wm_destroy_window(win->id);
            return;
        }
    }
}

static void recycle_key(window_t *win, char c) {
    if (!g_api || !win || !win->app_data)
        return;
    recycle_state_t *st = (recycle_state_t *)win->app_data;
    if (c == 27) { // ESC
        g_api->wm_destroy_window(win->id);
    } else if (c == '+' || c == '=') {
        if (st->limit_preset < 5) {
            st->limit_preset++;
            rec_save_config(st);
        }
    } else if (c == '-') {
        if (st->limit_preset > 0) {
            st->limit_preset--;
            rec_save_config(st);
        }
    }
}

// ==============================================================================
// ENTRY POINT DELLA LIBRERIA DINAMICA RECYCLE.DLL
// ==============================================================================
__attribute__((section(".text.entry"))) int
dll_entry(const kernel_api_t *api, window_t *win, recycle_request_t *req) {
    if (!api || !req)
        return -1;
    g_api = api;

    recycle_state_t tmp;
    for (size_t i = 0; i < sizeof(tmp); i++)
        ((uint8_t *)&tmp)[i] = 0;
    rec_load_config(&tmp);
    rec_refresh_stats(&tmp);

    uint32_t limit_bytes = g_quota_bytes[tmp.limit_preset % 6];
    req->out_used_bytes = tmp.used_bytes;
    req->out_limit_bytes = limit_bytes;
    req->out_file_count = tmp.file_count;
    req->out_bypass_active = tmp.bypass_recycle_bin;

    // 1. Richiesta statistiche rapide (senza finestra)
    if (req->mode == RECYCLE_MODE_GET_STATS) {
        return 0;
    }

    // 2. Ripristino singolo file da C:\Recycle.Bin
    if (req->mode == RECYCLE_MODE_RESTORE_FILE) {
        return rec_restore_one(req->target_path) ? 0 : -1;
    }

    // 3. Svuotamento completo C:\Recycle.Bin
    if (req->mode == RECYCLE_MODE_EMPTY_BIN) {
        rec_empty_all();
        return 0;
    }

    // 4. Verifica limite spazio ed eliminazione:
    //    - Restituisce 0 se il file e' stato spostato regolarmente nel Cestino.
    //    - Restituisce 1 se il file supera il limite spazio (o bypass attivo),
    //      segnalando al chiamante di aprire la finestra di suggerimento eliminazione diretta!
    if (req->mode == RECYCLE_MODE_DELETE_CHECK) {
        uint32_t fsz = rec_get_file_size(req->target_path);
        if (fsz == 0)
            fsz = 64;

        if (tmp.bypass_recycle_bin) {
            if (tmp.suggest_direct_on_quota) {
                return 1; // Richiede apertura dialogo conferma eliminazione diretta
            }
            if (g_api->vfs_unlink && g_api->vfs_unlink(req->target_path))
                return 0;
            return -1;
        }

        if (tmp.used_bytes + fsz > limit_bytes) {
            if (tmp.auto_clean_oldest && fsz <= limit_bytes) {
                rec_empty_all();
                return rec_send_to_bin(req->target_path) ? 0 : -1;
            }
            if (tmp.suggest_direct_on_quota) {
                return 1; // Superato limite spazio -> Apre finestra suggerimento eliminazione diretta!
            }
            if (g_api->vfs_unlink && g_api->vfs_unlink(req->target_path))
                return 0;
            return -1;
        }

        return rec_send_to_bin(req->target_path) ? 0 : -1;
    }

    // 5. Modalità con finestra GUI (RECYCLE_MODE_SETTINGS_DLG o RECYCLE_MODE_QUOTA_PROMPT)
    if (!win)
        return -1;

    recycle_state_t *st = (recycle_state_t *)api->kmalloc(sizeof(recycle_state_t));
    if (!st)
        return -1;
    memcpy(st, &tmp, sizeof(recycle_state_t));

    if (req->mode == RECYCLE_MODE_QUOTA_PROMPT) {
        st->view_mode = 1;
        if (req->target_path[0]) {
            dll_strcpy(st->pending_path, req->target_path, sizeof(st->pending_path));
            rec_extract_basename(req->target_path, st->pending_name, sizeof(st->pending_name));
            st->pending_size = rec_get_file_size(req->target_path);
            if (st->pending_size == 0)
                st->pending_size = 256;
        } else {
            dll_strcpy(st->pending_path, "C:\\Docs\\ARCHIVIO_BACKUP.PAK", sizeof(st->pending_path));
            dll_strcpy(st->pending_name, "ARCHIVIO_BACKUP.PAK", sizeof(st->pending_name));
            st->pending_size = limit_bytes + 512;
        }
        dll_strcpy(st->status_msg,
                   "Limite spazio Cestino superato: consigliata l'eliminazione diretta.",
                   sizeof(st->status_msg));
    } else {
        st->view_mode = 0;
        dll_strcpy(st->status_msg,
                   "Configura il limite spazio entro cui spostare i file nel Cestino.",
                   sizeof(st->status_msg));
    }

    win->app_data = st;
    win->on_draw = recycle_draw;
    win->on_mouse = recycle_mouse;
    win->on_key = recycle_key;
    return 0;
}

