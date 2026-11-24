#include <arch/x86_64/io.h>
#include <drivers/ata.h>
#include <drivers/keyboard.h>
#include <drivers/mouse.h>
#include <drivers/net/net_nic.h>
#include <drivers/power.h>
#include <drivers/rtc.h>
#include <drivers/sound.h>
#include <drivers/sound_wav.h>
#include <drivers/timer.h>
#include <fs/registry.h>
#include <fs/vfs.h>
#include <fs/vfs_mount.h>
#include <gui/graphics.h>
#include <gui/fontmgr.h>
#include <gui/icons.h>
#include <gui/image.h>
#include <gui/screen_mgr.h>
#include <gui/nscomp_loader.h>
#include <gui/wm.h>
#include <lib/aes256.h>
#include <clipboard.h>
#include <mm/heap.h>
#include <net/dhcp.h>
#include <net/dns.h>
#include <net/icmp.h>
#include <net/nettls_loader.h>
#include <net/socket.h>
#include <net/tcp.h>
#include <string.h>
#include <net/tcp.h>
#include <drivers/vga.h>
#include <drivers/driver_mgr.h>
#include <drivers/net/wifi.h>
#include <drivers/usb/usb.h>
#include <drivers/usb/uhci.h>
#include <drivers/usb/ohci.h>
#include <drivers/usb/xhci.h>
#include <drivers/ac97.h>
#include <arch/x86_64/cpu.h>
#include <arch/x86_64/smp.h>
#include <arch/x86_64/sched.h>
#include <ipc/ipc.h>
#include <mm/paging.h>
#include <fs/exfat.h>
#include <user.h>
#include <apps/regedit.h>
#include <apps/rules_viewer.h>
#include <apps/taskmgr.h>
#include <comdlg.h>
#include <drivers/pci.h>
#include <drivers/acpi.h>
#include <drivers/nvme.h>
#include <gpu3d.h>
#include <drivers/usb/usb_msc.h>
#include <gui/uistyle_loader.h>
#include <apps/installer.h>
#include <control_panel.h>
#include <launcher.h>
#include <settings.h>
#include <kernel_ext_api.h>

static kernel_api_t g_kernel_api;
static kernel_ext_api_t g_kernel_ext_api;
static bool g_api_initialized = false;
static void serial_log(const char *s);

typedef struct __attribute__((packed)) {
  char     magic[8];
  uint32_t header_size;
  uint32_t total_files;
  uint32_t total_uncompressed;
  uint32_t encrypted_size;
  uint8_t  aes_iv[16];
  uint32_t payload_crc32;
  uint8_t  reserved[20];
} shutdown_pak_hdr_t;

static bool repair_from_system_pak_ex(const char *const *fail_rel_paths, int fail_count, bool full_newos, int *out_repaired) {
  if (out_repaired) *out_repaired = 0;
  if (!full_newos && (!fail_rel_paths || fail_count <= 0)) return true;

  static const uint8_t pak_key[32] = "NewOS-V1SETUP-256BIT-AUTHENTIC26";
  const char *pak_cands[] = {
      "C:\\BootSetup\\SYSTEM.PAK",
      "C:\\NewOS\\Boot\\SYSTEM.PAK",
      "D:\\BootSetup\\SYSTEM.PAK",
      "/BootSetup/SYSTEM.PAK"
  };

  uint8_t *pak_buf = NULL;
  uint32_t pak_sz = 0;
  for (int i = 0; i < 4; i++) {
    pak_buf = (uint8_t *)vfs_read_entire_file(pak_cands[i], &pak_sz);
    if (pak_buf && pak_sz > sizeof(shutdown_pak_hdr_t)) break;
    if (pak_buf) { kfree(pak_buf); pak_buf = NULL; }
  }
  if (!pak_buf) return false;

  const shutdown_pak_hdr_t *hdr = (const shutdown_pak_hdr_t *)pak_buf;
  if (memcmp(hdr->magic, "NOSPAK10", 8) != 0 || hdr->encrypted_size == 0 ||
      sizeof(shutdown_pak_hdr_t) + hdr->encrypted_size > pak_sz) {
    kfree(pak_buf);
    return false;
  }

  uint8_t *plain = (uint8_t *)kmalloc(hdr->encrypted_size);
  if (!plain) {
    kfree(pak_buf);
    return false;
  }

  aes256_ctx_t ctx;
  aes256_init_decrypt(&ctx, pak_key);
  aes256_cbc_decrypt(&ctx, hdr->aes_iv, pak_buf + sizeof(shutdown_pak_hdr_t), plain, hdr->encrypted_size);
  kfree(pak_buf);

  uint32_t off = 0;
  uint32_t limit = hdr->total_uncompressed;
  if (limit > hdr->encrypted_size) limit = hdr->encrypted_size;
  int repaired = 0;

  for (uint32_t fi = 0; fi < hdr->total_files && off + 10 <= limit; fi++) {
    uint16_t path_len = (uint16_t)(plain[off] | (plain[off + 1] << 8));
    off += 2;
    if (off + path_len + 8 > limit) break;
    char rel[256];
    uint16_t copy_len = path_len < 255 ? path_len : 255;
    memcpy(rel, plain + off, copy_len);
    rel[copy_len] = '\0';
    off += path_len;

    uint32_t fsize = *(uint32_t *)(plain + off);
    off += 8; /* skip size + crc32 */
    if (off + fsize > limit) break;

    bool should_restore = false;
    if (full_newos) {
      should_restore = (strncmp(rel, "NewOS/", 6) == 0 || strncmp(rel, "NewOS\\", 6) == 0);
    } else {
      for (int k = 0; k < fail_count; k++) {
        if (strcasecmp(rel, fail_rel_paths[k]) == 0) {
          should_restore = true;
          break;
        }
      }
    }
    if (should_restore) {
        char dst_path[272];
        mini_snprintf(dst_path, sizeof(dst_path), "C:\\%s", rel);
        for (int p = 0; dst_path[p]; p++) {
          if (dst_path[p] == '/') dst_path[p] = '\\';
        }
        if (!vfs_exists(dst_path)) {
          vfs_create(dst_path);
        } else if (full_newos) {
          vfs_unlink(dst_path);
          vfs_create(dst_path);
        }
        vfs_file_t *vf = vfs_open(dst_path);
        if (vf) {
          int32_t wr = vfs_write(vf, plain + off, fsize);
          vfs_close(vf);
          if (wr == (int32_t)fsize) {
            repaired++;
            serial_log("[SHUTDOWN_CHECK] Riparato file critico da SYSTEM.PAK: ");
            serial_log(dst_path);
            serial_log("\n");
          }
        }
    }
    off += fsize;
  }

  kfree(plain);
  vfs_flush();
  if (out_repaired) *out_repaired = repaired;
  return (repaired > 0);
}

static bool repair_files_from_system_pak(const char *const *fail_rel_paths, int fail_count, int *out_repaired) {
  return repair_from_system_pak_ex(fail_rel_paths, fail_count, false, out_repaired);
}

static bool g_reboot_trick_active = false;

bool boot_is_reboot_trick_active(void) {
  if (g_reboot_trick_active) return true;
  uint32_t sz = 0;
  char *buf = (char *)vfs_read_entire_file("C:\\NewOS\\Boot\\boot.ini", &sz);
  if (!buf) return false;
  bool found = (sz > 0 && strstr(buf, "RebootTrick=1") != NULL);
  kfree(buf);
  return found;
}

bool screen_mgr_update_boot_ini_reboot_trick(bool active) {
  g_reboot_trick_active = active;
  const char *path = "C:\\NewOS\\Boot\\boot.ini";
  uint32_t old_sz = 0;
  char *old_buf = (char *)vfs_read_entire_file(path, &old_sz);

  if (!old_buf || old_sz == 0) {
    if (old_buf) kfree(old_buf);
    if (!active) return false;
    const char *init_data = "[BOOT]\r\nRebootTrick=1\r\nLastTrigger=REBOOT\r\n";
    if (!vfs_exists(path)) vfs_create(path);
    vfs_file_t *vf = vfs_open(path);
    bool ok = false;
    if (vf) {
      uint32_t len = (uint32_t)strlen(init_data);
      ok = (vfs_write(vf, init_data, len) == (int32_t)len);
      vfs_close(vf);
    }
    vfs_flush();
    serial_log("[BOOT.INI] Creato boot.ini con RebootTrick=1.\n");
    return ok;
  }

  /* Se si resetta al boot e RebootTrick=1 non e' presente, non toccare il file */
  if (!active && !strstr(old_buf, "RebootTrick=1")) {
    kfree(old_buf);
    return false;
  }

  size_t cap = old_sz + 128;
  char *out = (char *)kmalloc(cap);
  if (!out) {
    kfree(old_buf);
    return false;
  }

  size_t out_len = 0;
  bool saw_reboot_trick = false;
  bool saw_last_trigger = false;
  uint32_t pos = 0;

  while (pos < old_sz) {
    uint32_t line_start = pos;
    while (pos < old_sz && old_buf[pos] != '\n') pos++;
    uint32_t line_end = pos;
    if (pos < old_sz && old_buf[pos] == '\n') pos++;

    uint32_t trim_end = line_end;
    while (trim_end > line_start && (old_buf[trim_end - 1] == '\r' || old_buf[trim_end - 1] == ' ')) {
      trim_end--;
    }
    uint32_t line_len = trim_end - line_start;
    const char *line = old_buf + line_start;

    const char *repl = NULL;
    if (line_len >= 12 && strncmp(line, "RebootTrick=", 12) == 0) {
      repl = active ? "RebootTrick=1\r\n" : "RebootTrick=0\r\n";
      saw_reboot_trick = true;
    } else if (line_len >= 12 && strncmp(line, "LastTrigger=", 12) == 0) {
      repl = active ? "LastTrigger=REBOOT\r\n" : "LastTrigger=NONE\r\n";
      saw_last_trigger = true;
    }

    if (repl) {
      size_t rlen = strlen(repl);
      if (out_len + rlen < cap) {
        memcpy(out + out_len, repl, rlen);
        out_len += rlen;
      }
    } else if (line_len > 0) {
      if (out_len + line_len + 2 < cap) {
        memcpy(out + out_len, line, line_len);
        out_len += line_len;
        out[out_len++] = '\r';
        out[out_len++] = '\n';
      }
    }
  }

  if (!saw_reboot_trick) {
    const char *add_rt = active ? "RebootTrick=1\r\n" : "RebootTrick=0\r\n";
    size_t alen = strlen(add_rt);
    if (out_len + alen < cap) { memcpy(out + out_len, add_rt, alen); out_len += alen; }
  }
  if (!saw_last_trigger) {
    const char *add_lt = active ? "LastTrigger=REBOOT\r\n" : "LastTrigger=NONE\r\n";
    size_t alen = strlen(add_lt);
    if (out_len + alen < cap) { memcpy(out + out_len, add_lt, alen); out_len += alen; }
  }

  kfree(old_buf);
  vfs_unlink(path);
  vfs_create(path);
  vfs_file_t *vf = vfs_open(path);
  bool ok = false;
  if (vf) {
    ok = (vfs_write(vf, out, (uint32_t)out_len) == (int32_t)out_len);
    vfs_close(vf);
  }
  kfree(out);
  vfs_flush();
  serial_log(active
    ? "[BOOT.INI] Aggiornato RebootTrick=1 preservando le chiavi esistenti -> delega a Shutdown.\n"
    : "[BOOT.INI] Reset RebootTrick=0 preservando le chiavi esistenti.\n");
  return ok;
}

static bool screen_mgr_set_reboot_trick(void) {
  return screen_mgr_update_boot_ini_reboot_trick(true);
}

