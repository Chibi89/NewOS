#include <hwaccel.h>
#include <screen_modules.h>

__attribute__((section(".header"), used)) module_header_t g_hwaccel_header = {
    .magic = MODULE_MAGIC_NEWOS,
    .header_size = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type = MODULE_TYPE_SYSTEM_LIBRARY,
    .code_size = 0,
    .entry_offset = 64,
    .checksum = 0,
    .security_flags =
        SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name = "HWACCEL",
    .signature = MODULE_AUTH_TAG};

static int hwaccel_dispatch(const kernel_api_t *api, hwaccel_params_t *p);

// Entry point garantito all'offset 64 (subito dopo .header)
int module_main(const kernel_api_t *api, hwaccel_params_t *p) {
  return hwaccel_dispatch(api, p);
}

static void dll_strcpy(char *dst, const char *src, size_t max_len) {
  if (!dst || max_len == 0)
    return;
  size_t i = 0;
  while (src && src[i] && i + 1 < max_len) {
    dst[i] = src[i];
    i++;
  }
  dst[i] = '\0';
}

static inline void io_outl(uint16_t port, uint32_t val) {
  __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t io_inl(uint16_t port) {
  uint32_t ret;
  __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
  return ret;
}

static uint32_t pci_cfg_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off) {
  uint32_t addr = (1U << 31) | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                  ((uint32_t)func << 8) | (off & 0xFC);
  io_outl(0xCF8, addr);
  return io_inl(0xCFC);
}

static inline void cpu_cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx,
                             uint32_t *ecx, uint32_t *edx) {
  __asm__ volatile("cpuid"
                   : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                   : "a"(leaf), "c"(0));
}

static inline uint64_t cpu_rdmsr(uint32_t msr) {
  uint32_t lo, hi;
  __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
  return ((uint64_t)hi << 32) | lo;
}

static inline void cpu_wrmsr(uint32_t msr, uint64_t val) {
  uint32_t lo = (uint32_t)(val & 0xFFFFFFFFULL);
  uint32_t hi = (uint32_t)(val >> 32);
  __asm__ volatile("wrmsr" : : "c"(msr), "a"(lo), "d"(hi));
}

static inline uint64_t read_cr3(void) {
  uint64_t val;
  __asm__ volatile("mov %%cr3, %0" : "=r"(val));
  return val;
}

static inline void flush_tlb_all(void) {
  uint64_t cr3 = read_cr3();
  __asm__ volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
}

// Configura IA32_PAT (0x277) -> PA4 = 0x01 (Write-Combining) e applica WC sulle pagine 2MB del LFB
static bool setup_pat_write_combining(uint64_t lfb_base, uint32_t size_bytes) {
  if (lfb_base == 0 || lfb_base >= 0x100000000ULL)
    return false; // Limite identity-map 4GB

  uint32_t eax, ebx, ecx, edx;
  cpu_cpuid(1, &eax, &ebx, &ecx, &edx);
  if (!(edx & (1U << 16))) // Bit 16 = PAT
    return false;

  // Imposta PA4 (bit 32..39) a 0x01 (WC)
  uint64_t pat = cpu_rdmsr(0x277);
  pat = (pat & ~(0xFFULL << 32)) | (0x01ULL << 32);
  cpu_wrmsr(0x277, pat);

  uint64_t pml4_phys = read_cr3() & 0x000FFFFFFFFFF000ULL;
  uint64_t *pml4 = (uint64_t *)(uintptr_t)pml4_phys;

  uint64_t start = lfb_base & ~0x1FFFFFULL;
  uint64_t end = (lfb_base + size_bytes + 0x1FFFFFULL) & ~0x1FFFFFULL;
  if (end > 0x100000000ULL)
    end = 0x100000000ULL;

  bool patched = false;
  for (uint64_t addr = start; addr < end; addr += 0x200000ULL) {
    uint64_t pml4_i = (addr >> 39) & 0x1FF;
    uint64_t pdpt_i = (addr >> 30) & 0x1FF;
    uint64_t pd_i   = (addr >> 21) & 0x1FF;

    if (!(pml4[pml4_i] & 1ULL))
      continue;
    uint64_t *pdpt = (uint64_t *)(uintptr_t)(pml4[pml4_i] & 0x000FFFFFFFFFF000ULL);
    if (!(pdpt[pdpt_i] & 1ULL))
      continue;
    uint64_t *pd = (uint64_t *)(uintptr_t)(pdpt[pdpt_i] & 0x000FFFFFFFFFF000ULL);
    uint64_t pde = pd[pd_i];
    if ((pde & 1ULL) && (pde & (1ULL << 7))) { // Present & 2MB Page Size
      // PA4 = PAT(bit 12)=1, PCD(bit 4)=0, PWT(bit 3)=0
      pde &= ~((1ULL << 3) | (1ULL << 4));
      pde |= (1ULL << 12);
      pd[pd_i] = pde;
      patched = true;
    }
  }

  if (patched) {
    __asm__ volatile("wbinvd" ::: "memory");
    flush_tlb_all();
  }
  return patched;
}

