// ==============================================================================
// NewOS V1 64-bit - UEFI Native Bootloader (BOOTX64.EFI)
// (C) Newspoint Software - NewSoft - NewOS V1
// - Inizializzazione UEFI 64-bit nativa (senza dipendenze esterne)
// - Configurazione Graphics Output Protocol (GOP) a 32bpp (1024x768 o nativa)
// - Lettura del Kernel (KERNEL.BIN) da volume FAT32/ESP
// - Mappatura della memoria RAM fisica (GetMemoryMap)
// - Chiusura servizi firmware (ExitBootServices)
// - Setup tabelle Paging a 4 livelli (4GB Identity Mapping Huge 2MB) e GDT
// - Passaggio parametri tramite BootInfo (0x9000) e salto a 0x100000 (1 MB)
// ==============================================================================

#include "efi.h"

static const EFI_GUID gEfiLoadedImageProtocolGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
static const EFI_GUID gEfiSimpleFileSystemProtocolGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
static const EFI_GUID gEfiFileInfoGuid = EFI_FILE_INFO_ID;
static const EFI_GUID gEfiGraphicsOutputProtocolGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;

void* memcpy(void* dest, const void* src, UINTN n) {
    UINT8* d = (UINT8*)dest;
    const UINT8* s = (const UINT8*)src;
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

void* memset(void* dest, int c, UINTN n) {
    UINT8* d = (UINT8*)dest;
    while (n--) {
        *d++ = (UINT8)c;
    }
    return dest;
}

static void efi_print(EFI_SYSTEM_TABLE *st, const CHAR16 *str) {
    if (st && st->ConOut && str) {
        st->ConOut->OutputString(st->ConOut, str);
    }
}


// Entry Point principale dell'applicazione UEFI (sottosistema efi_application)
EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    if (!SystemTable || !SystemTable->BootServices) {
        return EFI_INVALID_PARAMETER;
    }

    EFI_BOOT_SERVICES *bs = SystemTable->BootServices;

    efi_print(SystemTable, u"\r\n=== NewOS V1 64-bit UEFI Bootloader ===\r\n");
    efi_print(SystemTable, u"Inizializzazione grafica GOP e caricamento Kernel...\r\n");

    // -------------------------------------------------------------------------
    // 1. Configurazione Display tramite Graphics Output Protocol (GOP)
    // -------------------------------------------------------------------------
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
    EFI_STATUS status = bs->LocateProtocol((EFI_GUID*)&gEfiGraphicsOutputProtocolGuid, NULL, (VOID**)&gop);

    UINT64 fb_base = 0;
    UINT32 fb_width = 1024;
    UINT32 fb_height = 768;
    UINT32 fb_pitch = 4096;

    if (!EFI_ERROR(status) && gop && gop->Mode && gop->Mode->Info) {
        UINT32 best_mode = gop->Mode->Mode;
        BOOLEAN exact_found = FALSE;

        // Cerca prioritariamente la risoluzione 1024x768 (standard Classic NewOS)
        for (UINT32 m = 0; m < gop->Mode->MaxMode; m++) {
            UINTN info_size = 0;
            EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = NULL;
            if (!EFI_ERROR(gop->QueryMode(gop, m, &info_size, &info)) && info) {
                if (info->HorizontalResolution == 1024 && info->VerticalResolution == 768) {
                    best_mode = m;
                    exact_found = TRUE;
                    break;
                }
            }
        }

        if (exact_found && best_mode != gop->Mode->Mode) {
            gop->SetMode(gop, best_mode);
        }

        fb_base   = (UINT64)gop->Mode->FrameBufferBase;
        fb_width  = gop->Mode->Info->HorizontalResolution;
        fb_height = gop->Mode->Info->VerticalResolution;
        fb_pitch  = gop->Mode->Info->PixelsPerScanLine * 4;

        efi_print(SystemTable, u"[OK] Protocollo GOP inizializzato con successo.\r\n");
    } else {
        efi_print(SystemTable, u"[AVVISO] GOP non rilevato. Verranno usati valori predefiniti.\r\n");
    }

    // -------------------------------------------------------------------------
    // 2. Apertura del File System e Caricamento di KERNEL.BIN in memoria
    // -------------------------------------------------------------------------
    EFI_LOADED_IMAGE_PROTOCOL *loaded_image = NULL;
    status = bs->HandleProtocol(ImageHandle, (EFI_GUID*)&gEfiLoadedImageProtocolGuid, (VOID**)&loaded_image);

    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = NULL;
    if (!EFI_ERROR(status) && loaded_image && loaded_image->DeviceHandle) {
        bs->HandleProtocol(loaded_image->DeviceHandle, (EFI_GUID*)&gEfiSimpleFileSystemProtocolGuid, (VOID**)&fs);
    }
    if (!fs) {
        // Fallback: cerca su qualsiasi volume disponibile nel sistema
        bs->LocateProtocol((EFI_GUID*)&gEfiSimpleFileSystemProtocolGuid, NULL, (VOID**)&fs);
    }

    if (!fs) {
        efi_print(SystemTable, u"[ERRORE] File System FAT/ESP non accessibile!\r\n");
        return EFI_NOT_FOUND;
    }

    EFI_FILE_PROTOCOL *root = NULL;
    status = fs->OpenVolume(fs, &root);
    if (EFI_ERROR(status) || !root) {
        efi_print(SystemTable, u"[ERRORE] Impossibile aprire la radice del volume!\r\n");
        return status;
    }

    // Prova ad aprire il file del kernel nei percorsi standard
    const CHAR16 *kernel_paths[] = {
        u"\\NewOS\\Boot\\KERNEL.BIN",
        u"\\NewOS\\boot\\kernel.bin",
        u"NewOS\\Boot\\KERNEL.BIN",
        u"NewOS\\boot\\kernel.bin",
        u"KERNEL.BIN",
        u"\\KERNEL.BIN",
        u"\\EFI\\BOOT\\KERNEL.BIN",
        u"EFI\\BOOT\\KERNEL.BIN"
    };

    EFI_FILE_PROTOCOL *kernel_file = NULL;
    for (UINTN p = 0; p < sizeof(kernel_paths) / sizeof(kernel_paths[0]); p++) {
        status = root->Open(root, &kernel_file, kernel_paths[p], EFI_FILE_MODE_READ, EFI_FILE_READ_ONLY);
        if (!EFI_ERROR(status) && kernel_file) {
            break;
        }
    }

    if (!kernel_file) {
        efi_print(SystemTable, u"[ERRORE] KERNEL.BIN non trovato nel supporto di avvio!\r\n");
        root->Close(root);
        return EFI_NOT_FOUND;
    }

    // Calcola dimensione del file tramite GetInfo
    UINT8 info_buf[sizeof(EFI_FILE_INFO) + 256];
    UINTN info_size = sizeof(info_buf);
    UINTN kernel_size = 480 * 1024; // Default fallback: budget massimo 480 KB

    status = kernel_file->GetInfo(kernel_file, (EFI_GUID*)&gEfiFileInfoGuid, &info_size, info_buf);
    if (!EFI_ERROR(status)) {
        EFI_FILE_INFO *finfo = (EFI_FILE_INFO*)info_buf;
        if (finfo->FileSize > 0 && finfo->FileSize <= 491520) {
            kernel_size = (UINTN)finfo->FileSize;
        }
    }

    // Alloca buffer temporaneo per il kernel
    VOID *kernel_temp_buf = NULL;
    status = bs->AllocatePool(EfiLoaderData, kernel_size, &kernel_temp_buf);
    if (EFI_ERROR(status) || !kernel_temp_buf) {
        efi_print(SystemTable, u"[ERRORE] Impossibile allocare memoria per il Kernel!\r\n");
        kernel_file->Close(kernel_file);
        root->Close(root);
        return status;
    }

    UINTN read_size = kernel_size;
    status = kernel_file->Read(kernel_file, &read_size, kernel_temp_buf);
    kernel_file->Close(kernel_file);
    root->Close(root);

    if (EFI_ERROR(status) || read_size == 0) {
        efi_print(SystemTable, u"[ERRORE] Lettura di KERNEL.BIN fallita!\r\n");
        bs->FreePool(kernel_temp_buf);
        return status;
    }

    efi_print(SystemTable, u"[OK] KERNEL.BIN caricato correttamente in RAM.\r\n");

    // -------------------------------------------------------------------------
    // 3. Mappatura della Memoria Fisica Totale e Ottenimento MapKey
    // -------------------------------------------------------------------------
    UINTN mem_map_size = 0;
    EFI_MEMORY_DESCRIPTOR *mem_map = NULL;
    UINTN map_key = 0;
    UINTN desc_size = 0;
    UINT32 desc_ver = 0;

    // Prima chiamata per ottenere la dimensione richiesta
    bs->GetMemoryMap(&mem_map_size, NULL, &map_key, &desc_size, &desc_ver);
    mem_map_size += 4096; // Cuscinetto di sicurezza

    status = bs->AllocatePool(EfiLoaderData, mem_map_size, (VOID**)&mem_map);
    if (EFI_ERROR(status) || !mem_map) {
        efi_print(SystemTable, u"[ERRORE] Impossibile allocare buffer mappa memoria!\r\n");
        return status;
    }

    status = bs->GetMemoryMap(&mem_map_size, mem_map, &map_key, &desc_size, &desc_ver);
    if (EFI_ERROR(status)) {
        efi_print(SystemTable, u"[ERRORE] GetMemoryMap fallito!\r\n");
        return status;
    }

    // Calcola memoria RAM totale utilizzabile
    UINT64 total_ram_bytes = 0;
    UINTN num_entries = mem_map_size / desc_size;
    UINT8 *entry_ptr = (UINT8*)mem_map;

    for (UINTN i = 0; i < num_entries; i++) {
        EFI_MEMORY_DESCRIPTOR *d = (EFI_MEMORY_DESCRIPTOR*)(entry_ptr + (i * desc_size));
        if (d->Type == EfiLoaderCode || d->Type == EfiLoaderData ||
            d->Type == EfiBootServicesCode || d->Type == EfiBootServicesData ||
            d->Type == EfiConventionalMemory) {
            total_ram_bytes += d->NumberOfPages * 4096ULL;
        }
    }

    if (total_ram_bytes < 256ULL * 1024 * 1024) {
        total_ram_bytes = 256ULL * 1024 * 1024; // Fallback minimo: 256 MB
    }

    // -------------------------------------------------------------------------
    // 4. Uscita definitiva dai Servizi di Avvio Firmware (ExitBootServices)
    // -------------------------------------------------------------------------
    efi_print(SystemTable, u"Chiusura servizi UEFI e passaggio delle consegne a NewOS V1...\r\n");

    status = bs->ExitBootServices(ImageHandle, map_key);
    if (EFI_ERROR(status)) {
        // Se la mappa memoria è cambiata, rilegge la mappa e riprova una seconda volta
        mem_map_size += 4096;
        bs->GetMemoryMap(&mem_map_size, mem_map, &map_key, &desc_size, &desc_ver);
        status = bs->ExitBootServices(ImageHandle, map_key);
        if (EFI_ERROR(status)) {
            // Impossibile uscire dai boot services
            return status;
        }
    }

    // =========================================================================
    // D'ORA IN POI I SERVIZI UEFI NON ESISTONO PIÙ: IL KERNEL È IL SOLO ARBITRO
    // =========================================================================

    // 5. Copia il Kernel all'indirizzo fisico di destinazione 0x100000 (1 MB)
    UINT8 *kernel_dst = (UINT8*)0x100000;
    UINT8 *kernel_src = (UINT8*)kernel_temp_buf;
    for (UINTN i = 0; i < read_size; i++) {
        kernel_dst[i] = kernel_src[i];
    }
    // Pulisce il resto dell'area fino a 480 KB per sicurezza BSS
    for (UINTN i = read_size; i < 491520; i++) {
        kernel_dst[i] = 0;
    }

    // 6. Popola la struttura BootInfo all'indirizzo canonico 0x9000
    BootInfo *binfo = (BootInfo*)0x9000;
    binfo->framebuffer_base    = fb_base;
    binfo->width               = fb_width;
    binfo->height              = fb_height;
    binfo->pitch               = fb_pitch;
    binfo->bpp                 = 32;
    binfo->total_memory_bytes  = total_ram_bytes;

    // 7. Configura Paging a 4 Livelli (4 GB Identity Mapping a 0x2000)
    // Identico a boot/stage2.asm: PML4 a 0x2000, PDPT a 0x3000, PD a 0x4000..0x7000
    UINT32 *page_area = (UINT32*)0x2000;
    for (UINTN i = 0; i < 6144; i++) {
        page_area[i] = 0;
    }

    UINT64 *pml4 = (UINT64*)0x2000;
    UINT64 *pdpt = (UINT64*)0x3000;

    pml4[0] = 0x3003; // PML4[0] -> PDPT a 0x3000 (Present, Writable)
    pdpt[0] = 0x4003; // 0 - 1 GB (PD0 a 0x4000)
    pdpt[1] = 0x5003; // 1 - 2 GB (PD1 a 0x5000)
    pdpt[2] = 0x6003; // 2 - 3 GB (PD2 a 0x6000)
    pdpt[3] = 0x7003; // 3 - 4 GB (PD3 a 0x7000)

    // 2048 ingressi da 2 MB (totale 4 GB di spazio fisico)
    UINT64 *pd_entries = (UINT64*)0x4000;
    for (UINT64 i = 0; i < 2048; i++) {
        pd_entries[i] = (i * 0x200000ULL) | 0x83; // Bit 7 = Huge Page (2MB), Writable, Present
    }

    // Se il Framebuffer GOP risiede oltre i 4 GB (es. PCIe MMIO a 64 bit su GPU dedicate)
    if (fb_base >= 0x100000000ULL) {
        UINT64 gb_slot = fb_base / 0x40000000ULL;
        if (gb_slot < 512) {
            UINT64 *pd_high = (UINT64*)0x8000;
            for (UINTN i = 0; i < 512; i++) pd_high[i] = 0;
            pdpt[gb_slot] = 0x8003;

            UINT64 fb_aligned = (fb_base / 0x200000ULL) * 0x200000ULL;
            for (UINT64 i = 0; i < 512; i++) {
                pd_high[i] = (fb_aligned + i * 0x200000ULL) | 0x83;
            }
        }
    }

    // 8. Tabella GDT standard NewOS (0x1000)
    UINT64 *gdt = (UINT64*)0x1000;
    gdt[0] = 0x0000000000000000ULL; // 0x00: Null Descriptor
    gdt[1] = 0x00CF9A000000FFFFULL; // 0x08: 32-bit Code Segment
    gdt[2] = 0x00CF92000000FFFFULL; // 0x10: 32-bit Data Segment
    gdt[3] = 0x00209A0000000000ULL; // 0x18: 64-bit Code Segment (L=1, D=0)
    gdt[4] = 0x0000920000000000ULL; // 0x20: 64-bit Data Segment (Read/Write)

    struct {
        UINT16 limit;
        UINT64 base;
    } __attribute__((packed)) gdtr;
    gdtr.limit = 5 * 8 - 1;
    gdtr.base  = 0x1000;

    // 9. Transizione controllata: ricarica CR3, applica GDT e salta al Kernel a 0x100000
    __asm__ volatile (
        "cli\n"
        "movq %0, %%cr3\n"              // Carica la nostra PML4 a 0x2000
        "lgdt (%1)\n"                   // Carica la nostra GDT a 0x1000
        "pushq $0x18\n"                 // Segmento codice a 64-bit (0x18)
        "leaq 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"                       // Far return per ricaricare CS con 0x18
        "1:\n"
        "movw $0x20, %%ax\n"            // Segmenti dati a 64-bit (0x20)
        "movw %%ax, %%ds\n"
        "movw %%ax, %%es\n"
        "movw %%ax, %%ss\n"
        "movw %%ax, %%fs\n"
        "movw %%ax, %%gs\n"
        "movq $0x200000, %%rsp\n"       // Stack a 2 MB allineato a 16 byte
        "movq $0x9000, %%rdi\n"         // RDI = puntatore a BootInfo (System V AMD64 ABI)
        "movq $0x100000, %%rax\n"       // RAX = entry point kernel.bin
        "jmp *%%rax\n"                  // Avvia NewOS V1!
        :
        : "r"(0x2000ULL), "r"(&gdtr)
        : "rax", "rdi", "memory"
    );

    // Non si dovrebbe mai raggiungere questo punto
    while (1) {
        __asm__ volatile ("hlt");
    }

    return EFI_SUCCESS;
}
