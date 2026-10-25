# NewOS V1 (64-bit)

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)]()
[![Arch](https://img.shields.io/badge/architecture-x86__64%20(Long%20Mode)-blue.svg)]()
[![Kernel Size](https://img.shields.io/badge/kernel%20size-312%20KB%20%2F%20480%20KB-brightgreen.svg)]()
[![Dual Boot](https://img.shields.io/badge/boot-BIOS%20MBR%20%2B%20UEFI%2064--bit-blueviolet.svg)]()
[![Filesystem](https://img.shields.io/badge/filesystems-exFAT%20%7C%20NTFS--3G%20%7C%20ISO%209660%20%7C%20UDF%20%7C%20RamFS-purple.svg)]()
[![UI](https://img.shields.io/badge/GUI-Classic%20Windows%202000-lightgrey.svg)]()

**NewOS V1** è un sistema operativo nativo a 64-bit scritto in C ed Assembly per architettura **x86_64**, con architettura ibrida a kernel protetto e sottosistemi applicativi scorporati, interfaccia grafica Classic Windows 2000, supporto completo a dischi fisici MBR/GPT ed exFAT, stack di rete completo (IPv4, TCP, UDP, DHCP, DNS) e browser web integrato.

---

## Caratteristiche Principali

### 1. Bootloader Ibrido Dual-Mode (BIOS & UEFI)
- **BIOS Legacy (MBR + Stage 2)**:
  - MBR a 16-bit (`boot/bootsect.asm`).
  - Stage 2 a 16/32/64-bit (`boot/stage2.asm`) con rilevamento memoria E820, VESA VBE 1024x768 @ 32bpp LFB, abilitazione linea A20, mapping PML4 a 4 livelli (4 GB identity mapping con Huge Pages da 2 MB) e passaggio sicuro a Long Mode 64-bit.
  - Limite di caricamento settori CHS impostato rigorosamente a 960 settori (480 KB).
- **UEFI Nativo 64-bit (`BOOTX64.EFI`)**:
  - Bootloader UEFI conforme alle specifiche UEFI 2.x x86_64.
  - Partizione EFI System Partition (ESP) FAT32 a LBA 256000 (`Type 0xEF`).

### 2. Kernel Core Protetto (312 KB / 480 KB)
- **Budget Kernel Rispettato**: Dimensione di `kernel.bin` ridotta a **319.952 byte (~312 KB)**, ampiamente inferiore al vincolo di 480 KB (491.520 byte).
- **Separazione Privilegi Ring 0 / Ring 3**:
  - Istruzioni native `SYSCALL` e `SYSRET` per le chiamate di sistema (`kernel/arch/x86_64/syscall.c`).
  - Protezione della memoria virtuale con Supervisor bit nelle tabelle PML4 (`kernel/mm/paging.c`).
- **Multitasking & Multi-Core SMP**:
  - Supporto Symmetric Multiprocessing (SMP) per architetture x86_64 con AP trampoline (`kernel/arch/x86_64/smp.c`).
  - Scheduler cooperativo con salvataggio contesti e priorità di esecuzione (`kernel/arch/x86_64/sched.c`).
  - Canali di comunicazione inter-processo IPC a coda di messaggi sicura (`kernel/ipc/ipc.c`).
- **Allocatore di Memoria & Heap**:
  - Heap dinamico di sistema (`kmalloc`, `kfree`, `kcalloc`, `krealloc`).
- **Crittografia Hardware & Registro Sicuro**:
  - Motore di cifratura **AES-256** integrato (`kernel/lib/aes256.c`).
  - Registro di configurazione con accesso in sola lettura pre-desktop e scrittura abilitata solo a sessione autenticata (`kernel/fs/registry.c`).

### 3. File System & Gestione Dischi
- **exFAT Nativo R/W**: Allocazione dinamica cluster a 4 KB, scansione root e sottocartelle, creazione/modifica/cancellazione file e directory.
- **NTFS-3G R/W**: Lettura e scrittura completa su volumi NTFS, parsing della MFT (Master File Table) e gestione Data Runs.
- **ISO 9660 & Joliet**: Supporto per dischi CD/DVD con codifica Unicode UCS-2.
- **UDF 1.02 & 2.50**: Supporto per dischi DVD-Video e Blu-Ray Disc.
- **RamFS**: Unità disco volatile ad alte prestazioni in memoria RAM (`R:\`).
- **Supporto GPT & MBR**: Rilevamento automatico geometrie e tabelle di partizione per dischi e supporti USB.

### 4. Driver Hardware & Periferiche
- **Storage**: Controller ATA/IDE PIO 28-bit e ATAPI per lettori ottici CD/DVD.
- **USB Stack**: Controller xHCI (USB 3.x/USB4), UHCI (USB 1.1), OHCI, periferiche HID (mouse e tastiera) e USB Mass Storage BOT con AutoPlay automatico.
- **Audio**: Codec stereo AC97 a 44.1 kHz con mixer di sistema e riproduzione di file PCM `.WAV`.
- **Grafica**: Driver VESA VBE e Bochs/QEMU BGA fino a 1920x1080 @ 32bpp.
- **Network**: Driver Intel PRO/1000 MT (E1000 Gigabit), Realtek RTL8139 e Wi-Fi con stack Ethernet, ARP, IPv4, ICMP Ping, UDP, TCP, DHCP e DNS nativo RFC 1035.

### 5. Interfaccia Grafica e Desktop Grid System
- **Classic Windows 2000 Styling**:
  - Palette ufficiale (`COLOR_BTNFACE = 0xD4D0C8`), bordi 3D bevel/sunken, barre dei titoli con gradiente e icone alpha-blended 32-bit e 16-bit.
- **Nuovo Sistema a Griglia Desktop Anti-Collisione**:
  - Dimensioni cella griglia: 116x88 pixel, con margini perimetrali di sicurezza.
  - Calcolo dinamico delle righe disponibili in base alla risoluzione dello schermo e alla barra delle applicazioni.
  - Word-wrapping automatico a due righe per etichette superiori a 12 caratteri (es. *"Prompt"* / *"Comandi"*, *"Installa NewOS"* / *"V1"*).
  - Bounding box di click e selezione centrato a 96x68 pixel privo di sovrapposizioni tra colonne.
  - Voce *"Disponi icone"* nel menu contestuale del Desktop e riallineamento automatico su cambio risoluzione.
- **Menu Contestuale Indipendente & Autoaggiornante**:
  - Scorporato dal kernel (`kernel/gui/context_menu.c`), legge dinamicamente le associazioni file da `REGISTRY.DAT` (.TXT, .NOR, .WAV, .MP3, .BMP, ecc.) con auto-dimensionamento e margini anti-overflow.

### 6. Applicazioni e Sottosistemi Scorporati
Tutte le applicazioni accessorie e i tool di sistema sono stati estratti dal kernel e organizzati come moduli indipendenti:
- [`explorer/`](file:///c:/Users/morga/Desktop/MyOS/explorer/): **Esplora Risorse** con Web View preview, Tree View laterale, barra indirizzi, dialoghi di proprietà e menu contestuale.
- [`oobe/`](file:///c:/Users/morga/Desktop/MyOS/oobe/): **Procedura Guidata OOBE** in 6 passi (Benvenuto, Schermo, Test Audio, Rete, Account, Conclusione).
- [`notepad/`](file:///c:/Users/morga/Desktop/MyOS/notepad/): **Blocco Note** con supporto editing testo, salvataggio e apertura file.
- [`calculator/`](file:///c:/Users/morga/Desktop/MyOS/calculator/): **Calcolatrice Digitale** con display numerico ed operazioni standard.
- [`player/`](file:///c:/Users/morga/Desktop/MyOS/player/): **NewOS Media Player** con visualizzatore spettro sonoro animato, playlist e controllo volume.
- [`rules_viewer/`](file:///c:/Users/morga/Desktop/MyOS/rules_viewer/): **Visualizzatore Regole** per documenti e policy di sistema in formato `.NOR`.
- [`whatsnew/`](file:///c:/Users/morga/Desktop/MyOS/whatsnew/): **Novità del Sistema** con storico release e funzionalità per gli utenti.
- [`autoplay/`](file:///c:/Users/morga/Desktop/MyOS/autoplay/): **Gestione AutoPlay** per rilevamento e apertura automatica di chiavette USB e dischi.
- [`taskmgr/`](file:///c:/Users/morga/Desktop/MyOS/taskmgr/): **Gestione Attività** con grafici di utilizzo CPU e memoria, tabella processi e canali IPC.
- [`control_panel/`](file:///c:/Users/morga/Desktop/MyOS/control_panel/): **Pannello di Controllo** con applet per Proprietà Schermo, Audio, Alimentazione, Gestione Dispositivi e Informazioni di Sistema.
- [`WebBrowser/`](file:///c:/Users/morga/Desktop/MyOS/WebBrowser/): **NewSoft Web Browser** con motore di rendering HTML integrato e navigazione web.

---

## Struttura del Repository

```
├── .agents/                 # Direttive comportamentali e operative dell'Agente AI
├── autoplay/                # Sottosistema AutoPlay USB (autoplay.c, autoplay.ini, LEGGIMI.TXT)
├── boot/                    # Sorgenti Bootloader (bootsect.asm MBR, stage2.asm)
├── bootscreen/              # Sorgenti Bootscreen protetto (BOOTSCR.MOD)
├── BootSetup/               # Programma di installazione con decifratore AES-256 (BOOTSETUP.MOD)
├── calculator/              # Sottosistema Calcolatrice (calculator.c, calculator.ini, LEGGIMI.TXT)
├── control_panel/           # Pannello di Controllo e applet .CPL (display, sound, power, devmgmt, about)
├── explorer/                # Sottosistema Esplora Risorse (explorer.c, explorer.ini, LEGGIMI.TXT)
├── kernel/                  # Codice sorgente del Kernel monolitico 64-bit
│   ├── apps/                # Header bridge a zero overhead per compatibilità retroattiva
│   ├── arch/x86_64/         # Syscall, SMP, Scheduler, IDT, PIC, AP trampoline, entry asm
│   ├── drivers/             # Driver AC97, ATA, PCI, VGA, Power, Timer, USB (xHCI/UHCI), Net (E1000/Wi-Fi)
│   ├── fs/                  # exFAT, NTFS-3G, ISO 9660, UDF, RamFS, VFS e Registry cifrato
│   ├── gui/                 # Window Manager, griglia desktop, menu contestuale, rendering 2D e font
│   ├── ipc/                 # Meccanismi di comunicazione inter-processo (IPC)
│   ├── lib/                 # Librerie freestanding, AES-256, gestione utenti
│   ├── mm/                  # Gestione memoria, heap manager e paging PML4 a 4 livelli
│   └── net/                 # Network stack (Ethernet, ARP, IPv4, ICMP, UDP, TCP, DHCP, DNS)
├── loginscreen/             # Schermata di autenticazione utente (LOGIN.MOD)
├── logoutscreen/            # Schermata di disconnessione e spegnimento (LOGOUT.MOD)
├── notepad/                 # Sottosistema Blocco Note (notepad.c, notepad.ini, LEGGIMI.TXT)
├── oobe/                    # Sottosistema procedura guidata OOBE (oobe.c, oobe.ini, LEGGIMI.TXT)
├── player/                  # Sottosistema NewOS Media Player (player.c, player.ini, LEGGIMI.TXT)
├── Release/                 # Cartella di rilascio ufficiale (BOOTX64.EFI, SYSTEM.PAK, ecc.)
├── resources/               # Icone 32/16 bit, sfondi, suoni PCM .WAV e font bitmap
├── rules/                   # Regole di comportamento e sviluppo (comportamenti.md)
├── rules_viewer/            # Sottosistema visualizzatore regole .NOR (rules_viewer.c, LEGGIMI.TXT)
├── sources/                 # Snapshot portabile dei sorgenti distribuito nell'immagine ISO
├── taskmgr/                 # Sottosistema Gestione Attività (taskmgr.c, taskmgr.ini, LEGGIMI.TXT)
├── tools/                   # Toolchain Node.js/PowerShell (build.js, tree_manager.js, sync_sources.js)
├── WebBrowser/              # NewSoft Web Browser nativo
├── whatsnew/                # Sottosistema Novità del Sistema (whatsnew.c, whatsnew.ini, LEGGIMI.TXT)
├── AGENTS.md                # Direttive e vincoli primari del progetto
├── CHECKPOINT.md            # Cronologia dettagliata di tutte le sessioni di sviluppo
└── README.md                # Presentazione e documentazione ufficiale del progetto
```

---

## Compilazione e Avvio

### Prerequisiti
- [NASM](https://www.nasm.us/) (Netwide Assembler)
- [LLVM/Clang](https://llvm.org/) con target `x86_64-unknown-none-elf` e linker `ld.lld`
- [Node.js](https://nodejs.org/) (versione 18 o superiore)
- [QEMU](https://www.qemu.org/) per il collaudo ed emulazione

### Compilazione del Sistema
Per compilare il bootloader BIOS/UEFI, il kernel, i moduli protetti, impacchettare e cifrare `SYSTEM.PAK`, formattare l'unità exFAT e generare i supporti avviabili (`NewOS_V1.iso`, `NewOS_V1.img`, `NewOS_V1.vdi`):
```bash
node tools/build.js
```

### Avvio in QEMU
Per eseguire il sistema operativo in emulazione completa:
```bash
tools\qemu-system-x86_64.exe -cdrom NewOS_V1.iso -m 256M -vga std -device AC97 -netdev user,id=net0 -device e1000,netdev=net0
```

---

## Licenza e Diritti
Copyright (C) @ Newspoint Software - NewSoft - NewOS V1. Tutti i diritti riservati.