static void detect_pci_gpu(hwaccel_info_t *info) {
  info->pci_vendor = 0;
  info->pci_device = 0;
  info->vram_mb = 16;
  dll_strcpy(info->gpu_name, "Standard VESA / GOP Display", sizeof(info->gpu_name));

  for (uint16_t bus = 0; bus < 8; bus++) {
    for (uint8_t slot = 0; slot < 32; slot++) {
      uint32_t id = pci_cfg_read32((uint8_t)bus, slot, 0, 0x00);
      if ((id & 0xFFFF) == 0xFFFF)
        continue;
      uint32_t cls = pci_cfg_read32((uint8_t)bus, slot, 0, 0x08);
      uint8_t base_class = (uint8_t)((cls >> 24) & 0xFF);
      if (base_class == 0x03) { // Display Controller
        uint16_t vid = (uint16_t)(id & 0xFFFF);
        uint16_t did = (uint16_t)((id >> 16) & 0xFFFF);
        info->pci_vendor = vid;
        info->pci_device = did;

        if (vid == 0x1234 && did == 0x1111) {
          dll_strcpy(info->gpu_name, "QEMU/Bochs BGA Hardware VGA (PCI 1234:1111)", sizeof(info->gpu_name));
          info->vram_mb = 16;
        } else if (vid == 0x8086) {
          dll_strcpy(info->gpu_name, "Intel HD / UHD / Iris Graphics (PCI 8086)", sizeof(info->gpu_name));
          info->vram_mb = 64;
        } else if (vid == 0x1002) {
          dll_strcpy(info->gpu_name, "AMD Radeon Graphics Adapter (PCI 1002)", sizeof(info->gpu_name));
          info->vram_mb = 64;
        } else if (vid == 0x10DE) {
          dll_strcpy(info->gpu_name, "NVIDIA GeForce / Quadro GPU (PCI 10DE)", sizeof(info->gpu_name));
          info->vram_mb = 64;
        } else if (vid == 0x15AD) {
          dll_strcpy(info->gpu_name, "VMware SVGA II 3D Adapter (PCI 15AD)", sizeof(info->gpu_name));
          info->vram_mb = 32;
        } else if (vid == 0x1AF4) {
          dll_strcpy(info->gpu_name, "VirtIO-GPU Paravirtual Display (PCI 1AF4)", sizeof(info->gpu_name));
          info->vram_mb = 32;
        } else if (vid == 0x80EE) {
          dll_strcpy(info->gpu_name, "VirtualBox Graphics Adapter (PCI 80EE)", sizeof(info->gpu_name));
          info->vram_mb = 32;
        } else {
          dll_strcpy(info->gpu_name, "PCI Display Controller (Class 0x03)", sizeof(info->gpu_name));
        }
        return;
      }
    }
  }
}

static uint32_t dpi_to_scale_percent(uint32_t dpi) {
  if (dpi < 110) return 100;
  if (dpi < 134) return 125;
  if (dpi < 160) return 150;
  if (dpi < 184) return 175;
  return 200;
}