static bool screen_mgr_shutdown_integrity_check(char *out_report, size_t max_len) {
  serial_log("[SHUTDOWN_CHECK] 1. Salvataggio impostazioni utente e registro...\n");
  user_save_config();
  registry_audit_log("SHUTDOWN_CHECK", "HKEY_LOCAL_MACHINE\\NEWOS", "*", "-",
                     "Controllo variazioni registro e integrita' file critici pre-spegnimento",
                     REG_RISK_LOW);
  registry_save_to_disk();
  vfs_flush();

  const char *critical_rel[] = {
      "NewOS/Boot/KERNEL.BIN",
      "NewOS/Boot/BOOTSCR.MOD",
      "NewOS/Boot/LOGIN.MOD",
      "NewOS/Boot/LOGOUT.MOD",
      "NewOS/Restore/RECBASIC.MOD",
      "NewOS/Restore/RECADV.MOD",
      "NewOS/SystemLibrary/EXPLORER.DLL",
      "NewOS/SystemLibrary/NSCOMP.DLL",
      "NewOS/SystemLibrary/NSMOTION.DLL",
      "NewOS/SystemLibrary/THEMEMGR.DLL"
  };
  const char *fail_list[10];
  int fail_count = 0;

  for (int i = 0; i < (int)(sizeof(critical_rel) / sizeof(critical_rel[0])); i++) {
    char cpath[160];
    mini_snprintf(cpath, sizeof(cpath), "C:\\%s", critical_rel[i]);
    for (int p = 0; cpath[p]; p++) {
      if (cpath[p] == '/') cpath[p] = '\\';
    }
    uint32_t sz = 0;
    uint8_t *buf = (uint8_t *)vfs_read_entire_file(cpath, &sz);
    bool ok = (buf != NULL && sz > 0);
    if (ok && (strstr(cpath, ".MOD") || strstr(cpath, ".DLL"))) {
      const module_header_t *h = (const module_header_t *)buf;
      if (sz < sizeof(module_header_t) || h->magic != MODULE_MAGIC_NEWOS) {
        ok = false;
      }
    }
    if (buf) kfree(buf);
    if (!ok) {
      fail_list[fail_count++] = critical_rel[i];
      serial_log("[SHUTDOWN_CHECK] FailCheck rilevato su: ");
      serial_log(cpath);
      serial_log("\n");
    }
  }

  int repaired = 0;
  bool safe_ok = (fail_count == 0);
  if (!safe_ok) {
    repair_files_from_system_pak(fail_list, fail_count, &repaired);
  }

  if (out_report && max_len > 0) {
    if (safe_ok) {
      mini_snprintf(out_report, max_len, "SafeCheck OK (10/10 file critici integri)");
    } else {
      mini_snprintf(out_report, max_len, "FailCheck=%d, riparati %d da SYSTEM.PAK", fail_count, repaired);
    }
  }
  serial_log("[SHUTDOWN_CHECK] Verifica completata.\n");
  return safe_ok || (repaired == fail_count);
}

static uint32_t parse_u32_dec(const char *s) {
  uint32_t v = 0;
  while (s && *s >= '0' && *s <= '9') {
    v = v * 10 + (uint32_t)(*s - '0');
    s++;
  }
  return v;
}

static int hex_val(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
  return -1;
}

static bool restore_from_catalog_ghosts(const char *cat_buf, uint32_t cat_sz,
                                        const char *const *fail_rel_paths, int fail_count,
                                        int *out_repaired) {
  if (out_repaired) *out_repaired = 0;
  if (!cat_buf || cat_sz == 0 || !fail_rel_paths || fail_count <= 0) return false;

  int repaired = 0;
  uint32_t pos = 0;
  while (pos < cat_sz) {
    while (pos < cat_sz && (cat_buf[pos] == '\r' || cat_buf[pos] == '\n')) pos++;
    if (pos >= cat_sz) break;
    uint32_t line_start = pos;
    while (pos < cat_sz && cat_buf[pos] != '\r' && cat_buf[pos] != '\n') pos++;
    uint32_t line_len = pos - line_start;
    if (line_len < 10 || cat_buf[line_start] != 'G' || cat_buf[line_start + 1] != '|') continue;

    /* Formato: G|<rel_path>|<size>|0x<crc32>|<hex_payload> */
    const char *p = cat_buf + line_start + 2;
    const char *end = cat_buf + line_start + line_len;
    const char *sep1 = p;
    while (sep1 < end && *sep1 != '|') sep1++;
    if (sep1 >= end) continue;

    size_t rel_len = (size_t)(sep1 - p);
    if (rel_len == 0 || rel_len >= 256) continue;
    char rel[256];
    memcpy(rel, p, rel_len);
    rel[rel_len] = '\0';

    bool wanted = false;
    for (int k = 0; k < fail_count; k++) {
      if (strcasecmp(rel, fail_rel_paths[k]) == 0) {
        wanted = true;
        break;
      }
    }
    if (!wanted) continue;

    uint32_t gsize = parse_u32_dec(sep1 + 1);
    const char *sep2 = sep1 + 1;
    while (sep2 < end && *sep2 != '|') sep2++;
    if (sep2 >= end) continue;
    const char *sep3 = sep2 + 1;
    while (sep3 < end && *sep3 != '|') sep3++;
    if (sep3 >= end) continue;
    const char *hex = sep3 + 1;
    if ((uint32_t)(end - hex) < gsize * 2 || gsize == 0) continue;

    uint8_t *raw = (uint8_t *)kmalloc(gsize);
    if (!raw) continue;
    bool hex_ok = true;
    for (uint32_t i = 0; i < gsize; i++) {
      int hi = hex_val(hex[i * 2]);
      int lo = hex_val(hex[i * 2 + 1]);
      if (hi < 0 || lo < 0) { hex_ok = false; break; }
      raw[i] = (uint8_t)((hi << 4) | lo);
    }

    if (hex_ok) {
      char dst_path[272];
      mini_snprintf(dst_path, sizeof(dst_path), "C:\\%s", rel);
      for (int dp = 0; dst_path[dp]; dp++) if (dst_path[dp] == '/') dst_path[dp] = '\\';
      vfs_unlink(dst_path);
      vfs_create(dst_path);
      vfs_file_t *vf = vfs_open(dst_path);
      if (vf) {
        int32_t wr = vfs_write(vf, raw, gsize);
        vfs_close(vf);
        if (wr == (int32_t)gsize) {
          repaired++;
          serial_log("[CATALOG_GHOST] Ripristinato file di emergenza da copia fantasma: ");
          serial_log(dst_path);
          serial_log("\n");
        }
      }
    }
    kfree(raw);
  }

  if (repaired > 0) vfs_flush();
  if (out_repaired) *out_repaired = repaired;
  return (repaired > 0);
}

static bool screen_mgr_diagnose_from_catalog(bool only_critical, bool do_repair,
                                             char *out_report, size_t max_len) {
  const char *cat_paths[] = {
      "C:\\NewOS\\Restore\\store.catalog",
      "C:\\NewOS\\Boot\\store.catalog",
      "C:\\NewOS\\SystemLibrary\\store.catalog",
      "/NewOS/Restore/store.catalog"
  };

  uint32_t cat_sz = 0;
  char *cat_buf = NULL;
  for (int i = 0; i < 4; i++) {
    cat_buf = (char *)vfs_read_entire_file(cat_paths[i], &cat_sz);
    if (cat_buf && cat_sz > 0) break;
    if (cat_buf) { kfree(cat_buf); cat_buf = NULL; }
  }

  /* Se store.catalog manca, prova prima a ripristinarlo da SYSTEM.PAK */
  if (!cat_buf && do_repair) {
    const char *cat_rel[1] = {"NewOS/Restore/store.catalog"};
    int rep_cat = 0;
    repair_from_system_pak_ex(cat_rel, 1, false, &rep_cat);
    cat_buf = (char *)vfs_read_entire_file(cat_paths[0], &cat_sz);
  }

  if (!cat_buf || cat_sz == 0) {
    if (cat_buf) kfree(cat_buf);
    return false;
  }

  int total_dirs = 0;
  int fixed_dirs = 0;
  int checked_files = 0;
  int fail_total = 0;
  int repaired_total = 0;

  char *fail_batch[32];
  int batch_count = 0;

  uint32_t pos = 0;
  while (pos < cat_sz) {
    while (pos < cat_sz && (cat_buf[pos] == '\r' || cat_buf[pos] == '\n')) pos++;
    if (pos >= cat_sz) break;
    uint32_t line_start = pos;
    while (pos < cat_sz && cat_buf[pos] != '\r' && cat_buf[pos] != '\n') pos++;
    uint32_t line_len = pos - line_start;
    if (line_len < 3 || line_len >= 280) continue;

    char line[280];
    memcpy(line, cat_buf + line_start, line_len);
    line[line_len] = '\0';

    if (line[0] == 'D' && line[1] == '|') {
      total_dirs++;
      char dpath[272];
      mini_snprintf(dpath, sizeof(dpath), "C:\\%s", line + 2);
      for (int p = 0; dpath[p]; p++) if (dpath[p] == '/') dpath[p] = '\\';
      if (!vfs_exists(dpath) && do_repair) {
        if (vfs_mkdir(dpath)) fixed_dirs++;
      }
    } else if (line[0] == 'F' && line[1] == '|') {
      char tier = line[2];
      if (only_critical && tier != 'C') continue;
      if (line[3] != '|') continue;

      char *rel = line + 4;
      char *sep1 = strchr(rel, '|');
      if (!sep1) continue;
      *sep1 = '\0';
      uint32_t expected_sz = parse_u32_dec(sep1 + 1);

      checked_files++;
      char cpath[272];
      mini_snprintf(cpath, sizeof(cpath), "C:\\%s", rel);
      for (int p = 0; cpath[p]; p++) if (cpath[p] == '/') cpath[p] = '\\';

      uint32_t actual_sz = 0;
      uint8_t *fbuf = (uint8_t *)vfs_read_entire_file(cpath, &actual_sz);
      bool ok = (fbuf != NULL && actual_sz == expected_sz && actual_sz > 0);
      if (ok && (strstr(cpath, ".MOD") || strstr(cpath, ".DLL"))) {
        const module_header_t *h = (const module_header_t *)fbuf;
        if (actual_sz < sizeof(module_header_t) || h->magic != MODULE_MAGIC_NEWOS) {
          ok = false;
        }
      }
      if (fbuf) kfree(fbuf);

      if (!ok) {
        fail_total++;
        if (do_repair) {
          size_t rlen = strlen(rel) + 1;
          char *copy = (char *)kmalloc(rlen);
          if (copy) {
            memcpy(copy, rel, rlen);
            fail_batch[batch_count++] = copy;
            if (batch_count == 32) {
              int rep = 0;
              repair_from_system_pak_ex((const char *const *)fail_batch, batch_count, false, &rep);
              if (rep < batch_count) {
                int grep = 0;
                restore_from_catalog_ghosts(cat_buf, cat_sz, (const char *const *)fail_batch, batch_count, &grep);
                rep += grep;
              }
              repaired_total += rep;
              for (int b = 0; b < batch_count; b++) kfree(fail_batch[b]);
              batch_count = 0;
            }
          }
        }
      }
    }
  }

  if (do_repair && batch_count > 0) {
    int rep = 0;
    repair_from_system_pak_ex((const char *const *)fail_batch, batch_count, false, &rep);
    if (rep < batch_count) {
      int grep = 0;
      restore_from_catalog_ghosts(cat_buf, cat_sz, (const char *const *)fail_batch, batch_count, &grep);
      rep += grep;
    }
    repaired_total += rep;
    for (int b = 0; b < batch_count; b++) kfree(fail_batch[b]);
    batch_count = 0;
  }

  kfree(cat_buf);

  if (out_report && max_len > 0) {
    if (fail_total == 0 && fixed_dirs == 0) {
      mini_snprintf(out_report, max_len, "store.catalog OK (%d dir, %d/%d file integri)",
                    total_dirs, checked_files, checked_files);
    } else {
      mini_snprintf(out_report, max_len, "store.catalog: %d dir fix, %d fail, %d riparati",
                    fixed_dirs, fail_total, repaired_total);
    }
    if (vfs_exists("C:\\NewOS\\Restore")) {
      const char *log_path = "C:\\NewOS\\Restore\\last_recovery.log";
      vfs_unlink(log_path);
      vfs_create(log_path);
      vfs_file_t *lf = vfs_open(log_path);
      if (lf) {
        vfs_write(lf, out_report, (uint32_t)strlen(out_report));
        vfs_close(lf);
      }
      vfs_flush();
    }
  }
  return (fail_total == 0 || repaired_total == fail_total);
}

