# NewOS V1 (64-bit)

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)]()
[![Arch](https://img.shields.io/badge/architecture-x86__64%20(Long%20Mode)-blue.svg)]()
[![Kernel Size](https://img.shields.io/badge/kernel%20size-187%20KB%20%2F%20480%20KB-brightgreen.svg)]()
[![Dual Boot](https://img.shields.io/badge/boot-BIOS%20MBR%20%2B%20UEFI%2064--bit-blueviolet.svg)]()
[![Filesystem](https://img.shields.io/badge/filesystems-exFAT%20%7C%20NTFS--3G%20%7C%20EXT4%20%7C%20ISO%209660%20%7C%20UDF%20%7C%20RamFS-purple.svg)]()
[![UI](https://img.shields.io/badge/GUI-Liquid%20Glass%20%7C%20Mica%20%7C%20Classic%202000-lightgrey.svg)]()

**NewOS V1** è un sistema operativo nativo a 64-bit scritto in C ed Assembly per architettura **x86_64** / **NewOS V1** is a native 64-bit operating system written in C and Assembly for the **x86_64** architecture.

<p align="center">
  <img src="screenshot_block_a_desktop.png" alt="Ambiente Desktop NewOS V1 64-bit con tema Liquid Glass ed Esplora Risorse" width="820" />
  <br />
  <em>Ambiente Desktop di NewOS V1 (64-bit) con tema Liquid Glass, griglia icone anti-collisione ed Esplora Risorse su Disco locale (C:). / NewOS V1 (64-bit) Desktop with Liquid Glass theme, anti-collision icon grid and File Explorer on Local Disk (C:).</em>
</p>

---

<details>
<summary><strong>🇮🇹 Italiano (IT) — Documentazione e Guida Completa</strong></summary>

**NewOS V1** è un sistema operativo nativo a 64-bit scritto in C ed Assembly per architettura **x86_64**, con architettura ibrida a kernel protetto e sottosistemi applicativi/librerie scorporati (*Out-of-Kernel*), interfaccia grafica multi-tema (**Liquid Glass**, **Mica Dark/Light** e **Classic Windows 2000**), supporto completo a dischi fisici MBR/GPT ed exFAT/NTFS-3G/EXT4, stack di rete completo (IPv4, TCP, UDP, DHCP, DNS, TLS) e browser web integrato.

---

## Registro Aggiornamenti e Fix Recenti

<details>
<summary><strong>Espandi per visualizzare gli aggiornamenti e i fix recenti</strong></summary>

### 22 Novembre 2026
- **Nuovo Menu Start Unificato Moderno (`STARTMENU.DLL`) & Selettore `Personalizzazione`**:
  - Creato il menu Start out-of-kernel [startmenu_dll.c](SystemLibrary/startmenu_dll.c) ([startmenu_loader.c](kernel/gui/startmenu_loader.c)) con ricerca live unificata, sezione **Preferiti**, sezione **Tutto** con toggle **`Visualizzazione: Griglia | Elenco`** e badge **`Nuova`**, e footer utente/azioni rapide.
  - Aggiunto selettore dedicato **`Stile Menu Start: Moderno | Classico`** nella tab **Tema** di `Proprietà - Schermo` ([display.c](control_panel/display.c)).
- **Window Manager, Taskbar, Notifiche Toast & Scorciatoie Globali (`wm.c`)**:
  - Implementati **Window Snap** (50%/50% ai bordi laterali, massimizzazione al bordo superiore), **Alt+Tab grafico** con anteprima schede, **Menu contestuale Taskbar**, **Tooltip anteprima** al passaggio del mouse sui pulsanti finestra, pulsante **Mostra Desktop**, **Notifiche Toast**, **Centro Notifiche** nella tray e scorciatoie globali (**`Win+E`**, **`Win+R`**, **`Win+D`**, **`Win+L`**, **`Ctrl+Shift+Esc`**).
- **Diagnostica & Benchmark Bare-Metal (`BENCH.DLL`) & Audit Log Sicurezza (`SECCPL.CPL`)**:
  - Aggiunto il modulo out-of-kernel [bench_dll.c](SystemLibrary/bench_dll.c) (`BENCH.DLL`, avviabile con `start bench`) con rilevamento CPUID reale x86_64, test banda RAM via `RDTSC` (MB/s), indice CPU e scansione VFS.
  - Aggiunta la tab **`Audit Log`** in [seccpl.c](SystemLibrary/seccpl.c) per ispezionare gli eventi LSA/ACL e i commit di policy.
- **Esplora Risorse — Tab Multiple, Filtri Ricerca Live, Preferenze nel Registro, Sottomenu `Mostra` & Breadcrumb**:
  - Aggiunte in [explorer.c](explorer/explorer.c) le **Tab / Schede multiple** (`Ctrl+T`, `Ctrl+W`, pulsanti `+` / `x`), **filtri rapidi live** (`[.TXT]`, `[.DLL]`, `[All]`), **persistenza delle preferenze utente** (`ViewMode`, `ShowExtensions`, `ShowHidden`, `ShowPreviewPane`, `ShowCheckboxes`) in `HKCU\Software\NewOS\Explorer`, il sottomenu `Mostra >`, selezione multipla e **Breadcrumb** cliccabile.
- **Terminale `CMD` Potenziato ([`cmd.c`](kernel/apps/cmd.c))**:
  - Aggiunti **cronologia comandi** navigabile con frecce **`Su`/`Giù`**, **completamento comandi/file con `Tab`**, **redirect output (`>` e `>>`)** su file VFS e **scrollback con rotella del mouse**.
- **Gestione Font TrueType (`FONTSCPL.CPL`), Installazione Software Terzi (`PKGSVC.DLL`) & Porte I/O (`PORTS.DLL`)**:
  - Aggiunta applet out-of-kernel [fontscpl.c](SystemLibrary/fontscpl.c) con scansione `.TTF`, anteprima live TrueType via `FONTMGR.DLL` e salvataggio nel Registro.
  - Aggiunto servizio obbligatorio [pkgsvc.c](SystemLibrary/pkgsvc.c) per l'installazione isolata di software terzi in `C:\ProgramFile\<App>\` (blocco ACL su `C:\NewOS\`, manifest `APP.INI` e registrazione in `HKLM`).
  - Aggiunto sottosistema [ports_dll.c](SystemLibrary/ports_dll.c) con probing bare-metal UART `16550A` (`COM1`–`COM4`), porte parallele legacy IEEE 1284 (`LPT1`–`LPT2`), porte virtuali (`VCOM1/2`, `VLPT1`) e test Loopback HW.
- **Collegamenti `.LNK`, Client HTTP CLI (`curl`/`wget`) & Fix exFAT Multi-Cluster**:
  - Supporto completo ai file di collegamento `.LNK` (`[NewOS.Shortcut]` / `Target=` / `Args=`) in `LAUNCHER.DLL` ed `EXPLORER.DLL`, con comando `mklnk <file.lnk> <target>` in `CMD`.
  - Aggiunti comandi di rete `curl <url>`, `wget <url> [file]`, `nslookup <host>` e `arp -a` nel terminale `CMD` con esportazione DNS/TCP in `kernel_ext_api.h`.
  - Risolto il limite di 64 voci e la lettura a cavallo dei confini di cluster nelle directory exFAT multi-cluster in `kernel/fs/exfat.c`.
- **Visualizzatore Immagini (`IMGVIEW.DLL`) & Suite di Regressione (`test_all.js`)**:
  - Aggiunto il visualizzatore out-of-kernel [imgview_dll.c](SystemLibrary/imgview_dll.c) (`IMGVIEW.DLL`, avviabile con `start imgview`) con zoom `50%–150%`, rotazione `90°/180°/270°`, adattamento e navigazione libreria `.BMP`.
  - Aggiunta la suite automatizzata [test_all.js](tools/test_all.js) (`node tools/test_all.js`, 16 check PASS/FAIL su budget kernel, firme `NOSM`, ISO/IMG e boot QEMU).
- **Task Manager (`Moduli DLL`), Blocco Note (`Trova` / `Righe`) & Controllo Budget Kernel**:
  - Aggiunto in [taskmgr.c](taskmgr/taskmgr.c) il tab **Moduli DLL** con scansione dinamica di `C:\NewOS\SystemLibrary\`.
  - Potenziato [notepad.c](notepad/notepad.c) con ricerca live **`Trova` (`Ctrl+F`)**, **`Annulla` (`Ctrl+Z`)** e **numeri di riga opzionali**.
  - Aggiunto in [build.js](tools/build.js) il controllo automatico del budget `kernel.bin < 491.520 B (480 KB)`.
- **Esplora Risorse — 8 Modalità di Vista, Layout Dinamico & `CTRL + Rotella Mouse`**:
  - Implementate in [explorer.h](explorer/explorer.h) ed [explorer.c](explorer/explorer.c) tutte le 8 viste stile Windows Explorer (`Icone molto grandi`, `Icone grandi`, `Icone medie`, `Icone piccole`, `Elenco`, `Dettagli` con colonne Nome/Tipo/Dimensione/Permessi, `Riquadri` e `Contenuto`), selezionabili dal menu **Visualizza**.
  - Griglia adattiva dinamica alla larghezza della finestra con spaziatura compatta uniforme (`explorer_get_view_metrics`) e wrapping automatico su due righe per i nomi lunghi (`explorer_split_name`).
  - Aggiunto supporto hardware PS/2 IntelliMouse a 4 byte (`mouse_consume_scroll` in [mouse.c](kernel/drivers/mouse.c)), codici `KEY_WHEEL_UP`/`KEY_WHEEL_DOWN` + `keyboard_is_ctrl_pressed()` ([keyboard.c](kernel/drivers/keyboard.c), [wm.c](kernel/gui/wm.c)) per scorrere normalmente o cambiare vista istantaneamente con **`CTRL + Rotella`**.
- **Libreria Controlli UI Standard Out-of-Kernel (`COMCTL.DLL`)**:
  - Creata la libreria condivisa [comctl.h](SystemLibrary/comctl.h) / [comctl_dll.c](SystemLibrary/comctl_dll.c) ([comctl_loader.c](kernel/gui/comctl_loader.c)) con controlli standard moderni multi-tema (`Button`, `Checkbox`, `Radio`, `ToggleSwitch`, `ProgressBar`, `Slider`, `Scrollbar`, `TextBox`, `ComboBox`, `ListView`, `TreeView`, `TabControl`, `Breadcrumb`, `InfoBar` e `PropertyGrid` multi-colonna/multi-tipo) utilizzati dalle applicazioni out-of-kernel.
- **Supporto EXT4 (`FS_EXT.DLL`) & NVMe TRIM / Deallocate (`NVME.DLL`)**:
  - Aggiunto il driver `EXT4` ([ext4.c](kernel/fs/ext4.c), [ext4.h](kernel/fs/ext4.h)) nel modulo out-of-kernel [fs_ext_dll.c](SystemLibrary/fs_ext_dll.c) e il supporto `NVME_CMD_DSM` (Deallocate/TRIM) in [nvme_dll.c](SystemLibrary/nvme_dll.c), [ata.c](kernel/drivers/ata.c), [ahci.c](kernel/drivers/ahci.c) ed [exfat_ops.c](kernel/fs/exfat_ops.c).
- **Servizio di Avvio Unificato Out-of-Kernel (`LAUNCHER.DLL` + `launcher_loader.c`)**:
  - Centralizzato il lancio di programmi, applet `.CPL`, documenti, script e moduli critici in [launcher_dll.c](SystemLibrary/launcher_dll.c) e [launcher_loader.c](kernel/apps/launcher_loader.c), con verifica firma `NOSM` + `CRC32` e integrazione in [cmd.c](kernel/apps/cmd.c), [wm.c](kernel/gui/wm.c) ed [explorer.c](explorer/explorer.c).
- **Centro Sicurezza e Permessi (`SECCPL.CPL`) & Registro Isolato LSA**:
  - Aggiunta la suite di sicurezza out-of-kernel [seccpl.c](SystemLibrary/seccpl.c) con traslucidità allineata al tema (`surface_alpha`) e gestione policy kernel nel ramo isolato `HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Lsa\KernelSecurityPolicy` ([registry.c](kernel/fs/registry.c)), protetto da `g_kernel_policy_gate` e aggiornabile solo via `registry_commit_kernel_policy()`.
  - Implementato enforcement VFS (`vfs_check_acl`, `vfs_is_admin_authenticated`) in [vfs.c](kernel/fs/vfs.c): l'eliminazione/rinomina in `C:\NewOS\*` richiede autenticazione amministratore (verifica password inline in `SECCPL.CPL`) e `AllowNewOSDelete=1` nel Registro isolato, oltre alla protezione dei file critici (`REGISTRY.DAT`, `NewOS/Boot/*`, `BSOD.DLL`, `LAUNCHER.DLL`, `SECCPL.DLL`).
- **Estensione File System a 64-bit (`> 4 GB`) & Supporto HD/SSD/NVMe senza limiti fittizi**:
  - Convertiti a `uint64_t`/`int64_t` dimensioni file, offset `vfs_seek`/`vfs_tell`/`vfs_file_size`, dentry cache e indirizzamento LBA/FAT/Bitmap in [vfs.h](kernel/fs/vfs.h), [vfs.c](kernel/fs/vfs.c), [vfs_cache.c](kernel/fs/vfs_cache.c), [exfat.c](kernel/fs/exfat.c) ed [exfat_ops.c](kernel/fs/exfat_ops.c), rimuovendo l'offset hardcoded `EXFAT_PARTITION_LBA` in favore di `vbr->partition_offset`.
  - Rimossi i fallback fittizi (`128 MB`, `650 MB`, `16 MB`) in [vfs_mount.c](kernel/fs/vfs_mount.c) ed [exfat_ops.c](kernel/fs/exfat_ops.c) con calcolo reale `total_bytes`/`free_bytes` a 64-bit.
  - Integrato il controller **NVMe** (`NVME.DLL`) nel VFS tramite `ata_register_nvme_device()` in [ata.c](kernel/drivers/ata.c) e `VFS_BUS_NVME`, con probing GPT/MBR automatico fino alla capienza massima del namespace.
- **Ottimizzazioni Kernel Heap, Cache VFS & exFAT Bare-Metal**:
  - Implementato `kmalloc_aligned()` reale in [heap.c](kernel/mm/heap.c) tramite split in-place compatibile con `kfree()` per l'allineamento a `4096` byte di tabelle `PML4/PDPT/PD/PT` e buffer DMA.
  - Potenziata la cache VFS ([vfs_cache.h](kernel/fs/vfs_cache.h), [vfs_cache.c](kernel/fs/vfs_cache.c)) a **256 settori / 64 dentry** con tabella hash a **512 bucket** ($O(1)$) e fix eviction multi-disco (`ata_write_device_sectors`).
  - Introdotti `g_next_free_hint` con scansione bitmap a **64 bit** (`__builtin_ctzll`) e fast-path **zero-copy multi-cluster** in [exfat.c](kernel/fs/exfat.c).

### 20 Novembre 2026
- **BSOD Liquid Glass Out-of-Kernel (`BSOD.DLL` + `KERNEL.SYM`)**:
  - Implementata la schermata blu diagnostica out-of-kernel in [bsod_dll.c](SystemLibrary/bsod_dll.c) e [bsod_loader.c](kernel/arch/x86_64/bsod_loader.c), pre-caricata in RAM al boot insieme alla tabella simboli `KERNEL.SYM` (`NSYM`) e allo sfondo ufficiale di login (`LOGIN.BMP` 1024x768) senza dipendere da disco o `kmalloc` durante il crash.
  - Agganciamento diretto a tutte le eccezioni CPU non gestite in [idt.c](kernel/arch/x86_64/idt.c), dump completo dei registri x86_64 (`RIP`, `RSP`, `RBP`, `CR2`, `CR3`, `RAX..R15`), stack backtrace con risoluzione **file e riga sorgente**, output su seriale `COM1` e polling tastiera (`[INVIO / R]` Riavvia, `[S]` Spegni).
  - Aggiunto comando di test `bsodtest [pf|div0|ud]` in [cmd.c](kernel/apps/cmd.c).
- **Fix Cambio Risoluzione & Cambio Tema/Stile Anti-Artefatto**:
  - In [vga.c](kernel/drivers/vga.c): programmati i registri BGA `VBE_DISPI_INDEX_VIRT_WIDTH`, `VIRT_HEIGHT`, `X_OFFSET=0`, `Y_OFFSET=0` (rimosso `NOCLEARMEM`) eliminando lo sfasamento di stride sulle risoluzioni widescreen (`1280x720`, ecc.).
  - In [graphics.c](kernel/gui/graphics.c): `photo_wallpaper_buf` e `blurred_buf` si ridimensionano dinamicamente a `scr_width x scr_height` con copia riga per riga secondo `scr_pitch_pixels`, pulizia `gfx_clear(0)`, ricalcolo `gfx_precompute_blur()` coerente con sfondi procedurali o fotografici (`active_bg_is_photo`) e ombre `gfx_draw_drop_shadow()` arrotondate.
  - In [wm.c](kernel/gui/wm.c) e [uistyle_loader.c](kernel/gui/uistyle_loader.c): redraw atomico `wm_render() + gfx_flip()` ad ogni cambio tema, UI Style o risoluzione.
- **Ricollegamento Schermata di Logout (`LOGOUT.MOD`) & Fix PIC**:
  - Ricollegata la voce **"Arresta il sistema..."** del menu Start in [wm.c](kernel/gui/wm.c) a `screen_mgr_run_logoutscreen()` con gestione completa di Spegnimento ACPI, Riavvio, Standby/Sospensione, Disconnessione (ritorno a `LOGIN.MOD`) e Annulla.
  - Corretto l'indirizzamento delle stringhe opzioni in [logoutscreen.c](logoutscreen/logoutscreen.c) in formato 100% PIC `RIP`-relativo per il rendering corretto dei testi nel modulo piatto `LOGOUT.MOD`.

</details>

---

## Novità della Settimana (16 – 19 Novembre 2026)

- **Scorporo Massivo Out-of-Kernel (29 DLL in `C:\NewOS\SystemLibrary\`)**: riduzione di `kernel.bin` da ~326 KB a **~187 KB / 480 KB** spostando fuori dal kernel le applicazioni (`EXPLORER.DLL`, `SYSAPPS.DLL`), i controlli standard (`COMCTL.DLL`), le estensioni FS (`FS_EXT.DLL`), i dialoghi comuni (`COMDLG.DLL`), lo stile UI moderno Copenhagen (`COPENHAGEN.DLL`), l'accelerazione 2D/DPI (`HWACCEL.DLL`), il gestore font TrueType/Unicode (`FONTMGR.DLL`) e i codec immagine (`IMGCODEC.DLL`).
- **Motore Web Browser Completo Out-of-Kernel**: integrati fuori dal kernel `WEBBROWSER.DLL`, il motore HTML/DOM NetSurf/Hubbub (`NETSURF_CORE.DLL`), il motore CSS `libcss` (`CSSENGINE.DLL`), il motore JavaScript QuickJS (`JSENGINE.DLL`) e il layer HTTPS/TLS (`NETTLS.DLL`).
- **Supporto Hardware Bare-Metal Esteso & Ottimizzazione Kernel**: aggiunto parsing tabelle **ACPI**, scansione **PCI/PCIe fino a 256 bus** con decodifica **BAR a 64-bit**, mapping MMIO (`paging_map_mmio`), allocatore DMA allineato e nuovi driver **NVMe** (`NVME.DLL`), **Intel HDA** (`AUDIO.DLL`), **Realtek RTL8169/8168** e **VirtIO-Net** (`NETSTACK.DLL`).
- **Accelerazione Grafica & Sottosistema 3D (`GPU3D.DLL`)**: introdotti i driver **VMware SVGA II**, **VirtIO-GPU**, **Intel GPU** e rasterizzatore 3D software fallback (`soft3d.c`).
- **Gestione Periferiche ad Albero (`DEVMGMT.CPL`)**: rinnovata l'applet Gestione Periferiche con vista gerarchica categorizzata, espansione/collasso nodi e ispezione hardware reale PCI/ACPI.
- **Configurazione IDE Freestanding**: aggiunti `.vscode/c_cpp_properties.json` e `.clangd` allineati a `tools/build.js` per azzerare i falsi positivi di IntelliSense.

---

## Caratteristiche Principali

### 1. Bootloader Ibrido Dual-Mode (BIOS & UEFI), OOBE e Autenticazione Protetta
- **BIOS Legacy (MBR + Stage 2)**:
  - MBR a 16-bit (`boot/bootsect.asm`).
  - Stage 2 a 16/32/64-bit (`boot/stage2.asm`) con rilevamento memoria E820, VESA VBE 1024x768 @ 32bpp LFB, abilitazione linea A20, mapping PML4 a 4 livelli (4 GB identity mapping con Huge Pages da 2 MB) e passaggio sicuro a Long Mode 64-bit.
  - Limite di caricamento settori CHS impostato rigorosamente a 960 settori (480 KB).
- **UEFI Nativo 64-bit (`BOOTX64.EFI`)**:
  - Bootloader UEFI conforme alle specifiche UEFI 2.x x86_64.
  - Partizione EFI System Partition (ESP) FAT32 a LBA 256000 (`Type 0xEF`).
- **Bypass Automatico del Boot Manager Post-Installazione (`BYPASS_BOOT.md`)**:
  - Una volta completata la prima configurazione (`OOBECompleted=1` / `SetupStage=Completed`), il sistema salta automaticamente il menu di avvio interattivo caricando direttamente la schermata di logon protetta (`LOGIN.MOD`), mantenendo il supporto a `F8` / `ESC` per il menu di emergenza.
- **Migrazione e Creazione Dinamica del Profilo Utente in OOBE (`USER_PROFILE_OOBE.md`)**:
  - Durante il Passo 4 della procedura guidata `OOBE.MOD`, il nome account scelto dall'utente viene migrato atomicamente sul file system exFAT (`C:\User\<NomeUtente>\`), aggiornando `OOBE.INI`, `USER.CFG` e il Registro di Sistema e precompilando automaticamente `LOGIN.MOD` ai successivi riavvii.

| Splash Screen (`BOOTSCR.MOD`) | Procedura Guidata OOBE (`OOBE.MOD`) | Accesso Protetto (`LOGIN.MOD`) |
| :---: | :---: | :---: |
| <img src="screenshot_update.png" alt="Splash Screen BOOTSCR.MOD" width="280" /> | <img src="screenshot_oobe_step4_mario.png" alt="Configurazione Utente OOBE.MOD" width="280" /> | <img src="screenshot_login_mario.png" alt="Schermata di Login LOGIN.MOD" width="280" /> |
| *Modulo di avvio firmato `BOOTSCR.MOD`* | *Passo 4 OOBE: creazione profilo personalizzato* | *Logon diretto post-OOBE con account migrato* |

---

### 2. Kernel Core Protetto (~187 KB / 480 KB)
- **Budget Kernel Rispettato**: Dimensione di `kernel.bin` mantenuta a **~187 KB (191.916 byte)** grazie allo scorporo sistematico dei sottosistemi, driver avanzati, browser e librerie dinamiche in `C:\NewOS\SystemLibrary\`, ben al di sotto del vincolo di 480 KB (491.520 byte).
- **Separazione Privilegi Ring 0 / Ring 3**:
  - Istruzioni native `SYSCALL` e `SYSRET` per le chiamate di sistema (`kernel/arch/x86_64/syscall.c`).
  - Protezione della memoria virtuale con Supervisor bit nelle tabelle PML4 (`kernel/mm/paging.c`).
- **Multitasking & Multi-Core SMP**:
  - Supporto Symmetric Multiprocessing (SMP) per architetture x86_64 con AP trampoline (`kernel/arch/x86_64/smp.c`).
  - Scheduler cooperativo e preemptivo con salvataggio contesti e priorità di esecuzione (`kernel/arch/x86_64/sched.c`).
  - Canali di comunicazione inter-processo IPC a coda di messaggi sicura e piping shell (`kernel/ipc/ipc.c`).
- **Allocatore di Memoria & Heap**:
  - Heap dinamico di sistema (`kmalloc`, `kmalloc_aligned`, `kfree`, `kcalloc`, `krealloc`).
- **Crittografia Hardware & Registro Sicuro**:
  - Motore di cifratura **AES-256** integrato (`kernel/lib/aes256.c`).
  - Registro di configurazione con accesso in sola lettura pre-desktop e scrittura abilitata solo a sessione autenticata (`kernel/fs/registry.c`).

---

### 3. File System, Gestione Dischi e Isolamento Profili (`C:\User\`)
- **exFAT Nativo R/W**: Allocazione dinamica cluster a 4 KB, scansione bitmap a 64-bit, fast-path multi-cluster zero-copy, creazione/modifica/rinomina/cancellazione file e directory con flush sincronizzato della cache settori e auto-ripristino VBR di backup.
- **NTFS-3G R/W**: Lettura e scrittura completa su volumi NTFS, parsing della MFT (Master File Table) e gestione Data Runs.
- **EXT4**: Supporto filesystem Linux EXT4 out-of-kernel tramite `FS_EXT.DLL`.
- **ISO 9660 & Joliet**: Supporto per dischi CD/DVD con codifica Unicode UCS-2.
- **UDF 1.02 & 2.50**: Supporto per dischi DVD-Video e Blu-Ray Disc.
- **RamFS**: Unità disco volatile ad alte prestazioni in memoria RAM (`R:\`).
- **Supporto GPT & MBR a 64-bit (`> 4 GB`)**: Rilevamento automatico geometrie e tabelle di partizione per dischi HD/SSD/NVMe e supporti USB.

| Struttura Radice `C:\` in Esplora Risorse | Directory Profili Utente `C:\User\` post-OOBE |
| :---: | :---: |
| <img src="screenshot_block_a_desktop.png" alt="Radice Disco C:" width="410" /> | <img src="screenshot_explorer_user_mario.png" alt="Cartella C:\User con profilo Mario" width="410" /> |
| *Gerarchia di sistema in `C:\NewOS`, `C:\ProgramFile` e `C:\User`* | *Migrazione atomica del profilo utente in `C:\User\Mario`* |

---

### 4. Driver Hardware, Accelerazione 3D & Periferiche Bare-Metal
- **ACPI & Bus PCI/PCIe Esteso**: Parsing tabelle ACPI (`RSDP`, `XSDT`, `FADT`, `MADT`, `MCFG`), scansione PCI/PCIe completa fino a **256 bus** con decodifica **BAR a 64-bit**, mapping MMIO (`paging_map_mmio`) e allocatore DMA allineato.
- **Storage**: Controller **NVMe** (`nvme.c` + `NVME.DLL` con comando DSM Deallocate/TRIM), **AHCI SATA** (DMA/Interrupt) e **ATA/IDE** PIO 28-bit + ATAPI per lettori ottici CD/DVD.
- **Input PS/2 & USB**: Driver mouse PS/2 con supporto **IntelliMouse 4-byte** (rotella scroll), controller **USB xHCI / UHCI / OHCI**, periferiche HID e USB Mass Storage BOT con AutoPlay automatico e handoff BIOS/OS.
- **Audio**: Supporto **Intel HDA** (`hda.c`) e codec stereo **AC97** a 44.1 kHz, con sottosistema mixer e riproduzione `.WAV` scorporati in `AUDIO.DLL`.
- **Grafica & Accelerazione 3D**: Driver **VMware SVGA II**, **VirtIO-GPU**, **Intel GPU**, VESA VBE / Bochs BGA fino a 1920x1080 @ 32bpp, rasterizzatore 3D fallback (`soft3d.c`), accelerazione 2D/DPI (`HWACCEL.DLL`) e pipeline 3D out-of-kernel (`GPU3D.DLL`).
- **Network**: Driver **Intel PRO/1000 MT (E1000)**, **Realtek RTL8169/8168 Gigabit**, **Realtek RTL8139**, **VirtIO-Net** e Wi-Fi con stack Ethernet, ARP, IPv4, ICMP Ping, UDP, TCP, DHCP, DNS nativo RFC 1035 (`NETSTACK.DLL`) e supporto **TLS/HTTPS** (`NETTLS.DLL`).

---

### 5. Interfaccia Grafica, Temi Dinamici e Desktop Grid System
- **Supporto Multi-Tema Nativo (Classic Windows 2000, Liquid Glass, Mica Dark & Light)**:
  - Rendering classico Windows 2000 (`COLOR_BTNFACE = 0xD4D0C8`, bordi 3D bevel/sunken) oppure moderno traslucido con alpha-blending, curvature parametriche e riflessi speculari.
- **Sistema a Griglia Desktop Anti-Collisione**:
  - Dimensioni cella griglia: 116x88 pixel, con margini perimetrali di sicurezza e calcolo dinamico delle righe disponibili.
  - Word-wrapping automatico a due righe per etichette lunghe e riallineamento automatico su cambio risoluzione.
- **Menu Contestuale Indipendente & Autoaggiornante**:
  - Scorporato in `kernel/gui/context_menu.c`, legge dinamicamente le associazioni file da `REGISTRY.DAT` (`.TXT`, `.NOR`, `.WAV`, `.MP3`, `.BMP`, ecc.).

---

### 6. Applicazioni, Pannello di Controllo e Sottosistemi Scorporati
Tutte le applicazioni accessorie e i tool di sistema sono organizzati come moduli indipendenti:
- [`explorer/`](explorer/): **Esplora Risorse** (`EXPLORER.DLL`) con 8 viste stile Windows (`Icone molto grandi`, `Icone grandi`, `Icone medie`, `Icone piccole`, `Elenco`, `Dettagli`, `Riquadri`, `Contenuto`), cambio vista diretto con **`CTRL + Rotella`**, griglia dinamica adattiva, Web View preview, Tree View laterale e barra indirizzi.
- [`control_panel/`](control_panel/): **Pannello di Controllo** con supporto dual-mode (Vista Classica Windows 2000 e Vista Moderna Impostazioni/Copenhagen) e applet per Schermo (`DESK.CPL`), Mouse (`MAIN.CPL`), Audio (`MMSYS.CPL`), Alimentazione (`POWERCFG.CPL`), Gestione Periferiche ad albero (`DEVMGMT.CPL`), Sicurezza e Permessi (`SECCPL.CPL`) e Sistema (`SYSDM.CPL`).
- [`oobe/`](oobe/): **Procedura Guidata OOBE** in 6 passi (Benvenuto, Schermo, Test Audio, Rete, Account, Conclusione).
- [`notepad/`](notepad/): **Blocco Note** con supporto editing testo, salvataggio e apertura file.
- [`calculator/`](calculator/): **Calcolatrice Digitale** con display numerico ed operazioni standard.
- [`player/`](player/): **NewOS Media Player** con visualizzatore spettro sonoro animato, playlist e controllo volume.
- [`rules_viewer/`](rules_viewer/): **Visualizzatore Regole** per documenti e policy di sistema in formato `.NOR` (tramite `RULES.DLL`).
- [`whatsnew/`](whatsnew/): **Novità del Sistema** con storico release e funzionalità per gli utenti (tramite `WHATSNEW.DLL`).
- [`autoplay/`](autoplay/): **Gestione AutoPlay** per rilevamento e apertura automatica di chiavette USB e dischi.
- [`taskmgr/`](taskmgr/): **Gestione Attività** con grafici di utilizzo CPU e memoria, tabella processi e canali IPC.
- [`WebBrowser/`](WebBrowser/): **NewSoft Web Browser** out-of-kernel (`WEBBROWSER.DLL`) con motore HTML/DOM NetSurf/Hubbub (`NETSURF_CORE.DLL`), motore CSS (`CSSENGINE.DLL`), motore JavaScript QuickJS (`JSENGINE.DLL`), decodifica immagini PNG/JPEG/WebP (`IMGCODEC.DLL`) e supporto HTTPS/TLS (`NETTLS.DLL`).

| Impostazioni Moderne (`SETTINGS.DLL`) | Proprietà Mouse e Schemi (`MAIN.CPL` / `CURSORMGR.DLL`) |
| :---: | :---: |
| <img src="screenshot_cp_modern_scrollbar_top.png" alt="Impostazioni Moderne SETTINGS.DLL" width="410" /> | <img src="screenshot_mouse_cpl.png" alt="Proprietà Mouse MAIN.CPL" width="410" /> |
| *Interfaccia Impostazioni Moderna renderizzata fuori dal kernel da `SETTINGS.DLL`* | *Applet Proprietà Mouse a 4 schede con gestione schemi cursore Aero/Glass* |

| NewOS Media Player (`PLAYER.EXE`) | Stile Classico Windows 2000 & Sottosistema Audio AC97 |
| :---: | :---: |
| <img src="screenshot_player.png" alt="NewOS Media Player" width="410" /> | <img src="screenshot_start.png" alt="Tema Classic Windows 2000 e Audio AC97" width="410" /> |
| *Riproduzione multimediale `.WAV` con analizzatore di spettro e playlist* | *Tema Classic Bevel Windows 2000 con Proprietà Audio AC97 e Prompt dei Comandi* |

<details>
<summary><strong>📸 Galleria Funzionalità Implementate (Menu Start, Explorer, Window Manager, CPL, Sicurezza, CLI & App)</strong></summary>

<br/>

#### Menu Start Moderno & Personalizzazione (`STARTMENU.DLL`, `DESK.CPL`)
| Menu Start Griglia Moderna | Ricerca Live nel Menu Start | Selettore Stile Start in `DESK.CPL` |
| :---: | :---: | :---: |
| <img src="docs/screenshots/screenshot_startmenu_modern_grid.png" alt="Menu Start Griglia Moderna" width="280" /> | <img src="docs/screenshots/screenshot_startmenu_modern_search.png" alt="Ricerca Live Menu Start" width="280" /> | <img src="docs/screenshots/screenshot_display_startmenu_opt.png" alt="Selettore Stile Start in DESK.CPL" width="280" /> |

#### Esplora Risorse (`EXPLORER.DLL`) & Cestino Reale (`RECYCLE.DLL`)
| Schede Multiple (`Ctrl+T` / `Ctrl+W`) | Filtri Ricerca Live (`[.TXT]` / `[.DLL]`) | Anteprima & Dettagli Sidebar | Cestino Reale (`C:\Recycle.Bin`) |
| :---: | :---: | :---: | :---: |
| <img src="docs/screenshots/screenshot_explorer_tabs.png" alt="Explorer Tab Multiple" width="210" /> | <img src="docs/screenshots/screenshot_explorer_search_filter.png" alt="Explorer Filtri Ricerca" width="210" /> | <img src="docs/screenshots/screenshot_exp_cat1_preview_details.png" alt="Explorer Anteprima e Dettagli" width="210" /> | <img src="docs/screenshots/screenshot_recycle_bin_real.png" alt="Cestino Reale" width="210" /> |

#### Window Manager, Notifiche & Desktop (`wm.c`)
| Window Snap Laterale (`50%`) | Switcher Grafico `Alt+Tab` | Centro Notifiche & Toast |
| :---: | :---: | :---: |
| <img src="docs/screenshots/screenshot_wm_cat2_snap.png" alt="Window Snap" width="280" /> | <img src="docs/screenshots/screenshot_wm_cat2_alttab.png" alt="Alt+Tab Grafico" width="280" /> | <img src="docs/screenshots/screenshot_wm_notif_center.png" alt="Centro Notifiche" width="280" /> |

#### Pannello di Controllo, Font TrueType, Porte COM/LPT & Package Service
| Font TrueType (`FONTSCPL.DLL`) | Porte COM/LPT & Loopback (`PORTS.DLL`) | Installazione Software (`PKGSVC.DLL`) |
| :---: | :---: | :---: |
| <img src="docs/screenshots/screenshot_fontscpl_applied.png" alt="Font TrueType FONTSCPL.DLL" width="280" /> | <img src="docs/screenshots/screenshot_ports_loopback.png" alt="Porte COM e LPT PORTS.DLL" width="280" /> | <img src="docs/screenshots/screenshot_pkgsvc_installed.png" alt="Package Service PKGSVC.DLL" width="280" /> |

#### Sicurezza, Diagnostica Bare-Metal & Applicazioni di Sistema
| Audit Log (`SECCPL.DLL`) | Benchmark Hardware (`BENCH.DLL`) | Task Manager Moduli DLL | Notepad (`Ctrl+F` / Righe) | Visualizzatore (`IMGVIEW.DLL`) |
| :---: | :---: | :---: | :---: | :---: |
| <img src="docs/screenshots/screenshot_seccpl_audit_log.png" alt="SECCPL Audit Log" width="170" /> | <img src="docs/screenshots/screenshot_bench_hw.png" alt="BENCH.DLL" width="170" /> | <img src="docs/screenshots/screenshot_taskmgr_dll_modules.png" alt="Task Manager Moduli DLL" width="170" /> | <img src="docs/screenshots/screenshot_notepad_find_lines.png" alt="Notepad Trova e Righe" width="170" /> | <img src="docs/screenshots/screenshot_imgview_init.png" alt="IMGVIEW.DLL" width="170" /> |

#### Terminale CMD Potenziato, Collegamenti `.LNK` & Rete CLI
| History, Tab & Redirect `>` | `arp -a` & `mklnk` | Avvio via Collegamento `.LNK` |
| :---: | :---: | :---: |
| <img src="docs/screenshots/screenshot_cmd_enhanced.png" alt="CMD History Tab Redirect" width="280" /> | <img src="docs/screenshots/screenshot_cmd_net_lnk.png" alt="CMD arp -a e mklnk" width="280" /> | <img src="docs/screenshots/screenshot_lnk_launch.png" alt="Avvio da file LNK" width="280" /> |

</details>

---

### 7. Librerie Dinamiche Out-of-Kernel (`C:\NewOS\SystemLibrary\`)
In base alla Direttiva Architetturale (Regola 9), i sottosistemi grafici, driver avanzati, browser e applicativi risiedono fuori dal Kernel Mode come **29 librerie dinamiche `.DLL`** firmate (`NOSM` + CRC32) affiancate dai rispettivi descrittori `.INI`:
- **Temi, UI, Controlli & Grafica**: `THEMEMGR.DLL` (motore temi e `[RenderScripts]`), `CURSORMGR.DLL` (schemi cursori `.CUR`/`.ANI`), `COPENHAGEN.DLL` (stili UI moderni Copenhagen), `SETTINGS.DLL` (Impostazioni Moderne), `COMCTL.DLL` (libreria controlli UI standard), `HWACCEL.DLL` (accelerazione 2D e DPI scaling), `GPU3D.DLL` (accelerazione grafica 3D), `FONTMGR.DLL` (font TrueType/Unicode `SANS.TTF`/`UNICODE.FNT`), `IMGCODEC.DLL` (decodifica PNG/JPEG/WebP/BMP), `COMDLG.DLL` (dialoghi comuni Apertura/Salvataggio/Sfoglia).
- **Applicazioni, Shell & Pannello di Controllo**: `EXPLORER.DLL`, `LAUNCHER.DLL`, `SECCPL.DLL`, `BSOD.DLL`, `SYSAPPS.DLL`, `CPLEXT2.DLL`, `RECYCLE.DLL`, `KEYBCPL.DLL`, `NETCPL.DLL`, `TIMEDATE.DLL`, `USERACCT.DLL`, `WHATSNEW.DLL`, `RULES.DLL`.
- **Browser Web & Rete TLS**: `WEBBROWSER.DLL`, `NETSURF_CORE.DLL` (HTML/DOM NetSurf/Hubbub), `CSSENGINE.DLL` (`libcss`), `JSENGINE.DLL` (QuickJS), `NETTLS.DLL` (TLS/HTTPS).
- **Driver & Estensioni Hardware/FS**: `NVME.DLL` (driver NVMe + TRIM), `AUDIO.DLL` (mixer audio AC97/Intel HDA e `.WAV`), `NETSTACK.DLL` (stack TCP/IP/DHCP/DNS), `FS_EXT.DLL` (estensioni filesystem NTFS-3G / EXT4 / ISO 9660 / UDF).

#### Gestione Avanzata dei File Tema (`.THEMES` e `.THM`)
Il sistema di personalizzazione visiva di NewOS è **interamente pilotato dal file del tema**:
1. **Controllo Granulare di Curvature e Bordi**:
   - Raggi indipendenti per finestre (`CornerRadius`, `BorderRadius`), pulsanti (`ButtonRadius`, `ButtonBorderRadius`), schede (`TabRadius`), campi di input (`InputRadius`), pannelli laterali (`PanelRadius`), taskbar (`TaskbarBtnRadius`), pulsante Start (`StartBtnRadius`) e area di notifica (`TrayRadius`).
   - Flag di raccordo automatico: `BorderFollowsCorner` e `ControlBorderFollowsRadius`.
2. **Motore di Scripting di Rendering Avanzato (`[RenderScripts]`) in `THEMEMGR.DLL`**:
   - Pipeline dichiarativa per `TitlebarScript`, `ButtonScript`, `PanelScript`, `ProgressScript`, `SelectionScript`, `TaskbarScript`, `RenderScript`.
   - Macro grafiche supportate: `SPECULAR_ARC`, `VERTICAL_GLOSS`, `HIGHLIGHT_EDGE`, `INNER_HIGHLIGHT`, `ETCHED_LINE`, `CYLINDER_SHADING`, `AURA_BORDER`, `NOISE_TEXTURE`, `GLASS_TINT`, `GRADIENT`.
   - Documentazione completa disponibile in [`THEME_SPECIFICATION.md`](THEME_SPECIFICATION.md).

---

## Struttura del Repository

```
├── .agents/                 # Direttive comportamentali e operative dell'Agente AI
├── autoplay/                # Sottosistema AutoPlay USB (autoplay.c, autoplay.ini, LEGGIMI.TXT)
├── boot/                    # Sorgenti Bootloader (bootsect.asm MBR, stage2.asm)
├── bootscreen/              # Sorgenti Bootscreen protetto (BOOTSCR.MOD)
├── BootSetup/               # Programma di installazione con decifratore AES-256 (BOOTSETUP.MOD)
├── calculator/              # Sottosistema Calcolatrice (calculator.c, calculator.ini, LEGGIMI.TXT)
├── control_panel/           # Pannello di Controllo e applet .CPL (display, mouse, sound, power, devmgmt, about)
├── explorer/                # Sottosistema Esplora Risorse (explorer.c, explorer.h, explorer.ini, LEGGIMI.TXT)
├── kernel/                  # Codice sorgente del Kernel 64-bit
│   ├── apps/                # Shell CMD, Regedit, Boot Menu, Launcher loader e header bridge
│   ├── arch/x86_64/         # Syscall, SMP, Scheduler, IDT, PIC, AP trampoline, entry asm, BSOD loader
│   ├── drivers/             # Driver ACPI, PCI/PCIe (256 bus/BAR64), NVMe, Intel HDA, AC97, AHCI, ATA, PS/2, GPU, USB, Net
│   ├── fs/                  # exFAT, NTFS-3G, EXT4, ISO 9660, UDF, RamFS, VFS a 64-bit e Registry cifrato AES-256
│   ├── gui/                 # Window Manager, griglia desktop, menu contestuale, client loader DLL (comctl/theme/cursor/settings/...)
│   ├── ipc/                 # Meccanismi di comunicazione inter-processo (IPC)
│   ├── lib/                 # Librerie freestanding, AES-256, gestione e migrazione profili utente
│   ├── mm/                  # Gestione memoria, heap manager (kmalloc_aligned), paging PML4, mapping MMIO e DMA
│   └── net/                 # Network stack (Ethernet, ARP, IPv4, ICMP, UDP, TCP, DHCP, DNS, TLS)
├── loginscreen/             # Schermata di autenticazione utente (LOGIN.MOD)
├── logoutscreen/            # Schermata di disconnessione e spegnimento (LOGOUT.MOD)
├── MultimediaCodec/         # Moduli Codec indipendenti (.CDC: WAV, MP3, FLAC, AVI, AV1, MP4)
├── notepad/                 # Sottosistema Blocco Note (notepad.c, notepad.ini, LEGGIMI.TXT)
├── oobe/                    # Sottosistema procedura guidata OOBE (oobe.c, oobe.ini, LEGGIMI.TXT)
├── player/                  # Sottosistema NewOS Media Player (player.c, player.ini, LEGGIMI.TXT)
├── Release/                 # Cartella di rilascio ufficiale (BOOTX64.EFI, SYSTEM.PAK, moduli .MOD)
├── resources/               # Icone 32/16 bit, sfondi, schemi cursori, suoni PCM .WAV e font (SANS.TTF, UNICODE.FNT)
├── rules/                   # Regole di comportamento e sviluppo (comportamenti.md)
├── rules_viewer/            # Loader visualizzatore regole .NOR (rules_viewer.c, LEGGIMI.TXT)
├── sources/                 # Snapshot portabile dei sorgenti distribuito nell'immagine ISO
├── SystemLibrary/           # Librerie dinamiche out-of-kernel (.DLL e .INI: comctl, browser, JS/CSS, TLS, UI, driver, app)
├── taskmgr/                 # Sottosistema Gestione Attività (taskmgr.c, taskmgr.ini, LEGGIMI.TXT)
├── third_party/             # Librerie esterne integrate out-of-kernel (QuickJS, NetSurf libcss/libdom/libhubbub, stb, libwebp)
├── tools/                   # Toolchain Node.js/PowerShell (build.js, mkexfat.js, mkiso.js, suite di test QEMU)
├── WebBrowser/              # NewSoft Web Browser nativo
├── whatsnew/                # Loader Novità del Sistema (whatsnew.c, whatsnew.ini, LEGGIMI.TXT)
├── AGENTS.md                # Direttive e vincoli primari del progetto
├── BYPASS_BOOT.md           # Specifica tecnica del bypass automatico del Boot Manager
├── CHECKPOINT.md            # Cronologia dettagliata di tutte le sessioni di sviluppo
├── README.md                # Presentazione e documentazione ufficiale del progetto
├── THEME_SPECIFICATION.md   # Specifica completa dei temi .THEMES/.THM e linguaggio RenderScripts
└── USER_PROFILE_OOBE.md     # Specifica tecnica di migrazione e creazione dinamica del profilo utente in OOBE
```

---

## Compilazione e Avvio

### Prerequisiti
- [NASM](https://www.nasm.us/) (Netwide Assembler)
- [LLVM/Clang](https://llvm.org/) con target `x86_64-unknown-none-elf` e linker `ld.lld`
- [Node.js](https://nodejs.org/) (versione 18 o superiore)
- [QEMU](https://www.qemu.org/) per il collaudo ed emulazione

### Compilazione del Sistema
Per compilare il bootloader BIOS/UEFI, il kernel, i moduli protetti `.MOD`, i codec `.CDC` e le librerie `.DLL`, impacchettare e cifrare `SYSTEM.PAK`, formattare l'unità exFAT e generare i supporti avviabili (`NewOS_V1.iso`, `NewOS_V1.img`, `NewOS_V1.vdi`):
```bash
node tools/build.js
```

### Avvio in QEMU
Per eseguire il sistema operativo in emulazione completa:
```bash
tools\qemu-system-x86_64.exe -cdrom NewOS_V1.iso -m 256M -vga std -device AC97 -netdev user,id=net0 -device e1000,netdev=net0
```

</details>

---

<details>
<summary><strong>🇬🇧 English (EN) — Full Documentation and Guide</strong></summary>

**NewOS V1** is a native 64-bit operating system written in C and Assembly for the **x86_64** architecture, featuring a hybrid protected-kernel design with decoupled *Out-of-Kernel* subsystems and libraries, a multi-theme graphical interface (**Liquid Glass**, **Mica Dark/Light**, and **Classic Windows 2000**), full support for physical MBR/GPT disks and exFAT/NTFS-3G/EXT4, a complete networking stack (IPv4, TCP, UDP, DHCP, DNS, TLS), and an integrated web browser.

---

## Changelog & Recent Fixes

<details>
<summary><strong>Expand to view recent updates and fixes</strong></summary>

### November 22, 2026
- **New Modern Unified Start Menu (`STARTMENU.DLL`) & `Personalization` Selector**:
  - Created the out-of-kernel Start Menu [startmenu_dll.c](SystemLibrary/startmenu_dll.c) ([startmenu_loader.c](kernel/gui/startmenu_loader.c)) with unified live search, **Pinned** (`Preferiti`) grid, **All Apps** (`Tutto`) with **`View: Grid | List`** toggle and **`Nuova`** badges, plus user/quick-actions footer.
  - Added a dedicated **`Start Menu Style: Modern | Classic`** selector inside the **Theme** tab of Display Properties ([display.c](control_panel/display.c)).
- **Window Manager, Taskbar, Toast Notifications & Global Shortcuts (`wm.c`)**:
  - Implemented **Window Snap** (50%/50% left/right edge snap, top-edge maximize), graphical **Alt+Tab** switcher overlay, **Taskbar context menu**, **Taskbar hover tooltips**, **Show Desktop** button, **Toast Notifications**, tray **Notification Center**, and global shortcuts (**`Win+E`**, **`Win+R`**, **`Win+D`**, **`Win+L`**, **`Ctrl+Shift+Esc`**).
- **Bare-Metal Diagnostics & Benchmark (`BENCH.DLL`) & Security Audit Log (`SECCPL.CPL`)**:
  - Added out-of-kernel [bench_dll.c](SystemLibrary/bench_dll.c) (`BENCH.DLL`, launched via `start bench`) with real x86_64 CPUID detection, `RDTSC`-timed RAM bandwidth benchmark (MB/s), CPU score, and VFS scan.
  - Added the **`Audit Log`** tab in [seccpl.c](SystemLibrary/seccpl.c) to inspect LSA/ACL events and policy commits.
- **File Explorer — Multi-Tabs, Live Search Filters, Registry Preferences, `Show` Submenu & Breadcrumb**:
  - Added **multi-tab browsing** (`Ctrl+T`, `Ctrl+W`, `+` / `x` buttons), **live search filter chips** (`[.TXT]`, `[.DLL]`, `[All]`), **Registry user preference persistence** (`HKCU\Software\NewOS\Explorer`), the `Show >` submenu in [explorer.c](explorer/explorer.c), multi-selection, ASC/DESC column sorting, and clickable **Breadcrumb** path segments.
- **Enhanced `CMD` Terminal ([`cmd.c`](kernel/apps/cmd.c))**:
  - Added **command history** (`Up`/`Down` arrows), **`Tab` auto-completion** for commands and `C:\` paths, **output redirection (`>` and `>>`)** to VFS files, and **mouse-wheel scrollback**.
- **TrueType Font Manager (`FONTSCPL.CPL`), Standardized 3rd-Party Installer (`PKGSVC.DLL`) & I/O Ports (`PORTS.DLL`)**:
  - Added out-of-kernel [fontscpl.c](SystemLibrary/fontscpl.c) scanning `.TTF` fonts with live `FONTMGR.DLL` TrueType preview and Registry persistence.
  - Added mandatory [pkgsvc.c](SystemLibrary/pkgsvc.c) package service enforcing isolated installs under `C:\ProgramFile\<App>\` (ACL block on `C:\NewOS\`, `APP.INI` manifest, and `HKLM` registration).
  - Added [ports_dll.c](SystemLibrary/ports_dll.c) with bare-metal `16550A` UART probing (`COM1`–`COM4`), IEEE 1284 legacy parallel ports (`LPT1`–`LPT2`), virtual ports (`VCOM1/2`, `VLPT1`), and HW Loopback test.
- **`.LNK` Shortcuts, HTTP CLI Client (`curl`/`wget`) & Multi-Cluster exFAT Fix**:
  - Added `.LNK` shortcut resolution (`Target=` / `Args=`) in `LAUNCHER.DLL` and `EXPLORER.DLL`, plus `mklnk <file.lnk> <target>` in `CMD`.
  - Added `curl <url>`, `wget <url> [file]`, `nslookup <host>`, and `arp -a` network commands in `CMD` with DNS/TCP exports in `kernel_ext_api.h`.
  - Fixed the 64-entry cap and cluster-boundary spanning directory reads for multi-cluster exFAT folders in `kernel/fs/exfat.c`.
- **Dedicated Image Viewer (`IMGVIEW.DLL`) & Regression Suite (`test_all.js`)**:
  - Added out-of-kernel [imgview_dll.c](SystemLibrary/imgview_dll.c) (`IMGVIEW.DLL`, launched via `start imgview`) supporting `50%–150%` zoom, `90°/180°/270°` rotation, fit-to-window, and `.BMP` library browsing.
  - Added the automated regression suite [test_all.js](tools/test_all.js) (`node tools/test_all.js`, 16 PASS/FAIL checks covering kernel budget, `NOSM` signatures, ISO/IMG artifacts, and QEMU boot).
- **Task Manager (`Moduli DLL`), Notepad (`Find` / `Line Numbers`) & Kernel Budget Guard**:
  - Added the **Moduli DLL** tab in [taskmgr.c](taskmgr/taskmgr.c) scanning `C:\NewOS\SystemLibrary\`.
  - Enhanced [notepad.c](notepad/notepad.c) with live **`Find` (`Ctrl+F`)** highlighting, **`Undo` (`Ctrl+Z`)**, and **optional line numbers**.
  - Added automatic `kernel.bin < 491,520 B (480 KB)` budget enforcement in [build.js](tools/build.js).
- **File Explorer — 8 View Modes, Dynamic Grid Layout & `CTRL + Mouse Wheel`**:
  - Implemented all 8 Windows Explorer-style view modes in [explorer.h](explorer/explorer.h) and [explorer.c](explorer/explorer.c) (`Extra large icons`, `Large icons`, `Medium icons`, `Small icons`, `List`, `Details` with Name/Type/Size/Permissions columns, `Tiles`, and `Content`), selectable from the **View** menu.
  - Added dynamic window-width-aware column calculation with uniform compact spacing (`explorer_get_view_metrics`) and automatic two-line word wrapping for long item names (`explorer_split_name`).
  - Added 4-byte PS/2 IntelliMouse scroll wheel hardware support (`mouse_consume_scroll` in [mouse.c](kernel/drivers/mouse.c)), `KEY_WHEEL_UP`/`KEY_WHEEL_DOWN` codes, and `keyboard_is_ctrl_pressed()` ([keyboard.c](kernel/drivers/keyboard.c), [wm.c](kernel/gui/wm.c)) for normal scrolling or instant view switching via **`CTRL + Mouse Wheel`**.
- **Out-of-Kernel Standard UI Control Library (`COMCTL.DLL`)**:
  - Created the shared control library [comctl.h](SystemLibrary/comctl.h) / [comctl_dll.c](SystemLibrary/comctl_dll.c) ([comctl_loader.c](kernel/gui/comctl_loader.c)) providing modern multi-theme standard controls (`Button`, `Checkbox`, `Radio`, `ToggleSwitch`, `ProgressBar`, `Slider`, `Scrollbar`, `TextBox`, `ComboBox`, `ListView`, `TreeView`, `TabControl`, `Breadcrumb`, `InfoBar`, and a multi-column/multi-type `PropertyGrid`) used by out-of-kernel applications.
- **EXT4 Filesystem Support (`FS_EXT.DLL`) & NVMe TRIM / Deallocate (`NVME.DLL`)**:
  - Added the `EXT4` driver ([ext4.c](kernel/fs/ext4.c), [ext4.h](kernel/fs/ext4.h)) inside the out-of-kernel [fs_ext_dll.c](SystemLibrary/fs_ext_dll.c) module and `NVME_CMD_DSM` (Deallocate/TRIM) support across [nvme_dll.c](SystemLibrary/nvme_dll.c), [ata.c](kernel/drivers/ata.c), [ahci.c](kernel/drivers/ahci.c), and [exfat_ops.c](kernel/fs/exfat_ops.c).
- **Unified Out-of-Kernel Launch Service (`LAUNCHER.DLL` + `launcher_loader.c`)**:
  - Centralized launching of programs, `.CPL` applets, documents, scripts, and critical modules in [launcher_dll.c](SystemLibrary/launcher_dll.c) and [launcher_loader.c](kernel/apps/launcher_loader.c), with `NOSM` + `CRC32` signature verification and integration across [cmd.c](kernel/apps/cmd.c), [wm.c](kernel/gui/wm.c), and [explorer.c](explorer/explorer.c).
- **Security & Permissions Center (`SECCPL.CPL`) & Isolated LSA Registry**:
  - Added the out-of-kernel security suite [seccpl.c](SystemLibrary/seccpl.c) with theme-aligned translucency (`surface_alpha`) and kernel policy management under the isolated `HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Lsa\KernelSecurityPolicy` branch ([registry.c](kernel/fs/registry.c)), protected by `g_kernel_policy_gate` and updatable only through `registry_commit_kernel_policy()`.
  - Implemented VFS ACL enforcement (`vfs_check_acl`, `vfs_is_admin_authenticated`) in [vfs.c](kernel/fs/vfs.c): deleting or renaming items under `C:\NewOS\*` requires administrator authentication (inline password check in `SECCPL.CPL`) and `AllowNewOSDelete=1` in the isolated Registry, alongside critical system file protection (`REGISTRY.DAT`, `NewOS/Boot/*`, `BSOD.DLL`, `LAUNCHER.DLL`, `SECCPL.DLL`).
- **64-bit Filesystem Extension (`> 4 GB`) & Uncapped HD/SSD/NVMe Support**:
  - Converted file sizes, `vfs_seek`/`vfs_tell`/`vfs_file_size` offsets, dentry cache fields, and LBA/FAT/Bitmap addressing to `uint64_t`/`int64_t` in [vfs.h](kernel/fs/vfs.h), [vfs.c](kernel/fs/vfs.c), [vfs_cache.c](kernel/fs/vfs_cache.c), [exfat.c](kernel/fs/exfat.c), and [exfat_ops.c](kernel/fs/exfat_ops.c), replacing the hardcoded `EXFAT_PARTITION_LBA` offset with `vbr->partition_offset`.
  - Removed artificial size fallbacks (`128 MB`, `650 MB`, `16 MB`) in [vfs_mount.c](kernel/fs/vfs_mount.c) and [exfat_ops.c](kernel/fs/exfat_ops.c) in favor of real 64-bit `total_bytes`/`free_bytes` calculations.
  - Integrated the **NVMe** controller (`NVME.DLL`) into the VFS via `ata_register_nvme_device()` in [ata.c](kernel/drivers/ata.c) and `VFS_BUS_NVME`, with automatic GPT/MBR probing up to full namespace capacity.
- **Kernel Heap, VFS Cache & Bare-Metal exFAT Optimizations**:
  - Implemented real `kmalloc_aligned()` in [heap.c](kernel/mm/heap.c) via `kfree()`-compatible in-place block splitting for `4096`-byte alignment of `PML4/PDPT/PD/PT` tables and DMA buffers.
  - Upgraded the VFS cache ([vfs_cache.h](kernel/fs/vfs_cache.h), [vfs_cache.c](kernel/fs/vfs_cache.c)) to **256 sectors / 64 dentries** with a **512-bucket** ($O(1)$) hash table and multi-disk eviction fixes (`ata_write_device_sectors`).
  - Added `g_next_free_hint` with **64-bit** bitmap scanning (`__builtin_ctzll`) and a **zero-copy multi-cluster** fast path in [exfat.c](kernel/fs/exfat.c).

### November 20, 2026
- **Out-of-Kernel Liquid Glass BSOD (`BSOD.DLL` + `KERNEL.SYM`)**:
  - Implemented the diagnostic out-of-kernel Blue Screen of Death in [bsod_dll.c](SystemLibrary/bsod_dll.c) and [bsod_loader.c](kernel/arch/x86_64/bsod_loader.c), preloaded in RAM at boot together with the `KERNEL.SYM` (`NSYM`) symbol table and login background (`LOGIN.BMP` 1024x768) with zero disk or `kmalloc` dependencies during a crash.
  - Wired to all unhandled CPU exceptions in [idt.c](kernel/arch/x86_64/idt.c), dumping full x86_64 registers (`RIP`, `RSP`, `RBP`, `CR2`, `CR3`, `RAX..R15`), stack backtrace with **source file and line resolution**, `COM1` serial logging, and keyboard polling (`[ENTER / R]` Reboot, `[S]` Shutdown).
  - Added the `bsodtest [pf|div0|ud]` diagnostic command in [cmd.c](kernel/apps/cmd.c).
- **Anti-Artifact Resolution, Theme & UI Style Switching Fixes**:
  - In [vga.c](kernel/drivers/vga.c): programmed BGA registers `VBE_DISPI_INDEX_VIRT_WIDTH`, `VIRT_HEIGHT`, `X_OFFSET=0`, `Y_OFFSET=0` (removing `NOCLEARMEM`) to eliminate stride corruption on widescreen resolutions (`1280x720`, etc.).
  - In [graphics.c](kernel/gui/graphics.c): `photo_wallpaper_buf` and `blurred_buf` dynamically resize to `scr_width x scr_height` with `scr_pitch_pixels`-aware row copies, `gfx_clear(0)` cleanup, consistent `gfx_precompute_blur()` for procedural or photographic wallpapers (`active_bg_is_photo`), and rounded `gfx_draw_drop_shadow()` rendering.
  - In [wm.c](kernel/gui/wm.c) and [uistyle_loader.c](kernel/gui/uistyle_loader.c): atomic `wm_render() + gfx_flip()` redraw on every theme, UI Style, or resolution change.
- **Logout Screen Reconnection (`LOGOUT.MOD`) & PIC Fix**:
  - Reconnected the Start Menu **"Shut down..."** action in [wm.c](kernel/gui/wm.c) to `screen_mgr_run_logoutscreen()` with full support for ACPI Power Off, Reboot, Standby/Sleep, Sign Out (returning to `LOGIN.MOD`), and Cancel.
  - Fixed option string addressing in [logoutscreen.c](logoutscreen/logoutscreen.c) using 100% `RIP`-relative PIC code for proper text rendering inside the flat `LOGOUT.MOD` binary.

</details>

---

## Weekly Highlights (November 16 – 19, 2026)

- **Massive Out-of-Kernel Refactoring (29 DLLs in `C:\NewOS\SystemLibrary\`)**: reduced `kernel.bin` from ~326 KB to **~187 KB / 480 KB** by moving applications (`EXPLORER.DLL`, `SYSAPPS.DLL`), standard UI controls (`COMCTL.DLL`), filesystem extensions (`FS_EXT.DLL`), common dialogs (`COMDLG.DLL`), the modern Copenhagen UI style (`COPENHAGEN.DLL`), 2D/DPI acceleration (`HWACCEL.DLL`), TrueType/Unicode font management (`FONTMGR.DLL`), and image codecs (`IMGCODEC.DLL`) out of kernel space.
- **Complete Out-of-Kernel Web Browser Engine**: integrated `WEBBROWSER.DLL`, the NetSurf/Hubbub HTML/DOM engine (`NETSURF_CORE.DLL`), `libcss` (`CSSENGINE.DLL`), QuickJS JavaScript engine (`JSENGINE.DLL`), and HTTPS/TLS layer (`NETTLS.DLL`).
- **Extended Bare-Metal Hardware Support & Kernel Optimization**: added **ACPI** table parsing, **PCI/PCIe scanning up to 256 buses** with **64-bit BAR** decoding, MMIO mapping (`paging_map_mmio`), aligned DMA allocator, and new drivers for **NVMe** (`NVME.DLL`), **Intel HDA** (`AUDIO.DLL`), **Realtek RTL8169/8168**, and **VirtIO-Net** (`NETSTACK.DLL`).
- **Graphics Acceleration & 3D Subsystem (`GPU3D.DLL`)**: introduced **VMware SVGA II**, **VirtIO-GPU**, **Intel GPU** drivers, and a software 3D fallback rasterizer (`soft3d.c`).
- **Tree-Based Device Manager (`DEVMGMT.CPL`)**: revamped Device Manager with categorized hierarchical tree views, expand/collapse nodes, and real PCI/ACPI hardware inspection.
- **Freestanding IDE Configuration**: added `.vscode/c_cpp_properties.json` and `.clangd` aligned with `tools/build.js` to eliminate IntelliSense false positives.

---

## Key Features

### 1. Dual-Mode Hybrid Bootloader (BIOS & UEFI), OOBE & Protected Authentication
- **Legacy BIOS (MBR + Stage 2)**:
  - 16-bit MBR (`boot/bootsect.asm`).
  - 16/32/64-bit Stage 2 (`boot/stage2.asm`) with E820 memory detection, VESA VBE 1024x768 @ 32bpp LFB, A20 line activation, 4-level PML4 paging (4 GB identity mapping with 2 MB Huge Pages), and safe Long Mode 64-bit transition.
  - CHS sector load limit strictly set to 960 sectors (480 KB).
- **Native 64-bit UEFI (`BOOTX64.EFI`)**:
  - UEFI 2.x x86_64 compliant bootloader.
  - FAT32 EFI System Partition (ESP) at LBA 256000 (`Type 0xEF`).
- **Automatic Post-Setup Boot Manager Bypass (`BYPASS_BOOT.md`)**:
  - Once first-time setup is complete (`OOBECompleted=1` / `SetupStage=Completed`), the system skips the interactive boot menu and boots directly into the protected logon screen (`LOGIN.MOD`), while keeping `F8` / `ESC` available for recovery options.
- **Dynamic User Profile Creation & Migration in OOBE (`USER_PROFILE_OOBE.md`)**:
  - During Step 4 of `OOBE.MOD`, the chosen account name is atomically migrated onto the exFAT filesystem (`C:\User\<Username>\`), updating `OOBE.INI`, `USER.CFG`, and the System Registry while pre-filling `LOGIN.MOD` on subsequent boots.

| Splash Screen (`BOOTSCR.MOD`) | OOBE Wizard (`OOBE.MOD`) | Protected Logon (`LOGIN.MOD`) |
| :---: | :---: | :---: |
| <img src="screenshot_update.png" alt="Splash Screen BOOTSCR.MOD" width="280" /> | <img src="screenshot_oobe_step4_mario.png" alt="OOBE User Setup OOBE.MOD" width="280" /> | <img src="screenshot_login_mario.png" alt="Login Screen LOGIN.MOD" width="280" /> |
| *Signed boot module `BOOTSCR.MOD`* | *OOBE Step 4: custom profile creation* | *Direct post-OOBE logon with migrated account* |

---

### 2. Protected Kernel Core (~187 KB / 480 KB)
- **Strict Kernel Budget**: `kernel.bin` stays at **~187 KB (191,916 bytes)** thanks to systematic offloading of subsystems, advanced drivers, browser engines, and libraries into `C:\NewOS\SystemLibrary\`, well within the 480 KB (491,520-byte) limit.
- **Ring 0 / Ring 3 Privilege Separation**:
  - Native `SYSCALL` / `SYSRET` instructions for system calls (`kernel/arch/x86_64/syscall.c`).
  - Virtual memory protection via Supervisor bit in PML4 page tables (`kernel/mm/paging.c`).
- **Multitasking & Multi-Core SMP**:
  - Symmetric Multiprocessing (SMP) support for x86_64 with AP trampoline (`kernel/arch/x86_64/smp.c`).
  - Cooperative and preemptive scheduler with context switching and priority levels (`kernel/arch/x86_64/sched.c`).
  - Secure message-queue Inter-Process Communication (IPC) and shell piping (`kernel/ipc/ipc.c`).
- **Memory & Heap Allocator**:
  - Dynamic system heap (`kmalloc`, `kmalloc_aligned`, `kfree`, `kcalloc`, `krealloc`).
- **Hardware Cryptography & Secure Registry**:
  - Built-in **AES-256** encryption engine (`kernel/lib/aes256.c`).
  - System Registry with read-only pre-desktop access and writes unlocked only during authenticated sessions (`kernel/fs/registry.c`).

---

### 3. Filesystems, Disk Management & Profile Isolation (`C:\User\`)
- **Native exFAT R/W**: 4 KB dynamic cluster allocation, 64-bit bitmap scanning, zero-copy multi-cluster fast path, file/directory creation, modification, renaming, and deletion with synchronized sector cache flush and backup VBR auto-recovery.
- **NTFS-3G R/W**: Full read/write support on NTFS volumes, MFT (Master File Table) parsing, and Data Runs management.
- **EXT4**: Out-of-kernel Linux EXT4 filesystem support via `FS_EXT.DLL`.
- **ISO 9660 & Joliet**: CD/DVD disc support with Unicode UCS-2 encoding.
- **UDF 1.02 & 2.50**: Support for DVD-Video and Blu-Ray optical media.
- **RamFS**: High-speed volatile RAM disk (`R:\`).
- **64-bit GPT & MBR Support (`> 4 GB`)**: Automatic geometry detection and partition table parsing for HDD/SSD/NVMe drives and USB media.

| Root Structure `C:\` in File Explorer | Post-OOBE `C:\User\` Profiles Directory |
| :---: | :---: |
| <img src="screenshot_block_a_desktop.png" alt="Local Disk C: Root" width="410" /> | <img src="screenshot_explorer_user_mario.png" alt="C:\User folder with Mario profile" width="410" /> |
| *System hierarchy in `C:\NewOS`, `C:\ProgramFile` and `C:\User`* | *Atomic user profile migration into `C:\User\Mario`* |

---

### 4. Hardware Drivers, 3D Acceleration & Bare-Metal Peripherals
- **ACPI & Extended PCI/PCIe Bus**: ACPI table parsing (`RSDP`, `XSDT`, `FADT`, `MADT`, `MCFG`), full PCI/PCIe bus scan up to **256 buses** with **64-bit BAR** decoding, MMIO mapping (`paging_map_mmio`), and aligned DMA allocator.
- **Storage**: **NVMe** controller (`nvme.c` + `NVME.DLL` with DSM Deallocate/TRIM), **AHCI SATA** (DMA/Interrupt), and **ATA/IDE** 28-bit PIO + ATAPI for CD/DVD optical drives.
- **PS/2 & USB Input**: PS/2 mouse driver with **4-byte IntelliMouse** scroll wheel support, **USB xHCI / UHCI / OHCI** controllers, HID devices, and USB Mass Storage BOT with automatic AutoPlay and BIOS/OS handoff.
- **Audio**: **Intel HDA** (`hda.c`) and 44.1 kHz stereo **AC97** codec support, with mixer and `.WAV` playback offloaded to `AUDIO.DLL`.
- **Graphics & 3D Acceleration**: **VMware SVGA II**, **VirtIO-GPU**, **Intel GPU** drivers, VESA VBE / Bochs BGA up to 1920x1080 @ 32bpp, software 3D fallback rasterizer (`soft3d.c`), 2D/DPI acceleration (`HWACCEL.DLL`), and out-of-kernel 3D pipeline (`GPU3D.DLL`).
- **Networking**: **Intel PRO/1000 MT (E1000)**, **Realtek RTL8169/8168 Gigabit**, **Realtek RTL8139**, **VirtIO-Net**, and Wi-Fi drivers with Ethernet, ARP, IPv4, ICMP Ping, UDP, TCP, DHCP, native RFC 1035 DNS (`NETSTACK.DLL`), and **TLS/HTTPS** (`NETTLS.DLL`).

---

### 5. Graphical Interface, Dynamic Themes & Desktop Grid System
- **Native Multi-Theme Support (Classic Windows 2000, Liquid Glass, Mica Dark & Light)**:
  - Classic Windows 2000 rendering (`COLOR_BTNFACE = 0xD4D0C8`, 3D bevel/sunken borders) or modern translucent surfaces with alpha blending, parametric corner radii, and specular highlights.
- **Anti-Collision Desktop Grid System**:
  - Grid cell size: 116x88 pixels with perimeter safety margins and dynamic row calculation.
  - Automatic two-line word wrapping for long labels and automatic re-alignment on resolution changes.
- **Independent Self-Updating Context Menu**:
  - Modularized in `kernel/gui/context_menu.c`, dynamically reading file associations from `REGISTRY.DAT` (`.TXT`, `.NOR`, `.WAV`, `.MP3`, `.BMP`, etc.).

---

### 6. Applications, Control Panel & Out-of-Kernel Subsystems
All accessory applications and system tools are organized as independent modules:
- [`explorer/`](explorer/): **File Explorer** (`EXPLORER.DLL`) with 8 Windows-style view modes (`Extra large icons`, `Large icons`, `Medium icons`, `Small icons`, `List`, `Details`, `Tiles`, `Content`), direct **`CTRL + Mouse Wheel`** view switching, dynamic adaptive grid, Web View preview, sidebar Tree View, and address bar.
- [`control_panel/`](control_panel/): **Control Panel** with dual-mode UI (Classic Windows 2000 and Modern Settings/Copenhagen) and applets for Display (`DESK.CPL`), Mouse (`MAIN.CPL`), Audio (`MMSYS.CPL`), Power (`POWERCFG.CPL`), tree-based Device Manager (`DEVMGMT.CPL`), Security & Permissions (`SECCPL.CPL`), and System (`SYSDM.CPL`).
- [`oobe/`](oobe/): **OOBE Setup Wizard** in 6 steps (Welcome, Display, Audio Test, Network, Account, Finish).
- [`notepad/`](notepad/): **Notepad** with text editing, saving, and file opening support.
- [`calculator/`](calculator/): **Calculator** with numeric display and standard operations.
- [`player/`](player/): **NewOS Media Player** with animated audio spectrum visualizer, playlist, and volume control.
- [`rules_viewer/`](rules_viewer/): **Rules Viewer** for `.NOR` system policy documents (via `RULES.DLL`).
- [`whatsnew/`](whatsnew/): **What's New** displaying release history and user features (via `WHATSNEW.DLL`).
- [`autoplay/`](autoplay/): **AutoPlay Manager** for automatic detection and handling of USB drives and discs.
- [`taskmgr/`](taskmgr/): **Task Manager** with CPU and memory usage graphs, process table, and IPC channels.
- [`WebBrowser/`](WebBrowser/): **NewSoft Web Browser** (`WEBBROWSER.DLL`) powered by NetSurf/Hubbub HTML/DOM (`NETSURF_CORE.DLL`), `libcss` (`CSSENGINE.DLL`), QuickJS (`JSENGINE.DLL`), PNG/JPEG/WebP decoding (`IMGCODEC.DLL`), and HTTPS/TLS (`NETTLS.DLL`).

| Modern Settings (`SETTINGS.DLL`) | Mouse Properties & Cursor Schemes (`MAIN.CPL` / `CURSORMGR.DLL`) |
| :---: | :---: |
| <img src="screenshot_cp_modern_scrollbar_top.png" alt="Modern Settings SETTINGS.DLL" width="410" /> | <img src="screenshot_mouse_cpl.png" alt="Mouse Properties MAIN.CPL" width="410" /> |
| *Modern Settings UI rendered out-of-kernel by `SETTINGS.DLL`* | *4-tab Mouse Properties applet with Aero/Glass cursor scheme management* |

| NewOS Media Player (`PLAYER.EXE`) | Classic Windows 2000 Style & AC97 Audio Subsystem |
| :---: | :---: |
| <img src="screenshot_player.png" alt="NewOS Media Player" width="410" /> | <img src="screenshot_start.png" alt="Classic Windows 2000 Theme and AC97 Audio" width="410" /> |
| *`.WAV` media playback with spectrum analyzer and playlist* | *Classic Bevel Windows 2000 theme with AC97 Audio Properties and Command Prompt* |

<details>
<summary><strong>📸 Implemented Features Gallery (Start Menu, Explorer, Window Manager, CPL, Security, CLI & Apps)</strong></summary>

<br/>

#### Modern Start Menu & Personalization (`STARTMENU.DLL`, `DESK.CPL`)
| Modern Grid Start Menu | Live Search in Start Menu | Start Style Selector in `DESK.CPL` |
| :---: | :---: | :---: |
| <img src="docs/screenshots/screenshot_startmenu_modern_grid.png" alt="Modern Grid Start Menu" width="280" /> | <img src="docs/screenshots/screenshot_startmenu_modern_search.png" alt="Start Menu Live Search" width="280" /> | <img src="docs/screenshots/screenshot_display_startmenu_opt.png" alt="Start Style Selector in DESK.CPL" width="280" /> |

#### Enhanced File Explorer (`EXPLORER.DLL`) & Real Recycle Bin (`RECYCLE.DLL`)
| Multiple Tabs (`Ctrl+T` / `Ctrl+W`) | Live Search Filters (`[.TXT]` / `[.DLL]`) | Preview & Details Sidebar | Real Recycle Bin (`C:\Recycle.Bin`) |
| :---: | :---: | :---: | :---: |
| <img src="docs/screenshots/screenshot_explorer_tabs.png" alt="Explorer Multiple Tabs" width="210" /> | <img src="docs/screenshots/screenshot_explorer_search_filter.png" alt="Explorer Search Filters" width="210" /> | <img src="docs/screenshots/screenshot_exp_cat1_preview_details.png" alt="Explorer Preview and Details" width="210" /> | <img src="docs/screenshots/screenshot_recycle_bin_real.png" alt="Real Recycle Bin" width="210" /> |

#### Window Manager, Notifications & Desktop (`wm.c`)
| Window Snap (`50%` Split) | Graphical `Alt+Tab` Switcher | Notification Center & Toasts |
| :---: | :---: | :---: |
| <img src="docs/screenshots/screenshot_wm_cat2_snap.png" alt="Window Snap" width="280" /> | <img src="docs/screenshots/screenshot_wm_cat2_alttab.png" alt="Graphical Alt+Tab" width="280" /> | <img src="docs/screenshots/screenshot_wm_notif_center.png" alt="Notification Center" width="280" /> |

#### Control Panel, TrueType Fonts, COM/LPT Ports & Package Service
| TrueType Fonts (`FONTSCPL.DLL`) | COM/LPT Ports & Loopback (`PORTS.DLL`) | Third-Party Installer (`PKGSVC.DLL`) |
| :---: | :---: | :---: |
| <img src="docs/screenshots/screenshot_fontscpl_applied.png" alt="TrueType Fonts FONTSCPL.DLL" width="280" /> | <img src="docs/screenshots/screenshot_ports_loopback.png" alt="COM and LPT Ports PORTS.DLL" width="280" /> | <img src="docs/screenshots/screenshot_pkgsvc_installed.png" alt="Package Service PKGSVC.DLL" width="280" /> |

#### Security, Bare-Metal Diagnostics & System Applications
| Audit Log (`SECCPL.DLL`) | Hardware Benchmark (`BENCH.DLL`) | Task Manager DLL Modules | Notepad (`Ctrl+F` / Lines) | Image Viewer (`IMGVIEW.DLL`) |
| :---: | :---: | :---: | :---: | :---: |
| <img src="docs/screenshots/screenshot_seccpl_audit_log.png" alt="SECCPL Audit Log" width="170" /> | <img src="docs/screenshots/screenshot_bench_hw.png" alt="BENCH.DLL" width="170" /> | <img src="docs/screenshots/screenshot_taskmgr_dll_modules.png" alt="Task Manager DLL Modules" width="170" /> | <img src="docs/screenshots/screenshot_notepad_find_lines.png" alt="Notepad Find and Lines" width="170" /> | <img src="docs/screenshots/screenshot_imgview_init.png" alt="IMGVIEW.DLL" width="170" /> |

#### Enhanced CMD Terminal, `.LNK` Shortcuts & CLI Networking
| History, Tab & Redirect `>` | `arp -a` & `mklnk` | Launching via `.LNK` Shortcut |
| :---: | :---: | :---: |
| <img src="docs/screenshots/screenshot_cmd_enhanced.png" alt="CMD History Tab Redirect" width="280" /> | <img src="docs/screenshots/screenshot_cmd_net_lnk.png" alt="CMD arp -a and mklnk" width="280" /> | <img src="docs/screenshots/screenshot_lnk_launch.png" alt="Launching via LNK file" width="280" /> |

</details>

---

### 7. Out-of-Kernel Dynamic Libraries (`C:\NewOS\SystemLibrary\`)
Following Architectural Directive (Rule 9), graphical subsystems, advanced drivers, browser engines, and applications run outside Kernel Mode as **29 signed `.DLL` dynamic libraries** (`NOSM` + CRC32) paired with their `.INI` descriptors:
- **Themes, UI, Controls & Graphics**: `THEMEMGR.DLL` (theme engine & `[RenderScripts]`), `CURSORMGR.DLL` (`.CUR`/`.ANI` cursor schemes), `COPENHAGEN.DLL` (modern Copenhagen UI styles), `SETTINGS.DLL` (Modern Settings), `COMCTL.DLL` (standard UI control library), `HWACCEL.DLL` (2D & DPI scaling acceleration), `GPU3D.DLL` (3D graphics acceleration), `FONTMGR.DLL` (TrueType/Unicode fonts `SANS.TTF`/`UNICODE.FNT`), `IMGCODEC.DLL` (PNG/JPEG/WebP/BMP decoding), `COMDLG.DLL` (Open/Save/Browse common dialogs).
- **Applications, Shell & Control Panel**: `EXPLORER.DLL`, `LAUNCHER.DLL`, `SECCPL.DLL`, `BSOD.DLL`, `SYSAPPS.DLL`, `CPLEXT2.DLL`, `RECYCLE.DLL`, `KEYBCPL.DLL`, `NETCPL.DLL`, `TIMEDATE.DLL`, `USERACCT.DLL`, `WHATSNEW.DLL`, `RULES.DLL`.
- **Web Browser & TLS Networking**: `WEBBROWSER.DLL`, `NETSURF_CORE.DLL` (NetSurf/Hubbub HTML/DOM), `CSSENGINE.DLL` (`libcss`), `JSENGINE.DLL` (QuickJS), `NETTLS.DLL` (TLS/HTTPS).
- **Hardware & Filesystem Drivers/Extensions**: `NVME.DLL` (NVMe driver + TRIM), `AUDIO.DLL` (AC97/Intel HDA mixer & `.WAV`), `NETSTACK.DLL` (TCP/IP/DHCP/DNS stack), `FS_EXT.DLL` (NTFS-3G / EXT4 / ISO 9660 / UDF filesystem extensions).

#### Advanced Theme File Engine (`.THEMES` & `.THM`)
NewOS visual customization is **entirely theme-file driven**:
1. **Granular Corner & Border Control**:
   - Independent radii for windows (`CornerRadius`, `BorderRadius`), buttons (`ButtonRadius`, `ButtonBorderRadius`), tabs (`TabRadius`), inputs (`InputRadius`), side panels (`PanelRadius`), taskbar (`TaskbarBtnRadius`), Start button (`StartBtnRadius`), and notification area (`TrayRadius`).
   - Automatic matching flags: `BorderFollowsCorner` and `ControlBorderFollowsRadius`.
2. **Advanced Rendering Script Engine (`[RenderScripts]`) in `THEMEMGR.DLL`**:
   - Declarative pipeline for `TitlebarScript`, `ButtonScript`, `PanelScript`, `ProgressScript`, `SelectionScript`, `TaskbarScript`, `RenderScript`.
   - Supported graphics macros: `SPECULAR_ARC`, `VERTICAL_GLOSS`, `HIGHLIGHT_EDGE`, `INNER_HIGHLIGHT`, `ETCHED_LINE`, `CYLINDER_SHADING`, `AURA_BORDER`, `NOISE_TEXTURE`, `GLASS_TINT`, `GRADIENT`.
   - Full specification available in [`THEME_SPECIFICATION.md`](THEME_SPECIFICATION.md).

---

## Repository Structure

```
├── .agents/                 # AI Agent behavioral and operational directives
├── autoplay/                # USB AutoPlay subsystem (autoplay.c, autoplay.ini, LEGGIMI.TXT)
├── boot/                    # Bootloader sources (bootsect.asm MBR, stage2.asm)
├── bootscreen/              # Protected Bootscreen sources (BOOTSCR.MOD)
├── BootSetup/               # Installer with AES-256 decryptor (BOOTSETUP.MOD)
├── calculator/              # Calculator subsystem (calculator.c, calculator.ini, LEGGIMI.TXT)
├── control_panel/           # Control Panel and .CPL applets (display, mouse, sound, power, devmgmt, about)
├── explorer/                # File Explorer subsystem (explorer.c, explorer.h, explorer.ini, LEGGIMI.TXT)
├── kernel/                  # 64-bit Kernel source code
│   ├── apps/                # CMD shell, Regedit, Boot Menu, Launcher loader and bridge headers
│   ├── arch/x86_64/         # Syscall, SMP, Scheduler, IDT, PIC, AP trampoline, entry asm, BSOD loader
│   ├── drivers/             # ACPI, PCI/PCIe (256 bus/BAR64), NVMe, Intel HDA, AC97, AHCI, ATA, PS/2, GPU, USB, Net
│   ├── fs/                  # exFAT, NTFS-3G, EXT4, ISO 9660, UDF, RamFS, 64-bit VFS and AES-256 Registry
│   ├── gui/                 # Window Manager, desktop grid, context menu, DLL client loaders (comctl/theme/cursor/settings/...)
│   ├── ipc/                 # Inter-Process Communication (IPC) mechanisms
│   ├── lib/                 # Freestanding libs, AES-256, user profile management & migration
│   ├── mm/                  # Memory management, heap manager (kmalloc_aligned), PML4 paging, MMIO mapping & DMA
│   └── net/                 # Network stack (Ethernet, ARP, IPv4, ICMP, UDP, TCP, DHCP, DNS, TLS)
├── loginscreen/             # User authentication screen (LOGIN.MOD)
├── logoutscreen/            # Sign-out and shutdown screen (LOGOUT.MOD)
├── MultimediaCodec/         # Independent Codec modules (.CDC: WAV, MP3, FLAC, AVI, AV1, MP4)
├── notepad/                 # Notepad subsystem (notepad.c, notepad.ini, LEGGIMI.TXT)
├── oobe/                    # OOBE setup wizard subsystem (oobe.c, oobe.ini, LEGGIMI.TXT)
├── player/                  # NewOS Media Player subsystem (player.c, player.ini, LEGGIMI.TXT)
├── Release/                 # Official release directory (BOOTX64.EFI, SYSTEM.PAK, .MOD modules)
├── resources/               # 32/16-bit icons, wallpapers, cursor schemes, PCM .WAV sounds and fonts (SANS.TTF, UNICODE.FNT)
├── rules/                   # Behavioral and development rules (comportamenti.md)
├── rules_viewer/            # .NOR rules viewer loader (rules_viewer.c, LEGGIMI.TXT)
├── sources/                 # Portable source snapshot distributed inside the ISO image
├── SystemLibrary/           # Out-of-kernel dynamic libraries (.DLL & .INI: comctl, browser, JS/CSS, TLS, UI, drivers, apps)
├── taskmgr/                 # Task Manager subsystem (taskmgr.c, taskmgr.ini, LEGGIMI.TXT)
├── third_party/             # Out-of-kernel third-party libraries (QuickJS, NetSurf libcss/libdom/libhubbub, stb, libwebp)
├── tools/                   # Node.js/PowerShell toolchain (build.js, mkexfat.js, mkiso.js, QEMU test suite)
├── WebBrowser/              # Native NewSoft Web Browser
├── whatsnew/                # System What's New loader (whatsnew.c, whatsnew.ini, LEGGIMI.TXT)
├── AGENTS.md                # Primary project directives and constraints
├── BYPASS_BOOT.md           # Technical specification for automatic Boot Manager bypass
├── CHECKPOINT.md            # Detailed log of all development sessions
├── README.md                # Official project overview and documentation
├── THEME_SPECIFICATION.md   # Complete .THEMES/.THM & RenderScripts specification
└── USER_PROFILE_OOBE.md     # Technical specification for dynamic user profile creation & migration in OOBE
```

---

## Build & Run

### Prerequisites
- [NASM](https://www.nasm.us/) (Netwide Assembler)
- [LLVM/Clang](https://llvm.org/) with `x86_64-unknown-none-elf` target and `ld.lld` linker
- [Node.js](https://nodejs.org/) (v18 or higher)
- [QEMU](https://www.qemu.org/) for testing and emulation

### Building the System
To compile the BIOS/UEFI bootloader, kernel, protected `.MOD` modules, `.CDC` codecs, and `.DLL` libraries, package and encrypt `SYSTEM.PAK`, format the exFAT partition, and generate bootable images (`NewOS_V1.iso`, `NewOS_V1.img`, `NewOS_V1.vdi`):
```bash
node tools/build.js
```

### Running in QEMU
To boot the operating system in full emulation:
```bash
tools\qemu-system-x86_64.exe -cdrom NewOS_V1.iso -m 256M -vga std -device AC97 -netdev user,id=net0 -device e1000,netdev=net0
```

</details>

---

## Licenza e Diritti / License & Rights
Copyright (C) @ Newspoint Software - NewSoft - NewOS V1. Tutti i diritti riservati / All rights reserved.