static void parse_edid_and_dpi(hwaccel_info_t *info, uint32_t width, uint32_t height) {
  const uint8_t *edid = (const uint8_t *)(uintptr_t)0x9100ULL;
  info->edid_valid = false;
  info->phys_width_mm = 0;
  info->phys_height_mm = 0;
  dll_strcpy(info->monitor_name, "Generic PnP Display", sizeof(info->monitor_name));

  // Verifica header VESA EDID 1.x: 00 FF FF FF FF FF FF 00
  if (edid[0] == 0x00 && edid[1] == 0xFF && edid[2] == 0xFF && edid[3] == 0xFF &&
      edid[4] == 0xFF && edid[5] == 0xFF && edid[6] == 0xFF && edid[7] == 0x00) {
    uint32_t sum = 0;
    for (int i = 0; i < 128; i++)
      sum += edid[i];
    if ((sum & 0xFF) == 0) {
      info->edid_valid = true;
      uint8_t w_cm = edid[21];
      uint8_t h_cm = edid[22];
      if (w_cm > 0 && h_cm > 0) {
        info->phys_width_mm = (uint32_t)w_cm * 10;
        info->phys_height_mm = (uint32_t)h_cm * 10;
        // DPI = width * 25.4 / width_mm
        uint32_t dpi = (width * 254U + (info->phys_width_mm / 2)) / info->phys_width_mm;
        if (dpi < 72) dpi = 96;
        if (dpi > 300) dpi = 300;
        info->detected_dpi = dpi;
      }

      // Cerca nome monitor nei 4 descrittori da 18 byte (offset 54, 72, 90, 108)
      for (int d = 0; d < 4; d++) {
        const uint8_t *desc = edid + 54 + d * 18;
        if (desc[0] == 0 && desc[1] == 0 && desc[2] == 0 && desc[3] == 0xFC) {
          int k = 0;
          for (int j = 5; j < 18 && k < 28; j++) {
            uint8_t c = desc[j];
            if (c == '\n' || c == '\r' || c == 0)
              break;
            info->monitor_name[k++] = (char)c;
          }
          info->monitor_name[k] = '\0';
          break;
        }
      }
    }
  }

  if (!info->edid_valid || info->phys_width_mm == 0) {
    // Fallback euristico basato su risoluzione corrente (per VM / monitor senza DDC)
    (void)height;
    if (width <= 1280)      info->detected_dpi = 96;
    else if (width <= 1600) info->detected_dpi = 108;
    else if (width <= 1920) info->detected_dpi = 120;
    else if (width <= 2560) info->detected_dpi = 144;
    else                    info->detected_dpi = 192;
  }

  if (info->scale_mode == 0) {
    if (width <= 1280 && height <= 800) {
      info->scale_percent = 100;
    } else {
      info->scale_percent = dpi_to_scale_percent(info->detected_dpi);
    }
  }
}

// Copia veloce 32-bit accelerata con SSE2 non-temporal stores (movntdq + sfence)
static void sse2_blit_32(void *dst_ptr, const void *src_ptr, uint32_t num_pixels) {
  uint32_t *dst = (uint32_t *)dst_ptr;
  const uint32_t *src = (const uint32_t *)src_ptr;

  // Allinea dst a 16 byte
  while (num_pixels > 0 && (((uintptr_t)dst) & 15U) != 0) {
    *dst++ = *src++;
    num_pixels--;
  }

  // Blocco SSE2 streaming: 16 pixel (64 byte) per iterazione
  while (num_pixels >= 16) {
    __asm__ volatile(
        "movdqu   (%1), %%xmm0\n\t"
        "movdqu 16(%1), %%xmm1\n\t"
        "movdqu 32(%1), %%xmm2\n\t"
        "movdqu 48(%1), %%xmm3\n\t"
        "movntdq %%xmm0,   (%0)\n\t"
        "movntdq %%xmm1, 16(%0)\n\t"
        "movntdq %%xmm2, 32(%0)\n\t"
        "movntdq %%xmm3, 48(%0)\n\t"
        :
        : "r"(dst), "r"(src)
        : "xmm0", "xmm1", "xmm2", "xmm3", "memory");
    dst += 16;
    src += 16;
    num_pixels -= 16;
  }

  while (num_pixels >= 4) {
    __asm__ volatile(
        "movdqu (%1), %%xmm0\n\t"
        "movntdq %%xmm0, (%0)\n\t"
        :
        : "r"(dst), "r"(src)
        : "xmm0", "memory");
    dst += 4;
    src += 4;
    num_pixels -= 4;
  }

  __asm__ volatile("sfence" ::: "memory");

  while (num_pixels > 0) {
    *dst++ = *src++;
    num_pixels--;
  }
}