static bool screen_mgr_recovery_repair_system_pak(bool full_system_repair, char *out_report, size_t max_len) {
  if (screen_mgr_diagnose_from_catalog(!full_system_repair, true, out_report, max_len)) {
    return true;
  }
  /* Fallback nel caso store.catalog non sia ancora presente su disco */
  if (!full_system_repair) {
    return screen_mgr_shutdown_integrity_check(out_report, max_len);
  }
  int repaired = 0;
  bool ok = repair_from_system_pak_ex(NULL, 0, true, &repaired);
  if (out_report && max_len > 0) {
    if (ok) {
      mini_snprintf(out_report, max_len, "Ripristino completo NewOS/: %d file estratti da SYSTEM.PAK", repaired);
    } else {
      mini_snprintf(out_report, max_len, "Errore apertura/decifratura SYSTEM.PAK");
    }
  }
  return ok;
}

static bool screen_mgr_recovery_reset_boot_flags(void) {
  screen_mgr_update_boot_ini_reboot_trick(false);
  if (vfs_exists("C:\\NewOS\\Boot\\logout_mode.ini")) {
    vfs_unlink("C:\\NewOS\\Boot\\logout_mode.ini");
  }
  if (vfs_exists("C:\\NewOS\\Boot\\boot_action.ini")) {
    vfs_unlink("C:\\NewOS\\Boot\\boot_action.ini");
  }
  vfs_flush();
  return true;
}

int screen_mgr_consume_boot_recovery_mode(void) {
  const char *path = "C:\\NewOS\\Boot\\boot.ini";
  uint32_t sz = 0;
  char *buf = (char *)vfs_read_entire_file(path, &sz);
  if (!buf || sz == 0) {
    if (buf) kfree(buf);
    return 0;
  }

  int mode = 0;
  if (strstr(buf, "RecoveryMode=ADVANCED") || strstr(buf, "RecoveryMode=advanced")) {
    mode = 2;
  } else if (strstr(buf, "RecoveryMode=BASIC") || strstr(buf, "RecoveryMode=basic")) {
    mode = 1;
  }

  if (mode == 0) {
    kfree(buf);
    return 0;
  }

  /* Resetta RecoveryMode=NONE preservando tutte le altre chiavi di boot.ini */
  char *out = (char *)kmalloc(sz + 64);
  if (out) {
    size_t out_len = 0;
    uint32_t pos = 0;
    while (pos < sz) {
      uint32_t line_start = pos;
      while (pos < sz && buf[pos] != '\r' && buf[pos] != '\n') pos++;
      uint32_t line_len = pos - line_start;
      if (line_len >= 13 && (strncmp(buf + line_start, "RecoveryMode=", 13) == 0 ||
                            strncmp(buf + line_start, "recoverymode=", 13) == 0)) {
        const char *rep = "RecoveryMode=NONE\r\n";
        size_t rlen = strlen(rep);
        memcpy(out + out_len, rep, rlen);
        out_len += rlen;
      } else if (line_len > 0) {
        memcpy(out + out_len, buf + line_start, line_len);
        out_len += line_len;
        out[out_len++] = '\r';
        out[out_len++] = '\n';
      }
      while (pos < sz && (buf[pos] == '\r' || buf[pos] == '\n')) pos++;
    }
    out[out_len] = '\0';
    vfs_unlink(path);
    vfs_create(path);
    vfs_file_t *vf = vfs_open(path);
    if (vf) {
      vfs_write(vf, out, (uint32_t)out_len);
      vfs_close(vf);
    }
    kfree(out);
    vfs_flush();
  }
  kfree(buf);
  return mode;
}

static const void *screen_mgr_get_service(const char *service_name) {
  if (!service_name)
    return NULL;
  if (strcasecmp(service_name, "NSCOMP") == 0)
    return nscomp_get_api();
  if (strcasecmp(service_name, "NSMOTION") == 0)
    return nsmotion_get_api();
  if (strcasecmp(service_name, "COMCTL") == 0) {
    extern const void *comctl_get_api(void);
    return comctl_get_api();
  }
  return NULL;
}

extern void wm_focus_window(int32_t id);
extern void wm_set_background_type(int bg_type);
extern void app_comdlg_ftp_stack_launch(void);
extern void app_sound_props_launch(void);
extern void app_display_props_launch(void);
extern void app_power_props_launch(void);
extern void app_mouse_props_launch(void);
extern void app_devmgmt_launch(void);
extern void app_about_launch(void);
extern void app_user_accounts_launch(void);
extern void app_net_props_launch(void);
extern void app_timedate_props_launch(void);
extern void app_keyboard_props_launch(void);
extern void app_browser_launch(void);
extern void app_browser_open_url(const char *url);
extern void app_explorer_open_this_pc(void);
extern void app_explorer_open_drive_c(void);
extern void app_explorer_open_drive_u(void);
extern void app_explorer_open_drive_d(void);
extern void app_explorer_open_recycle_bin(void);
extern void app_recycle_quota_prompt_launch(const char *target_path);
extern void app_recycle_settings_launch(void);
extern void app_oobe_launch(void);
extern void app_notepad_open_file(const char *t, const char *c);
extern const char *ac97_get_device_name(void);
extern bool hwaccel_is_ready(void);
extern uint64_t gfx_get_lfb_phys(void);
extern bool hwaccel_init(uint64_t lfb_phys, uint32_t w, uint32_t h);
extern void *hwaccel_get_info(void);
extern bool hwaccel_set_scale(int mode, uint32_t pct);
extern int wm_get_background_type(void);
extern void wm_set_theme(int theme);
extern int vga_get_current_mode_index(void);
extern int theme_mgr_init(const char *path);
extern int theme_mgr_find_by_id(int id);
extern int theme_mgr_get_count(void);
extern bool theme_mgr_apply(int idx);
extern uint8_t sound_get_volume(void);
extern const char *sound_get_driver_name(void);
extern bool sound_is_ac97(void);
extern int cursor_mgr_get_active_scheme_index(void);
extern int cursor_mgr_get_scheme_count(void);
extern void *cursor_mgr_get_scheme_info(int idx);
extern void cursor_mgr_draw(int32_t x, int32_t y, int type);
extern bool cursor_mgr_set_active_scheme(int idx);
extern void gfx_draw_mini_monitor(int32_t x, int32_t y, int sel);
extern uint32_t power_get_idle_seconds(void);
extern void power_set_screen_timeout(uint32_t sec);
extern void driver_mgr_init(void);

static void serial_log(const char *s) {
  if (!s)
    return;
  while (*s) {
    outb(0x3F8, *s++);
  }
}

// Calcolo CRC32 standard per verifica di integrità crittografica
static uint32_t compute_crc32(const uint8_t *data, size_t length) {
  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < length; i++) {
    uint8_t byte = data[i];
    crc ^= byte;
    for (int j = 0; j < 8; j++) {
      uint32_t mask = -(crc & 1);
      crc = (crc >> 1) ^ (0xEDB88320 & mask);
    }
  }
  return ~crc;
}

static bool disk_get_info(int index, char *out_model, uint32_t *out_size_mb,
                          bool *is_cd) {
  int dev_cnt = ata_get_device_count();
  if (index < 0 || index >= dev_cnt)
    return false;
  const ata_device_t *d = ata_get_device(index);
  if (!d || !d->present)
    return false;
  if (out_model) {
    strncpy(out_model, d->model, 47);
    out_model[47] = '\0';
  }
  if (out_size_mb) {
    *out_size_mb = d->size_mb ? d->size_mb : 128;
  }
  if (is_cd) {
    *is_cd = d->is_atapi;
  }
  return true;
}

static bool disk_install_chunk(uint32_t chunk_idx, uint32_t total_chunks,
                               char *out_status, uint32_t max_status) {
  if (total_chunks == 0)
    total_chunks = 100;
  if (chunk_idx >= total_chunks)
    chunk_idx = total_chunks - 1;

  // 1. Identifica unità di destinazione (Hard Disk fisico o virtuale) e
  // sorgente (CD-ROM ATAPI)
  const ata_device_t *cd_dev = NULL;
  const ata_device_t *hd_dev = NULL;
  int dev_cnt = ata_get_device_count();
  for (int i = 0; i < dev_cnt; i++) {
    const ata_device_t *d = ata_get_device(i);
    if (d && d->present) {
      if (d->is_atapi && !cd_dev)
        cd_dev = d;
      if (!d->is_atapi && !hd_dev)
        hd_dev = d;
    }
  }

  if (!hd_dev) {
    if (out_status && max_status > 0) {
      strncpy(out_status, "ERRORE: Nessun disco fisso rilevato!",
              max_status - 1);
      out_status[max_status - 1] = '\0';
    }
    return false;
  }

  if (!cd_dev) {
    if (out_status && max_status > 0) {
      strncpy(out_status, "ERRORE: Nessuna unita' CD-ROM rilevata!",
              max_status - 1);
      out_status[max_status - 1] = '\0';
    }
    return false;
  }

  // 2. Calcolo intervallo settori da clonare (128 MB = 65536 settori CD =
  // 262144 settori disco)
  const uint32_t TOTAL_CD_SECTORS = 65536;
  uint32_t boot_lba = cd_dev->boot_lba ? cd_dev->boot_lba : 31111;
  uint32_t sectors_per_chunk =
      (TOTAL_CD_SECTORS + total_chunks - 1) / total_chunks;
  uint32_t start_cd = chunk_idx * sectors_per_chunk;
  uint32_t count_cd = sectors_per_chunk;
  if (start_cd + count_cd > TOTAL_CD_SECTORS) {
    count_cd = TOTAL_CD_SECTORS - start_cd;
  }

  // 3. Buffer di trasferimento da 32 KB (16 settori CD = 64 settori Hard Disk)
  static uint8_t s_transfer_buf[32768] __attribute__((aligned(16)));
  for (uint32_t s = 0; s < count_cd; s += 16) {
    uint32_t batch_cd = (s + 16 <= count_cd) ? 16 : (count_cd - s);
    uint32_t cd_sec = boot_lba + start_cd + s;
    if (!atapi_read_cd_sectors(cd_dev, cd_sec, batch_cd, s_transfer_buf)) {
      if (out_status && max_status > 0) {
        strncpy(out_status, "ERRORE: Lettura dal CD-ROM fallita!",
                max_status - 1);
        out_status[max_status - 1] = '\0';
      }
      return false;
    }

    uint64_t disk_lba = (uint64_t)(start_cd + s) * 4;
    uint32_t disk_count = batch_cd * 4;
    if (!ata_write_device_sectors(hd_dev, disk_lba, disk_count,
                                  s_transfer_buf)) {
      if (out_status && max_status > 0) {
        strncpy(out_status, "ERRORE: Scrittura su disco fisso fallita!",
                max_status - 1);
        out_status[max_status - 1] = '\0';
      }
      return false;
    }
  }

  // 4. Aggiorna stato descrittivo per la UI in base alla percentuale attuale
  if (out_status && max_status > 0) {
    uint32_t pct = ((chunk_idx + 1) * 100) / total_chunks;
    if (pct <= 2) {
      strncpy(out_status, "C:\\MasterBootRecord (LBA 0)...", max_status - 1);
    } else if (pct <= 4) {
      strncpy(out_status, "C:\\Boot\\stage2.bin (Stage 2 Bootloader)...",
              max_status - 1);
    } else if (pct <= 7) {
      strncpy(out_status, "C:\\NewOS\\Boot\\kernel.bin (Kernel 64-bit Core)...",
              max_status - 1);
    } else if (pct <= 9) {
      strncpy(out_status, "C:\\NewOS\\SystemLibrary\\ICONS.DLL...",
              max_status - 1);
    } else if (pct <= 13) {
      strncpy(out_status, "C:\\NewOS (Inizializzazione VBR e exFAT FAT)...",
              max_status - 1);
    } else if (pct <= 20) {
      strncpy(out_status, "C:\\NewOS\\SystemProgram\\BOOTSCR.MOD...",
              max_status - 1);
    } else if (pct <= 25) {
      strncpy(out_status, "C:\\NewOS\\SystemProgram\\LOGIN.MOD...",
              max_status - 1);
    } else if (pct <= 30) {
      strncpy(out_status, "C:\\NewOS\\SystemProgram\\EXPLORER.EXE...",
              max_status - 1);
    } else if (pct <= 35) {
      strncpy(out_status, "C:\\NewOS\\Driver\\VGA.SYS & PCI.SYS...",
              max_status - 1);
    } else if (pct <= 40) {
      strncpy(out_status, "C:\\NewOS\\Driver\\ATA.SYS & AHCI.SYS...",
              max_status - 1);
    } else if (pct <= 45) {
      strncpy(out_status, "C:\\NewOS\\Driver\\AC97.SYS & SOUND.SYS...",
              max_status - 1);
    } else if (pct <= 50) {
      strncpy(out_status, "C:\\NewOS\\Driver\\NET\\E1000.SYS & RTL8139.SYS...",
              max_status - 1);
    } else if (pct <= 55) {
      strncpy(out_status, "C:\\NewOS\\SystemLibrary\\SHELL32.DLL...",
              max_status - 1);
    } else if (pct <= 60) {
      strncpy(out_status, "C:\\NewOS\\SystemLibrary\\USER32.DLL...",
              max_status - 1);
    } else if (pct <= 65) {
      strncpy(out_status, "C:\\NewOS\\Resources\\Wallpapers\\HERO.BMP...",
              max_status - 1);
    } else if (pct <= 70) {
      strncpy(out_status, "C:\\NewOS\\Resources\\Wallpapers\\FLOW.BMP...",
              max_status - 1);
    } else if (pct <= 75) {
      strncpy(out_status, "C:\\NewOS\\Resources\\Sounds\\STARTUP.WAV...",
              max_status - 1);
    } else if (pct <= 80) {
      strncpy(out_status, "C:\\NewOS\\Themes\\THEMES.INI & LANGUAGE.INI...",
              max_status - 1);
    } else if (pct <= 85) {
      strncpy(out_status,
              "C:\\NewOS\\SystemProgram\\TASKMGR.EXE & REGEDIT.EXE...",
              max_status - 1);
    } else if (pct <= 90) {
      strncpy(out_status, "C:\\User\\Morgan\\Desktop\\ (Struttura Utente)...",
              max_status - 1);
    } else if (pct <= 95) {
      strncpy(out_status, "C:\\ProgramFile\\ (Cartella Software e App)...",
              max_status - 1);
    } else if (pct < 100) {
      strncpy(out_status, "C:\\EFI\\BOOT\\BOOTX64.EFI (UEFI Dual-Boot ESP)...",
              max_status - 1);
    } else {
      strncpy(out_status, "Installazione completata e disco sincronizzato!",
              max_status - 1);
    }
    out_status[max_status - 1] = '\0';
  }

  // 5. Se completato l'ultimo chunk, aggiorna lo stato dell'unità nel kernel
  if (chunk_idx == total_chunks - 1) {
    ata_device_t *mut_hd = (ata_device_t *)hd_dev;
    mut_hd->has_mbr = true;
    mut_hd->part_type = 0x07;
    mut_hd->part_lba = 2048;
    mut_hd->part_sectors = 250000;
    strncpy(mut_hd->part_fs, "exFAT", sizeof(mut_hd->part_fs) - 1);
  }

  return true;
}