// Fill veloce 32-bit accelerato con SSE2 (movntdq + sfence)
static void sse2_fill_32(void *dst_ptr, uint32_t color, uint32_t num_pixels) {
  uint32_t *dst = (uint32_t *)dst_ptr;

  while (num_pixels > 0 && (((uintptr_t)dst) & 15U) != 0) {
    *dst++ = color;
    num_pixels--;
  }

  if (num_pixels >= 4) {
    uint32_t vec[4] __attribute__((aligned(16))) = {color, color, color, color};
    __asm__ volatile("movdqa (%0), %%xmm0\n\t" : : "r"(vec) : "xmm0");

    while (num_pixels >= 16) {
      __asm__ volatile(
          "movntdq %%xmm0,   (%0)\n\t"
          "movntdq %%xmm0, 16(%0)\n\t"
          "movntdq %%xmm0, 32(%0)\n\t"
          "movntdq %%xmm0, 48(%0)\n\t"
          :
          : "r"(dst)
          : "memory");
      dst += 16;
      num_pixels -= 16;
    }

    while (num_pixels >= 4) {
      __asm__ volatile("movntdq %%xmm0, (%0)\n\t" : : "r"(dst) : "memory");
      dst += 4;
      num_pixels -= 4;
    }
    __asm__ volatile("sfence" ::: "memory");
  }

  while (num_pixels > 0) {
    *dst++ = color;
    num_pixels--;
  }
}

static int hwaccel_dispatch(const kernel_api_t *api, hwaccel_params_t *p) {
  if (!api || !p || !p->info_out)
    return -1;

  hwaccel_info_t *info = p->info_out;

  if (p->cmd == HWACCEL_CMD_INIT) {
    uint32_t eax, ebx, ecx, edx;
    cpu_cpuid(1, &eax, &ebx, &ecx, &edx);
    info->sse2_supported = ((edx & (1U << 26)) != 0);
    info->avx_supported = ((ecx & (1U << 28)) != 0);

    cpu_cpuid(7, &eax, &ebx, &ecx, &edx);
    info->erms_supported = ((ebx & (1U << 9)) != 0);

    info->lfb_base = p->lfb_base;
    detect_pci_gpu(info);
    info->pat_wc_active = setup_pat_write_combining(p->lfb_base, 16U * 1024U * 1024U);

    // Carica impostazioni DPI/Scale da Registro se presenti
    uint32_t saved_mode = 0;
    uint32_t saved_pct = 100;
    if (api->registry_get_value_dword &&
        api->registry_get_value_dword("HKLM\\SYSTEM\\Display", "ScaleMode", &saved_mode)) {
      info->scale_mode = (saved_mode == 1) ? 1 : 0;
    } else {
      info->scale_mode = 0; // Auto (EDID/DPI)
    }
    if (api->registry_get_value_dword &&
        api->registry_get_value_dword("HKLM\\SYSTEM\\Display", "ScalePercent", &saved_pct)) {
      if (saved_pct >= 100 && saved_pct <= 200)
        info->scale_percent = saved_pct;
      else
        info->scale_percent = 100;
    }

    parse_edid_and_dpi(info, p->width, p->height);
    info->initialized = true;
    return 0;
  }

  if (p->cmd == HWACCEL_CMD_BLIT32) {
    if (info->sse2_supported) {
      sse2_blit_32(p->dst, p->src, p->count_pixels);
    } else {
      uint64_t *d = (uint64_t *)p->dst;
      const uint64_t *s = (const uint64_t *)p->src;
      uint32_t qw = p->count_pixels / 2;
      for (uint32_t i = 0; i < qw; i++)
        d[i] = s[i];
      if (p->count_pixels & 1)
        ((uint32_t *)p->dst)[p->count_pixels - 1] = ((const uint32_t *)p->src)[p->count_pixels - 1];
    }
    return 0;
  }

  if (p->cmd == HWACCEL_CMD_FILL32) {
    if (info->sse2_supported) {
      sse2_fill_32(p->dst, p->fill_color, p->count_pixels);
    } else {
      uint32_t *d = (uint32_t *)p->dst;
      for (uint32_t i = 0; i < p->count_pixels; i++)
        d[i] = p->fill_color;
    }
    return 0;
  }

  if (p->cmd == HWACCEL_CMD_RECALC_DPI) {
    parse_edid_and_dpi(info, p->width, p->height);
    return 0;
  }

  if (p->cmd == HWACCEL_CMD_SET_SCALE) {
    info->scale_mode = (p->scale_mode == 1) ? 1 : 0;
    if (info->scale_mode == 0) {
      parse_edid_and_dpi(info, p->width, p->height);
    } else {
      uint32_t sc = p->scale_percent;
      if (sc < 100 || sc > 200)
        sc = 100;
      info->scale_percent = sc;
    }
    if (api->registry_set_value_dword) {
      api->registry_set_value_dword("HKLM\\SYSTEM\\Display", "ScaleMode", info->scale_mode);
      api->registry_set_value_dword("HKLM\\SYSTEM\\Display", "ScalePercent", info->scale_percent);
    }
    return 0;
  }

  return -1;
}