void screen_mgr_init(void) {
  if (g_api_initialized)
    return;

  serial_log("[SCREEN_MGR] Inizializzazione Kernel Screen Manager e Tabella "
             "API Protetta...\n");
  icons_init();

  memset(&g_kernel_api, 0, sizeof(kernel_api_t));
  g_kernel_api.api_version = 0x00010000;

  // Video & Grafica
  g_kernel_api.gfx_get_width = gfx_get_width;
  g_kernel_api.gfx_get_height = gfx_get_height;
  g_kernel_api.gfx_flip = gfx_flip;
  g_kernel_api.gfx_clear = gfx_clear;
  g_kernel_api.gfx_fill_rect = gfx_fill_rect;
  g_kernel_api.gfx_draw_rect = gfx_draw_rect;
  g_kernel_api.gfx_draw_bevel_rect = gfx_draw_bevel_rect;
  g_kernel_api.gfx_draw_gradient_h = gfx_draw_gradient_h;
  g_kernel_api.gfx_draw_gradient_v = gfx_draw_gradient_v;
  g_kernel_api.gfx_draw_string = gfx_draw_string;
  g_kernel_api.gfx_draw_char = gfx_draw_char;
  g_kernel_api.gfx_draw_cursor = gfx_draw_cursor;
  g_kernel_api.gfx_draw_windows_logo = gfx_draw_windows_logo;
  g_kernel_api.gfx_draw_icon_32 =
      (void (*)(int32_t, int32_t, int))gfx_draw_icon_32;
  g_kernel_api.gfx_draw_icon_16 =
      (void (*)(int32_t, int32_t, int))gfx_draw_icon_16;
  g_kernel_api.gfx_draw_image = gfx_draw_image;
  g_kernel_api.gfx_draw_pixel = gfx_draw_pixel;
  g_kernel_api.gfx_get_pixel = gfx_get_pixel;
  g_kernel_api.gfx_fill_alpha = gfx_fill_alpha;
  g_kernel_api.gfx_fill_rounded_rect = gfx_fill_rounded_rect;
  g_kernel_api.gfx_draw_rounded_rect = gfx_draw_rounded_rect;
  g_kernel_api.gfx_draw_specular_rim = gfx_draw_specular_rim;
  g_kernel_api.gfx_draw_drop_shadow = gfx_draw_drop_shadow;

  // Immagini
  g_kernel_api.image_load_from_file = image_load_from_file;
  g_kernel_api.image_free = image_free;

  // Input
  g_kernel_api.mouse_get_state = mouse_get_state;
  g_kernel_api.keyboard_has_char = keyboard_has_char;
  g_kernel_api.keyboard_get_char = keyboard_get_char;

  // Timer & Log
  g_kernel_api.timer_get_ticks = timer_get_ticks;
  g_kernel_api.timer_sleep = timer_sleep;
  g_kernel_api.serial_puts = serial_log;

  // Profilo Utente
  g_kernel_api.user_get_current = user_get_current;
  g_kernel_api.user_set_profile = user_set_profile;
  g_kernel_api.user_save_config = user_save_config;

  // Power
  g_kernel_api.power_shutdown = power_shutdown;
  g_kernel_api.power_reboot = power_reboot;

  // Audio
  g_kernel_api.sound_play = (void (*)(int))sound_play;
  g_kernel_api.sound_play_wav_file = sound_play_wav_file;

  // VFS & Memoria
  g_kernel_api.vfs_read_entire_file =
      (uint8_t *(*)(const char *, uint32_t *))vfs_read_entire_file;
  g_kernel_api.vfs_write_entire_file_inplace = vfs_write_entire_file_inplace;
  g_kernel_api.vfs_readdir =
      (int32_t (*)(const char *, void *, int32_t))vfs_readdir;
  g_kernel_api.vfs_exists = vfs_exists;
  g_kernel_api.vfs_create = vfs_create;
  g_kernel_api.registry_set_read_only = registry_set_read_only;
  g_kernel_api.registry_set_value_sz = registry_set_value_sz;
  g_kernel_api.registry_set_value_dword = registry_set_value_dword;
  g_kernel_api.registry_save_to_disk = registry_save_to_disk;
  g_kernel_api.kmalloc = kmalloc;
  g_kernel_api.kfree = kfree;
  g_kernel_api.wm_destroy_window = wm_destroy_window;
  g_kernel_api.app_explorer_open_folder = app_explorer_open_folder;
  g_kernel_api.registry_is_read_only = registry_is_read_only;

  // Installazione fisica e dischi
  g_kernel_api.disk_install_chunk = disk_install_chunk;
  g_kernel_api.disk_get_info = disk_get_info;

  // Rete e Diagnostica
  g_kernel_api.net_nic_get_active = (void *(*)(void))net_nic_get_active;
  g_kernel_api.net_format_mac = net_format_mac;
  g_kernel_api.net_format_ip = net_format_ip;
  g_kernel_api.net_parse_ip = net_parse_ip;
  g_kernel_api.dhcp_discover = dhcp_discover;
  g_kernel_api.net_nic_poll = net_nic_poll;
  g_kernel_api.icmp_ping_ip =
      (bool (*)(uint32_t, uint32_t, void *, void *))icmp_ping_ip;

  // Estensioni Sistema, RTC, Profilo Utente e UI Temizzata per Applet .DLL
  g_kernel_api.rtc_get_time = (void (*)(void *))rtc_get_time;
  g_kernel_api.user_create_or_migrate = user_create_or_migrate;
  g_kernel_api.registry_get_value_sz = registry_get_value_sz;
  g_kernel_api.registry_get_value_dword = registry_get_value_dword;
  g_kernel_api.wm_get_current_theme_props =
      (const void *(*)(void))wm_get_current_theme_props;
  g_kernel_api.wm_get_theme = wm_get_theme;
  g_kernel_api.wm_draw_themed_button = wm_draw_themed_button;
  g_kernel_api.wm_draw_themed_tab = wm_draw_themed_tab;
  g_kernel_api.gfx_fill_rounded_alpha = gfx_fill_rounded_alpha;

  // VFS Avanzato, Appunti Globali (Clipboard) e Socket TCP per COMDLG.DLL & FTP Stack
  g_kernel_api.vfs_unlink = vfs_unlink;
  g_kernel_api.vfs_rename = vfs_rename;
  g_kernel_api.vfs_mkdir = vfs_mkdir;
  g_kernel_api.clipboard_set_text = clipboard_set_text;
  g_kernel_api.clipboard_get_text = clipboard_get_text;
  g_kernel_api.clipboard_push_file = clipboard_push_file;
  g_kernel_api.clipboard_get_file_count = clipboard_get_file_count;
  g_kernel_api.clipboard_get_file = clipboard_get_file;
  g_kernel_api.clipboard_clear_files = clipboard_clear_files;
  g_kernel_api.tcp_create_conn = (void *(*)(void))tcp_create_conn;
  g_kernel_api.tcp_connect =
      (bool (*)(void *, uint32_t, uint16_t))tcp_connect;
  g_kernel_api.tcp_send =
      (int (*)(void *, const void *, uint16_t))tcp_send;
  g_kernel_api.tcp_recv =
      (int (*)(void *, void *, uint16_t))tcp_recv;
  g_kernel_api.tcp_close = (void (*)(void *))tcp_close;
  g_kernel_api.tcp_destroy_conn = (void (*)(void *))tcp_destroy_conn;

  // Streaming VFS a blocchi e telemetria bus hardware reale
  g_kernel_api.vfs_open = (void *(*)(const char *))vfs_open;
  g_kernel_api.vfs_read = (int32_t (*)(void *, void *, uint32_t))vfs_read;
  g_kernel_api.vfs_write =
      (int32_t (*)(void *, const void *, uint32_t))vfs_write;
  g_kernel_api.vfs_close = (void (*)(void *))vfs_close;
  g_kernel_api.vfs_file_size = vfs_file_size;
  g_kernel_api.vfs_flush = vfs_flush;
  g_kernel_api.vfs_get_drive_bus_info = vfs_get_drive_bus_info;
  g_kernel_api.wm_create_window =
      (void *(*)(const char *, int32_t, int32_t, int32_t, int32_t, int))wm_create_window;
  g_kernel_api.app_control_panel_open = app_control_panel_open;
  g_kernel_api.net_socket = net_socket;
  g_kernel_api.net_connect =
      (int (*)(int, const void *))net_connect;
  g_kernel_api.net_send = net_send;
  g_kernel_api.net_recv = net_recv;
  g_kernel_api.net_close = net_close;
  g_kernel_api.net_poll = net_poll;
  g_kernel_api.dns_resolve = dns_resolve;
  g_kernel_api.screen_mgr_verify_module = screen_mgr_verify_module;
  g_kernel_api.fontmgr_get_ttf_glyph = fontmgr_get_ttf_glyph;
  g_kernel_api.nettls_http_get =
      (bool (*)(const char *, uint32_t, void *))nettls_http_get;
  g_kernel_api.nettls_free_response =
      (void (*)(void *))nettls_free_response;

  memset(&g_kernel_ext_api, 0, sizeof(g_kernel_ext_api));
  g_kernel_ext_api.wm_focus_window = wm_focus_window;
  g_kernel_ext_api.wm_set_background_type = wm_set_background_type;
  g_kernel_ext_api.comdlg_open_file_dialog = (bool (*)(const char *, void *, void *))comdlg_open_file_dialog;
  g_kernel_ext_api.comdlg_save_file_dialog = (bool (*)(const char *, const char *, void *, void *))comdlg_save_file_dialog;
  g_kernel_ext_api.app_comdlg_copy_move_launch = (void (*)(int, const char *, const char *))app_comdlg_copy_move_launch;
  g_kernel_ext_api.app_comdlg_ftp_stack_launch = app_comdlg_ftp_stack_launch;
  g_kernel_ext_api.app_sound_props_launch = app_sound_props_launch;
  g_kernel_ext_api.app_display_props_launch = app_display_props_launch;
  g_kernel_ext_api.app_power_props_launch = app_power_props_launch;
  g_kernel_ext_api.app_mouse_props_launch = app_mouse_props_launch;
  g_kernel_ext_api.app_devmgmt_launch = app_devmgmt_launch;
  g_kernel_ext_api.app_about_launch = app_about_launch;
  g_kernel_ext_api.app_user_accounts_launch = app_user_accounts_launch;
  g_kernel_ext_api.app_net_props_launch = app_net_props_launch;
  g_kernel_ext_api.app_timedate_props_launch = app_timedate_props_launch;
  g_kernel_ext_api.app_keyboard_props_launch = app_keyboard_props_launch;
  g_kernel_ext_api.app_browser_launch = app_browser_launch;
  g_kernel_ext_api.app_browser_open_url = app_browser_open_url;
  g_kernel_ext_api.app_explorer_open_this_pc = app_explorer_open_this_pc;
  g_kernel_ext_api.app_explorer_open_drive_c = app_explorer_open_drive_c;
  g_kernel_ext_api.app_explorer_open_drive_u = app_explorer_open_drive_u;
  g_kernel_ext_api.app_explorer_open_drive_d = app_explorer_open_drive_d;
  g_kernel_ext_api.app_explorer_open_recycle_bin = app_explorer_open_recycle_bin;
  g_kernel_ext_api.app_recycle_quota_prompt_launch = app_recycle_quota_prompt_launch;
  g_kernel_ext_api.app_recycle_settings_launch = app_recycle_settings_launch;
  g_kernel_ext_api.app_oobe_launch = app_oobe_launch;
  g_kernel_ext_api.ac97_is_available = ac97_is_available;
  g_kernel_ext_api.ac97_set_volume = ac97_set_volume;
  g_kernel_ext_api.ac97_stop = ac97_stop;
  g_kernel_ext_api.sound_stop = sound_stop;
  g_kernel_ext_api.sound_set_volume = sound_set_volume;
  g_kernel_ext_api.smp_get_info = (void *(*)(void))smp_get_info;
  g_kernel_ext_api.cpu_get_info = (void *(*)(void))cpu_get_info;
  g_kernel_ext_api.sched_get_task = (void *(*)(int))sched_get_task;
  g_kernel_ext_api.sched_state_to_str = (const char *(*)(int))sched_state_to_str;
  g_kernel_ext_api.sched_get_task_count = sched_get_task_count;
  g_kernel_ext_api.task_kill = task_kill;
  g_kernel_ext_api.ipc_pending_msg_count = ipc_pending_msg_count;
  g_kernel_ext_api.paging_get_stats = (void (*)(void *))paging_get_stats;
  g_kernel_ext_api.power_enter_screen_standby = power_enter_screen_standby;
  g_kernel_ext_api.power_get_status = power_get_status;
  g_kernel_ext_api.registry_get_log_count = registry_get_log_count;
  g_kernel_ext_api.registry_get_log_line = registry_get_log_line;
  g_kernel_ext_api.registry_get_root_count = registry_get_root_count;
  g_kernel_ext_api.registry_get_root = (void *(*)(int))registry_get_root;
  g_kernel_ext_api.registry_get_key = (void *(*)(const char *))registry_get_key;
  g_kernel_ext_api.registry_get_risk_string = (const char *(*)(int))registry_get_risk_string;
  g_kernel_ext_api.registry_get_risk_level = (int (*)(const char *, const char *))registry_get_risk_level;
  g_kernel_ext_api.vfs_get_drive_info = (bool (*)(char, void *))vfs_get_drive_info;
  g_kernel_ext_api.vfs_list_drives = vfs_list_drives;
  g_kernel_ext_api.vfs_wildcard_match = vfs_wildcard_match;
  g_kernel_ext_api.vfs_mount_drive = (bool (*)(char, const char *, const char *, uint32_t, uint64_t, bool, void *))vfs_mount_drive;
  g_kernel_ext_api.vfs_seek = (int64_t (*)(void *, int64_t, int))vfs_seek;
  g_kernel_ext_api.vfs_is_protected = vfs_is_protected;
  g_kernel_ext_api.exfat_check_boot_integrity = (bool (*)(void *))exfat_check_boot_integrity;
  g_kernel_ext_api.exfat_get_used_mb = exfat_get_used_mb;
  g_kernel_ext_api.exfat_get_allocated_clusters = exfat_get_allocated_clusters;
  g_kernel_ext_api.exfat_get_free_clusters = exfat_get_free_clusters;
  g_kernel_ext_api.driver_mgr_get_count = driver_mgr_get_count;
  g_kernel_ext_api.driver_mgr_get_driver = (void *(*)(int))driver_mgr_get_driver;
  g_kernel_ext_api.driver_mgr_status_to_str = (const char *(*)(int))driver_mgr_status_to_str;
  g_kernel_ext_api.xhci_is_active = xhci_is_active;
  g_kernel_ext_api.xhci_get_controller = (void *(*)(void))xhci_get_controller;
  g_kernel_ext_api.uhci_is_active = uhci_is_active;
  g_kernel_ext_api.uhci_get_controller = (void *(*)(void))uhci_get_controller;
  g_kernel_ext_api.ohci_is_active = ohci_is_active;
  g_kernel_ext_api.ohci_get_controller = (void *(*)(void))ohci_get_controller;
  g_kernel_ext_api.usb_get_device_count = usb_get_device_count;
  g_kernel_ext_api.usb_get_device = (void *(*)(int))usb_get_device;
  g_kernel_ext_api.usb_controller_type_to_str = (const char *(*)(int))usb_controller_type_to_str;
  g_kernel_ext_api.usb_speed_to_str = (const char *(*)(int))usb_speed_to_str;
  g_kernel_ext_api.usb_class_to_str = usb_class_to_str;
  g_kernel_ext_api.wifi_scan = (int (*)(void *, int))wifi_scan;
  g_kernel_ext_api.wifi_connect = wifi_connect;
  g_kernel_ext_api.wifi_disconnect = wifi_disconnect;
  g_kernel_ext_api.wifi_get_info = (bool (*)(void *))wifi_get_info;
  g_kernel_ext_api.wifi_set_hardware_cap = wifi_set_hardware_cap;
  g_kernel_ext_api.wifi_bus_to_str = (const char *(*)(int))wifi_bus_to_str;
  g_kernel_ext_api.wifi_band_to_str = (const char *(*)(int))wifi_band_to_str;
  g_kernel_ext_api.vga_is_bga_supported = vga_is_bga_supported;
  g_kernel_ext_api.vga_get_mode_count = vga_get_mode_count;
  g_kernel_ext_api.vga_get_mode_info = (void *(*)(int))vga_get_mode_info;
  g_kernel_ext_api.vga_set_mode_index = vga_set_mode_index;
  g_kernel_ext_api.vga_set_resolution = vga_set_resolution;
  g_kernel_ext_api.dhcp_get_state = (int (*)(void))dhcp_get_state;
  g_kernel_ext_api.dhcp_state_to_str = (const char *(*)(int))dhcp_state_to_str;
  g_kernel_ext_api.tcp_get_connections_count = tcp_get_connections_count;
  g_kernel_ext_api.tcp_get_connection = (void *(*)(int))tcp_get_connection;
  g_kernel_ext_api.tcp_state_to_str = (const char *(*)(int))tcp_state_to_str;
  g_kernel_ext_api.kernel_get_boot_info = (void *(*)(void))kernel_get_boot_info;
  g_kernel_ext_api.ata_get_primary_master = (void *(*)(void))ata_get_primary_master;
  g_kernel_ext_api.ata_get_device_count = ata_get_device_count;
  g_kernel_ext_api.ac97_get_device_name = ac97_get_device_name;
  g_kernel_ext_api.hwaccel_is_ready = hwaccel_is_ready;
  g_kernel_ext_api.gfx_get_lfb_phys = gfx_get_lfb_phys;
  g_kernel_ext_api.hwaccel_init = hwaccel_init;
  g_kernel_ext_api.hwaccel_get_info = hwaccel_get_info;
  g_kernel_ext_api.hwaccel_set_scale = hwaccel_set_scale;
  g_kernel_ext_api.wm_get_background_type = wm_get_background_type;
  g_kernel_ext_api.wm_set_theme = wm_set_theme;
  g_kernel_ext_api.vga_get_current_mode_index = vga_get_current_mode_index;
  g_kernel_ext_api.vga_init = vga_init;
  g_kernel_ext_api.theme_mgr_init = theme_mgr_init;
  g_kernel_ext_api.theme_mgr_find_by_id = theme_mgr_find_by_id;
  g_kernel_ext_api.theme_mgr_get = (void *(*)(int))theme_mgr_get;
  g_kernel_ext_api.theme_mgr_get_count = theme_mgr_get_count;
  g_kernel_ext_api.theme_mgr_apply = theme_mgr_apply;
  g_kernel_ext_api.sound_get_volume = sound_get_volume;
  g_kernel_ext_api.sound_get_driver_name = sound_get_driver_name;
  g_kernel_ext_api.sound_is_ac97 = sound_is_ac97;
  g_kernel_ext_api.cursor_mgr_get_active_scheme_index = cursor_mgr_get_active_scheme_index;
  g_kernel_ext_api.cursor_mgr_get_scheme_count = cursor_mgr_get_scheme_count;
  g_kernel_ext_api.cursor_mgr_get_scheme_info = cursor_mgr_get_scheme_info;
  g_kernel_ext_api.cursor_mgr_draw = cursor_mgr_draw;
  g_kernel_ext_api.cursor_mgr_set_active_scheme = cursor_mgr_set_active_scheme;
  g_kernel_ext_api.gfx_draw_mini_monitor = gfx_draw_mini_monitor;
  g_kernel_ext_api.power_get_idle_seconds = power_get_idle_seconds;
  g_kernel_ext_api.power_set_profile = (void (*)(int))power_set_profile;
  g_kernel_ext_api.power_set_screen_timeout = power_set_screen_timeout;
  g_kernel_ext_api.app_notepad_open_file = app_notepad_open_file;
  g_kernel_ext_api.driver_mgr_init = driver_mgr_init;
  g_kernel_ext_api.app_calc_launch = app_calc_launch;
  g_kernel_ext_api.app_cmd_launch = app_cmd_launch;
  g_kernel_ext_api.app_notepad_launch = app_notepad_launch;
  g_kernel_ext_api.app_player_launch = app_player_launch;
  g_kernel_ext_api.app_player_open_file = app_player_open_file;
  g_kernel_ext_api.app_regedit_launch = app_regedit_launch;
  g_kernel_ext_api.app_rules_viewer_launch = app_rules_viewer_launch;
  g_kernel_ext_api.app_rules_viewer_open_file = app_rules_viewer_open_file;
  g_kernel_ext_api.app_taskmgr_launch = app_taskmgr_launch;
  g_kernel_ext_api.ata_get_device = (const void *(*)(int))ata_get_device;
  g_kernel_ext_api.atapi_get_disc_type_name = (const char *(*)(const void *))atapi_get_disc_type_name;
  g_kernel_ext_api.comdlg_exec_file_op = (bool (*)(int, const char *, const char *))comdlg_exec_file_op;
  g_kernel_ext_api.gfx_set_custom_wallpaper = gfx_set_custom_wallpaper;
  g_kernel_ext_api.recycle_empty_bin = recycle_empty_bin;
  g_kernel_ext_api.recycle_get_stats = recycle_get_stats;
  g_kernel_ext_api.recycle_restore_file = recycle_restore_file;
  g_kernel_ext_api.settings_handle_key = (bool (*)(const void *, char, void *))settings_handle_key;
  g_kernel_ext_api.settings_handle_mouse = (int (*)(const void *, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, bool, void *))settings_handle_mouse;
  g_kernel_ext_api.settings_loader_init = settings_loader_init;
  g_kernel_ext_api.settings_render = (void (*)(const void *, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, bool, uint32_t, void *, const void *))settings_render;
  g_kernel_ext_api.theme_mgr_render_script = theme_mgr_render_script;
  g_kernel_ext_api.uistyle_get_active = uistyle_get_active;
  g_kernel_ext_api.uistyle_set_active = uistyle_set_active;
  g_kernel_ext_api.usb_msc_get_device = (const void *(*)(void))usb_msc_get_device;
  g_kernel_ext_api.usb_msc_is_present = usb_msc_is_present;
  g_kernel_ext_api.pci_read32 = pci_read32;
  g_kernel_ext_api.pci_read16 = pci_read16;
  g_kernel_ext_api.pci_read8 = pci_read8;
  g_kernel_ext_api.pci_write32 = pci_write32;
  g_kernel_ext_api.pci_write16 = pci_write16;
  g_kernel_ext_api.pci_find_device = (bool (*)(uint16_t, uint16_t, void *))pci_find_device;
  g_kernel_ext_api.pci_find_class = (bool (*)(uint8_t, uint8_t, void *))pci_find_class;
  g_kernel_ext_api.pci_find_class_prog_if = (bool (*)(uint8_t, uint8_t, uint8_t, void *))pci_find_class_prog_if;
  g_kernel_ext_api.pci_enable_bus_master = (void (*)(void *))pci_enable_bus_master;
  g_kernel_ext_api.ata_read_sectors = ata_read_sectors;
  g_kernel_ext_api.ata_write_sectors = ata_write_sectors;
  g_kernel_ext_api.ata_read_device_sectors = (bool (*)(const void *, uint64_t, uint32_t, void *))ata_read_device_sectors;
  g_kernel_ext_api.ata_write_device_sectors = (bool (*)(const void *, uint64_t, uint32_t, const void *))ata_write_device_sectors;
  g_kernel_ext_api.atapi_read_cd_sector = (bool (*)(const void *, uint32_t, void *))atapi_read_cd_sector;
  g_kernel_ext_api.atapi_read_cd_sectors = (bool (*)(const void *, uint32_t, uint32_t, void *))atapi_read_cd_sectors;
  g_kernel_ext_api.usb_msc_read_sector = usb_msc_read_sector;
  g_kernel_ext_api.usb_msc_write_sector = usb_msc_write_sector;
  g_kernel_ext_api.vfs_register_driver = (bool (*)(const void *))vfs_register_driver;
  g_kernel_ext_api.xhci_init = xhci_init;
  g_kernel_ext_api.usb_find_wireless_device = (void *(*)(void))usb_find_wireless_device;
  g_kernel_ext_api.usb_subsystem_init = usb_subsystem_init;
  g_kernel_ext_api.kmalloc_aligned = kmalloc_aligned;
  g_kernel_ext_api.pci_get_device_count = pci_get_device_count;
  g_kernel_ext_api.pci_get_device = (const void *(*)(int))pci_get_device;
  g_kernel_ext_api.pci_get_bar_addr = (uint64_t (*)(const void *, int))pci_get_bar_addr;
  g_kernel_ext_api.paging_map_mmio = paging_map_mmio;
  g_kernel_ext_api.dma_alloc = dma_alloc;
  g_kernel_ext_api.acpi_get_info = (const void *(*)(void))acpi_get_info;
  g_kernel_ext_api.gpu3d_get_info = (const void *(*)(void))gpu3d_get_info;
  g_kernel_ext_api.nvme_is_present = nvme_is_present;
  g_kernel_ext_api.nvme_get_model = nvme_get_model;
  g_kernel_ext_api.nvme_get_capacity_sectors = nvme_get_capacity_sectors;
  g_kernel_ext_api.launcher_open = launcher_open;
  g_kernel_ext_api.uistyle_open_explorer = uistyle_open_explorer;
  g_kernel_ext_api.app_installer_launch = app_installer_launch;
  g_kernel_ext_api.app_seccpl_launch = app_seccpl_launch;
  extern const void *comctl_get_api(void);
  g_kernel_ext_api.comctl_get_api = comctl_get_api;
  g_kernel_ext_api.keyboard_is_ctrl_pressed = keyboard_is_ctrl_pressed;
  g_kernel_ext_api.fontmgr_set_ttf = fontmgr_set_ttf;
  g_kernel_ext_api.app_fonts_props_launch = app_fonts_props_launch;
  g_kernel_ext_api.app_pkgsvc_launch = app_pkgsvc_launch;
  g_kernel_ext_api.app_ports_props_launch = app_ports_props_launch;
  g_kernel_ext_api.app_bench_props_launch = app_bench_props_launch;
  g_kernel_ext_api.app_imgview_props_launch = app_imgview_props_launch;
  g_kernel_ext_api.wm_set_start_menu_mode = wm_set_start_menu_mode;
  g_kernel_ext_api.wm_get_start_menu_mode = wm_get_start_menu_mode;
  g_kernel_ext_api.dns_resolve = dns_resolve;
  g_kernel_ext_api.tcp_create_conn = (void *(*)(void))tcp_create_conn;
  g_kernel_ext_api.tcp_destroy_conn = (void (*)(void *))tcp_destroy_conn;
  g_kernel_ext_api.tcp_connect = (bool (*)(void *, uint32_t, uint16_t))tcp_connect;
  g_kernel_ext_api.tcp_send = (int (*)(void *, const void *, uint16_t))tcp_send;
  g_kernel_ext_api.tcp_recv = (int (*)(void *, void *, uint16_t))tcp_recv;
  g_kernel_ext_api.tcp_close = (void (*)(void *))tcp_close;
  g_kernel_ext_api.keyboard_set_layout = keyboard_set_layout;
  g_kernel_ext_api.keyboard_get_layout = keyboard_get_layout;
  g_kernel_ext_api.ac97_play_pcm = ac97_play_pcm;
  g_kernel_ext_api.gfx_fill_rounded_glass = gfx_fill_rounded_glass;
  g_kernel_ext_api.nscomp_apply_window_refraction = nscomp_apply_window_refraction;
  g_kernel_ext_api.nsmotion_get_api = nsmotion_get_api;
  g_kernel_ext_api.get_system_service = screen_mgr_get_service;
  g_kernel_ext_api.system_shutdown_integrity_check = screen_mgr_shutdown_integrity_check;
  g_kernel_ext_api.boot_set_reboot_trick = screen_mgr_set_reboot_trick;
  g_kernel_ext_api.recovery_repair_system_pak = screen_mgr_recovery_repair_system_pak;
  g_kernel_ext_api.recovery_reset_boot_flags = screen_mgr_recovery_reset_boot_flags;
  g_kernel_api.ext_api = &g_kernel_ext_api;

  g_api_initialized = true;
  serial_log("[SCREEN_MGR] Tabella API Kernel collegata con successo (76 "
             "Entrypoint).\n");
}

const kernel_api_t *screen_mgr_get_api(void) {
  if (!g_api_initialized)
    screen_mgr_init();
  return &g_kernel_api;
}

bool screen_mgr_verify_module(const void *buffer, uint32_t size,
                              uint32_t expected_type,
                              const char *expected_name) {
  if (!buffer || size < sizeof(module_header_t)) {
    serial_log(
        "[SECURITY ERROR] Buffer modulo nullo o dimensione insufficiente!\n");
    return false;
  }

  const module_header_t *h = (const module_header_t *)buffer;

  // 1. Verifica Magic Signature 'NOSM'
  if (h->magic != MODULE_MAGIC_NEWOS) {
    serial_log(
        "[SECURITY ERROR] Magic Signature modulo non valida! Atteso: NOSM\n");
    return false;
  }

  // 2. Verifica dimensione header
  if (h->header_size != sizeof(module_header_t)) {
    serial_log("[SECURITY ERROR] Dimensione header non coerente!\n");
    return false;
  }

  // 3. Verifica Tipo Modulo
  if (h->module_type != expected_type) {
    serial_log("[SECURITY ERROR] Tipo modulo non corrispondente a quello "
               "richiesto!\n");
    return false;
  }

  // 4. Verifica Firma del Produttore "NEWSOFT"
  if (strncmp(h->signature, MODULE_AUTH_TAG, 7) != 0) {
    serial_log("[SECURITY ERROR] Firma crittografica del produttore non valida "
               "o manomessa!\n");
    return false;
  }

  // 5. Verifica Flag di Protezione
  if (!(h->security_flags & SEC_FLAG_SYSTEM_SIGNED)) {
    serial_log("[SECURITY ERROR] Flag SYSTEM_SIGNED assente: esecuzione non "
               "autorizzata!\n");
    return false;
  }

  // 6. Verifica Integrità tramite Checksum CRC32 del codice
  const uint8_t *payload = (const uint8_t *)buffer + h->entry_offset;
  uint32_t payload_len = size - h->entry_offset;
  uint32_t calc_crc = compute_crc32(payload, payload_len);

  if (h->checksum != 0 && h->checksum != calc_crc) {
    serial_log("[SECURITY ERROR] Checksum CRC32 non corrispondente: il binario "
               "e' stato alterato!\n");
    return false;
  }

  serial_log("[SECURITY OK] Modulo '");
  serial_log(expected_name ? expected_name : h->module_name);
  serial_log("' verificato e autenticato con successo (CRC32: 0x");
  // Stampa hex esadecimale semplice
  char hex[9];
  mini_snprintf(hex, sizeof(hex), "%x", (unsigned int)calc_crc);
  serial_log(hex);
  serial_log(")\n");

  return true;
}

// ------------------------------------------------------------------------------
// Fallback di Sicurezza Integrati nel Kernel (se modulo esterno assente o
// corrotto)
// ------------------------------------------------------------------------------
static bootscreen_result_t bootscreen_fallback(void) {
  serial_log("[BOOTSCREEN] Fallback di sicurezza attivo...\n");
  gfx_clear(0x0A246A);
  gfx_draw_windows_logo(gfx_get_width() / 2 - 18, gfx_get_height() / 2 - 40);
  gfx_draw_string(gfx_get_width() / 2 - 80, gfx_get_height() / 2 + 10,
                  "NewOS V1 64-bit", 0xFFFFFF, 0, true);
  gfx_flip();
  timer_sleep(400);
  return BOOTSCREEN_OK;
}

static loginscreen_result_t loginscreen_fallback(void) {
  serial_log("[LOGINSCREEN] Fallback di sicurezza attivo...\n");
  return LOGIN_SUCCESS;
}

static logoutscreen_result_t logoutscreen_fallback(void) {
  serial_log("[LOGOUTSCREEN] Fallback di sicurezza attivo...\n");
  power_shutdown();
  return LOGOUT_ACTION_SHUTDOWN;
}

// ------------------------------------------------------------------------------
// Esecutori dei Moduli Protetti da File System C:\SYSTEM\SCREENS\
// ------------------------------------------------------------------------------
bootscreen_result_t screen_mgr_run_bootscreen(void) {
  screen_mgr_init();

  const char *paths[] = {"C:\\NewOS\\Boot\\BOOTSCR.MOD",
                         "D:\\NewOS\\Boot\\BOOTSCR.MOD",
                         "C:\\NewOS\\SystemProgram\\BOOTSCR.MOD",
                         "/NewOS/Boot/BOOTSCR.MOD",
                         "/NewOS/SystemProgram/BOOTSCR.MOD",
                         "D:\\BOOTSCR.MOD",
                         "BOOTSCR.MOD"};

  uint8_t *mod_buf = NULL;
  uint32_t mod_size = 0;

  for (int i = 0; i < (int)(sizeof(paths) / sizeof(paths[0])); i++) {
    mod_buf = (uint8_t *)vfs_read_entire_file(paths[i], &mod_size);
    if (mod_buf && mod_size > 0)
      break;
  }

  if (mod_buf && mod_size >= sizeof(module_header_t)) {
    if (screen_mgr_verify_module(mod_buf, mod_size, MODULE_TYPE_BOOTSCREEN,
                                 "BOOTSCR.MOD")) {
      const module_header_t *h = (const module_header_t *)mod_buf;
      bootscreen_entry_fn entry =
          (bootscreen_entry_fn)(mod_buf + h->entry_offset);

      bootscreen_params_t p = {.duration_ms = 1800, .allow_skip = true};
      bootscreen_result_t res = (bootscreen_result_t)entry(&g_kernel_api, &p);

      kfree(mod_buf);
      return res;
    }
    kfree(mod_buf);
  }

  return bootscreen_fallback();
}

loginscreen_result_t screen_mgr_run_loginscreen(void) {
  screen_mgr_init();

  const char *paths[] = {
      "C:\\NewOS\\Boot\\LOGIN.MOD", "C:\\NewOS\\SystemProgram\\LOGIN.MOD",
      "/NewOS/Boot/LOGIN.MOD", "/NewOS/SystemProgram/LOGIN.MOD", "LOGIN.MOD"};

  uint8_t *mod_buf = NULL;
  uint32_t mod_size = 0;

  for (int i = 0; i < (int)(sizeof(paths) / sizeof(paths[0])); i++) {
    mod_buf = (uint8_t *)vfs_read_entire_file(paths[i], &mod_size);
    if (mod_buf && mod_size > 0)
      break;
  }

  if (mod_buf && mod_size >= sizeof(module_header_t)) {
    if (screen_mgr_verify_module(mod_buf, mod_size, MODULE_TYPE_LOGINSCREEN,
                                 "LOGIN.MOD")) {
      const module_header_t *h = (const module_header_t *)mod_buf;
      loginscreen_entry_fn entry =
          (loginscreen_entry_fn)(mod_buf + h->entry_offset);

      user_profile_t *u = user_get_current();
      const char *def_u = (u && u->username[0]) ? u->username : "Morgan";
      loginscreen_params_t p;
      memset(&p, 0, sizeof(p));
      strncpy(p.default_user, def_u, sizeof(p.default_user) - 1);
      p.allow_guest = true;
      loginscreen_result_t res = (loginscreen_result_t)entry(&g_kernel_api, &p);

      kfree(mod_buf);
      return res;
    }
    kfree(mod_buf);
  }

  return loginscreen_fallback();
}

logoutscreen_result_t screen_mgr_run_logoutscreen_mode(logout_mode_t mode) {
  screen_mgr_init();

  const char *paths[] = {"C:\\NewOS\\Boot\\LOGOUT.MOD",
                         "C:\\NewOS\\SystemProgram\\LOGOUT.MOD",
                         "/NewOS/Boot/LOGOUT.MOD",
                         "/NewOS/SystemProgram/LOGOUT.MOD", "LOGOUT.MOD"};

  uint8_t *mod_buf = NULL;
  uint32_t mod_size = 0;

  for (int i = 0; i < (int)(sizeof(paths) / sizeof(paths[0])); i++) {
    mod_buf = (uint8_t *)vfs_read_entire_file(paths[i], &mod_size);
    if (mod_buf && mod_size > 0)
      break;
  }

  if (mod_buf && mod_size >= sizeof(module_header_t)) {
    if (screen_mgr_verify_module(mod_buf, mod_size, MODULE_TYPE_LOGOUTSCREEN,
                                 "LOGOUT.MOD")) {
      const module_header_t *h = (const module_header_t *)mod_buf;
      logoutscreen_entry_fn entry =
          (logoutscreen_entry_fn)(mod_buf + h->entry_offset);

      user_profile_t *u = user_get_current();
      logoutscreen_params_t p = {
          .current_user = (u && u->username[0]) ? u->username : "Morgan",
          .mode = mode};
      logoutscreen_result_t res =
          (logoutscreen_result_t)entry(&g_kernel_api, &p);

      kfree(mod_buf);
      return res;
    }
    kfree(mod_buf);
  }

  return logoutscreen_fallback();
}

logoutscreen_result_t screen_mgr_run_logoutscreen(void) {
  logout_mode_t m = LOGOUT_MODE_SHUTDOWN;
  uint32_t msz = 0;
  char *mbuf = (char *)vfs_read_entire_file("C:\\NewOS\\Boot\\logout_mode.ini", &msz);
  if (mbuf && msz > 0) {
    if (mbuf[0] >= '1' && mbuf[0] <= '5') {
      m = (logout_mode_t)(mbuf[0] - '0');
    }
    kfree(mbuf);
  }
  return screen_mgr_run_logoutscreen_mode(m);
}

static bootsetup_result_t bootsetup_fallback(void) {
  serial_log("[BOOTSETUP ERRORE] Impossibile avviare il modulo di "
             "installazione (BOOTSETUP.MOD non trovato o non valido).\n");
  uint32_t sw = gfx_get_width();
  uint32_t sh = gfx_get_height();
  int32_t w = 540, h = 170;
  int32_t x = (sw - w) / 2, y = (sh - h) / 2;
  gfx_fill_rect(x, y, w, h, COLOR_BTNFACE);
  gfx_draw_bevel_rect(x, y, w, h, false);
  gfx_draw_gradient_h(x + 3, y + 3, w - 6, 20, COLOR_ACTIVECAPTION_LEFT,
                      COLOR_ACTIVECAPTION_RIGHT);
  gfx_draw_string(x + 10, y + 5,
                  "Installazione NewOS V1 - Supporto non trovato",
                  COLOR_CAPTIONTEXT, 0, true);
  gfx_draw_string(x + 20, y + 42,
                  "Impossibile trovare il file di installazione BOOTSETUP.MOD",
                  COLOR_WINDOWTEXT, 0, true);
  gfx_draw_string(
      x + 20, y + 68,
      "Verificare che il supporto di installazione (CD/DVD/USB) sia inserito.",
      0x555555, 0, true);
  gfx_draw_string(x + 20, y + 108, "Premere Invio per riavviare il sistema...",
                  0x880000, 0, true);
  gfx_flip();
  while (1) {
    if (keyboard_has_char()) {
      (void)keyboard_get_char();
      break;
    }
    timer_sleep(50);
  }
  return BOOTSETUP_ERROR;
}

bootsetup_result_t screen_mgr_run_bootsetup(void) {
  screen_mgr_init();

  const char *paths[] = {"D:\\NewOS\\SystemProgram\\BOOTSETUP.MOD",
                         "C:\\NewOS\\SystemProgram\\BOOTSETUP.MOD",
                         "D:\\NewOS\\Boot\\BOOTSETUP.MOD",
                         "D:\\BootSetup\\BOOTSETUP.MOD",
                         "D:\\BOOTSETUP.MOD",
                         "C:\\NewOS\\Boot\\BOOTSETUP.MOD",
                         "C:\\BootSetup\\BOOTSETUP.MOD",
                         "C:\\BOOTSETUP.MOD",
                         "E:\\NewOS\\SystemProgram\\BOOTSETUP.MOD",
                         "E:\\NewOS\\Boot\\BOOTSETUP.MOD",
                         "E:\\BootSetup\\BOOTSETUP.MOD",
                         "U:\\NewOS\\SystemProgram\\BOOTSETUP.MOD",
                         "U:\\NewOS\\Boot\\BOOTSETUP.MOD",
                         "U:\\BootSetup\\BOOTSETUP.MOD",
                         "/NewOS/SystemProgram/BOOTSETUP.MOD",
                         "/NewOS/Boot/BOOTSETUP.MOD",
                         "/BootSetup/BOOTSETUP.MOD",
                         "BOOTSETUP.MOD"};

  uint8_t *mod_buf = NULL;
  uint32_t mod_size = 0;
  int found_idx = -1;

  for (int i = 0; i < (int)(sizeof(paths) / sizeof(paths[0])); i++) {
    mod_buf = (uint8_t *)vfs_read_entire_file(paths[i], &mod_size);
    if (mod_buf && mod_size > 0) {
      found_idx = i;
      break;
    }
  }

  if (mod_buf && mod_size >= sizeof(module_header_t)) {
    if (screen_mgr_verify_module(mod_buf, mod_size, MODULE_TYPE_BOOTSETUP,
                                 "BOOTSETUP.MOD")) {
      const module_header_t *h = (const module_header_t *)mod_buf;
      bootsetup_entry_fn entry =
          (bootsetup_entry_fn)(mod_buf + h->entry_offset);

      char pak_path[64] = "C:\\BootSetup\\SYSTEM.PAK";
      if (found_idx >= 0 && paths[found_idx][1] == ':') {
        pak_path[0] = paths[found_idx][0];
      }

      bootsetup_params_t p = {.package_path = pak_path,
                              .auto_proceed_oobe = true};
      bootsetup_result_t res = (bootsetup_result_t)entry(&g_kernel_api, &p);

      kfree(mod_buf);
      return res;
    }
    kfree(mod_buf);
  }

  return bootsetup_fallback();
}

bool screen_mgr_has_pending_update(void) {
  uint32_t size = 0;
  uint8_t *buf = (uint8_t *)vfs_read_entire_file(
      "C:\\NewOS\\Boot\\boot_action.ini", &size);
  if (!buf || size == 0)
    return false;

  bool has_update = false;
  // Cerca Target=UPDATE.MOD o UPDATE.MOD
  for (uint32_t i = 0; i + 10 < size; i++) {
    if (buf[i] == 'U' && buf[i + 1] == 'P' && buf[i + 2] == 'D' &&
        buf[i + 3] == 'A' && buf[i + 4] == 'T' && buf[i + 5] == 'E' &&
        buf[i + 6] == '.' && buf[i + 7] == 'M' && buf[i + 8] == 'O' &&
        buf[i + 9] == 'D') {
      has_update = true;
      break;
    }
  }

  kfree(buf);
  return has_update;
}

update_result_t screen_mgr_run_update(void) {
  screen_mgr_init();
  serial_log("[SCREEN_MGR] Rilevata azione straordinaria di aggiornamento: "
             "avvio UPDATE.MOD...\n");

  // Watchdog contatore tentativi (Crash Recovery Boot Watchdog)
  uint32_t act_size = 0;
  uint8_t *act_buf = (uint8_t *)vfs_read_entire_file(
      "C:\\NewOS\\Boot\\boot_action.ini", &act_size);
  int attempts = 1;

  if (act_buf && act_size > 0) {
    for (uint32_t i = 0; i + 15 < act_size; i++) {
      if (act_buf[i] == 'A' && act_buf[i + 1] == 't' && act_buf[i + 2] == 't' &&
          act_buf[i + 3] == 'e' && act_buf[i + 4] == 'm' &&
          act_buf[i + 5] == 'p' && act_buf[i + 6] == 't' &&
          act_buf[i + 7] == 's' && act_buf[i + 8] == '=') {
        attempts = act_buf[i + 9] - '0';
        break;
      }
    }
    kfree(act_buf);
  }

  // Se superato il limite di tentativi (MaxAttempts=2), Rollback Automatico di
  // Emergenza
  if (attempts > 2) {
    serial_log("[WATCHDOG CRITICAL] Superato il numero massimo tentativi "
               "update (Attempts > 2)! Rollback di Emergenza.\n");
    const char *fail_log =
        "[ROLLBACK_EMERGENCY]\r\nTimestamp=CrashRecovery\r\nMotivo=Tentativi_"
        "Massimi_Superati\r\nStato=LOGIN.MOD_Ripristinato\r\n";
    vfs_write_entire_file_inplace("C:\\NewOS\\Update\\update_failure.log",
                                  fail_log, 110);
    vfs_write_entire_file_inplace("C:\\NewOS\\Boot\\boot_action.ini", "", 0);
    return UPDATE_ROLLBACK;
  }

  // Incrementa contatore tentativi per il watchdog
  char new_action[96];
  mini_snprintf(
      new_action, sizeof(new_action),
      "[BootAction]\r\nTarget=UPDATE.MOD\r\nAttempts=%d\r\nMaxAttempts=2\r\n",
      attempts + 1);
  vfs_write_entire_file_inplace("C:\\NewOS\\Boot\\boot_action.ini", new_action,
                                (uint32_t)strlen(new_action));

  const char *paths[] = {
      "C:\\NewOS\\Boot\\UPDATE.MOD", "C:\\NewOS\\SystemProgram\\UPDATE.MOD",
      "D:\\NewOS\\Boot\\UPDATE.MOD", "C:\\UPDATE.MOD",
      "/NewOS/Boot/UPDATE.MOD",      "UPDATE.MOD"};

  uint8_t *mod_buf = NULL;
  uint32_t mod_size = 0;

  for (int i = 0; i < (int)(sizeof(paths) / sizeof(paths[0])); i++) {
    mod_buf = (uint8_t *)vfs_read_entire_file(paths[i], &mod_size);
    if (mod_buf && mod_size > 0)
      break;
  }

  if (mod_buf && mod_size >= sizeof(module_header_t)) {
    if (screen_mgr_verify_module(mod_buf, mod_size, MODULE_TYPE_UPDATE,
                                 "UPDATE.MOD")) {
      // Buffer protetto con cuscinetto BSS azzerato (64 KB)
      uint8_t *exec_buf = (uint8_t *)kmalloc(mod_size + 65536);
      if (exec_buf) {
        memcpy(exec_buf, mod_buf, mod_size);
        memset(exec_buf + mod_size, 0, 65536);
        kfree(mod_buf);
        mod_buf = exec_buf;
      }

      const module_header_t *h = (const module_header_t *)mod_buf;
      update_entry_fn entry = (update_entry_fn)(mod_buf + h->entry_offset);

      update_params_t p = {.package_path = "C:\\NewOS\\Update\\UPDATE.PAK",
                           .reg_update_path =
                               "C:\\NewOS\\Update\\registry_update.update",
                           .force_unattended = true};
      update_result_t res = (update_result_t)entry(&g_kernel_api, &p);

      kfree(mod_buf);
      return res;
    }
    kfree(mod_buf);
  }

  serial_log("[SCREEN_MGR ERROR] Impossibile caricare UPDATE.MOD o modulo non "
             "autentico.\n");
  return UPDATE_ERROR;
}

static oobe_result_t oobe_fallback(void) {
  serial_log("[OOBE] Fallback: modulo protetto assente o corrotto, procedura "
             "segnata completata.\n");
  oobe_mark_completed();
  return OOBE_SUCCESS;
}

oobe_result_t screen_mgr_run_oobe(void) {
  screen_mgr_init();

  const char *paths[] = {"C:\\NewOS\\Boot\\OOBE.MOD",
                         "C:\\NewOS\\SystemProgram\\OOBE.MOD",
                         "C:\\NewOS\\OOBE\\OOBE.MOD",
                         "/NewOS/Boot/OOBE.MOD",
                         "/NewOS/SystemProgram/OOBE.MOD",
                         "/NewOS/OOBE/OOBE.MOD",
                         "OOBE.MOD"};

  uint8_t *mod_buf = NULL;
  uint32_t mod_size = 0;

  for (int i = 0; i < (int)(sizeof(paths) / sizeof(paths[0])); i++) {
    mod_buf = (uint8_t *)vfs_read_entire_file(paths[i], &mod_size);
    if (mod_buf && mod_size > 0)
      break;
  }

  if (mod_buf && mod_size >= sizeof(module_header_t)) {
    if (screen_mgr_verify_module(mod_buf, mod_size, MODULE_TYPE_OOBE,
                                 "OOBE.MOD")) {
      // Buffer protetto con cuscinetto BSS azzerato (64 KB)
      uint8_t *exec_buf = (uint8_t *)kmalloc(mod_size + 65536);
      if (exec_buf) {
        memcpy(exec_buf, mod_buf, mod_size);
        memset(exec_buf + mod_size, 0, 65536);
        kfree(mod_buf);
        mod_buf = exec_buf;
      }

      const module_header_t *h = (const module_header_t *)mod_buf;
      oobe_entry_fn entry = (oobe_entry_fn)(mod_buf + h->entry_offset);

      oobe_params_t p = {.allow_skip = true};
      oobe_result_t res = (oobe_result_t)entry(&g_kernel_api, &p);

      kfree(mod_buf);
      return res;
    }
    kfree(mod_buf);
  }

  return oobe_fallback();
}

bool oobe_is_completed(void) {
  uint32_t fsize = 0;
  char *ini = (char *)vfs_read_entire_file("C:\\NewOS\\OOBE\\OOBE.INI", &fsize);
  if (!ini) {
    ini = (char *)vfs_read_entire_file("/NewOS/OOBE/OOBE.INI", &fsize);
  }

  if (ini && fsize > 0) {
    bool comp = (strstr(ini, "Completed=1") != NULL);
    kfree(ini);
    return comp;
  }
  return false;
}

void oobe_mark_completed(void) {
  user_profile_t *u = user_get_current();
  const char *uname = (u && u->username[0]) ? u->username : "Morgan";
  vfs_create("C:\\NewOS\\OOBE\\OOBE.INI");
  char txt[256];
  mini_snprintf(txt, sizeof(txt),
                "[OOBE]\nCompleted=1\nSetupStage=Completed\nLanguage=IT_"
                "IT\nDefaultUser=%s\nAllowSkip=1\n",
                uname);
  vfs_write_entire_file_inplace("C:\\NewOS\\OOBE\\OOBE.INI", txt,
                                (uint32_t)strlen(txt));
}

void app_oobe_launch(void) {
  screen_mgr_run_oobe();
  wm_render();
  gfx_flip();
}

void app_installer_launch(void) {
  serial_log(
      "[APP_INSTALLER] Lancio procedura BootSetup esterna protetta...\n");
  screen_mgr_run_bootsetup();
  wm_render();
  gfx_flip();
}

static recovery_result_t run_recovery_module(const char *const *paths, int num_paths,
                                             uint32_t mod_type, const char *mod_name,
                                             int trigger_source, bool auto_execute) {
  screen_mgr_init();
  uint8_t *mod_buf = NULL;
  uint32_t mod_size = 0;

  for (int i = 0; i < num_paths; i++) {
    mod_buf = (uint8_t *)vfs_read_entire_file(paths[i], &mod_size);
    if (mod_buf && mod_size > 0) break;
  }

  if (mod_buf && mod_size >= sizeof(module_header_t)) {
    if (screen_mgr_verify_module(mod_buf, mod_size, mod_type, mod_name)) {
      const module_header_t *h = (const module_header_t *)mod_buf;
      recovery_entry_fn entry = (recovery_entry_fn)(mod_buf + h->entry_offset);
      recovery_params_t p = {
          .trigger_source = trigger_source,
          .auto_execute = auto_execute
      };
      recovery_result_t res = (recovery_result_t)entry(&g_kernel_api, &p);
      kfree(mod_buf);
      return res;
    }
    kfree(mod_buf);
  }

  serial_log("[RECOVERY] Modulo non trovato o non valido: fallback a RECOVERY_ERROR.\n");
  return RECOVERY_ERROR;
}

recovery_result_t screen_mgr_run_recovery_basic(void) {
  const char *paths[] = {
      "C:\\NewOS\\Restore\\RECBASIC.MOD",
      "/NewOS/Restore/RECBASIC.MOD",
      "C:\\NewOS\\Boot\\RECBASIC.MOD",
      "RECBASIC.MOD"
  };
  return run_recovery_module(paths, 4, MODULE_TYPE_RECOVERY_BASIC, "RECBASIC.MOD", 0, true);
}

recovery_result_t screen_mgr_run_recovery_advanced(void) {
  const char *paths[] = {
      "C:\\NewOS\\Restore\\RECADV.MOD",
      "/NewOS/Restore/RECADV.MOD",
      "C:\\NewOS\\Boot\\RECADV.MOD",
      "RECADV.MOD"
  };
  return run_recovery_module(paths, 4, MODULE_TYPE_RECOVERY_ADV, "RECADV.MOD", 0, false);
}
