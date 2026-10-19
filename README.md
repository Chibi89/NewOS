# NewOS V1 (64-bit)

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)]()
[![Arch](https://img.shields.io/badge/architecture-x86__64%20(Long%20Mode)-blue.svg)]()
[![Kernel Size](https://img.shields.io/badge/kernel%20size-471%20KB%20%2F%20480%20KB-orange.svg)]()
[![Filesystem](https://img.shields.io/badge/filesystems-exFAT%20%7C%20NTFS--3G%20%7C%20ISO%209660%20%7C%20UDF-purple.svg)]()
[![UI](https://img.shields.io/badge/GUI-Classic%20Windows%202000-lightgrey.svg)]()

**NewOS V1** è un sistema operativo monolitico nativo a 64-bit scritto in C ed Assembly per architettura x86_64, progettato con un'interfaccia grafica in stile Windows 2000, supporto completo a file system moderni (exFAT, NTFS-3G, ISO 9660/Joliet, UDF 1.02/2.50) e stack di rete avanzato.

---

## Caratteristiche Principali

- **Bootloader a 2 Stadi**:
  - MBR a 16-bit (`boot/bootsect.asm`)
  - Stage 2 a 16/32/64-bit (`boot/stage2.asm`) con rilevamento memoria E820, VESA VBE 1024x768x32bpp LFB, abilitazione A20, setup tabelle di paginazione PML4 a 4 livelli (4 GB identity mapping con Huge Pages da 2 MB) e passaggio a Long Mode 64-bit.
- **Kernel Monolitico Freestanding**:
  - Dimensione kernel rigorosamente ottimizzata sotto i **480 KB (471.740 byte)**.
  - Gestione interruzioni IDT e driver 8259 PIC.
  - Allocatore di memoria heap dinamico (`kmalloc` / `kfree`).
  - Timer PIT 8254 a 100 Hz, Real-Time Clock & CMOS, PC Speaker e Power Management ACPI.
- **Driver Hardware & Periferiche**:
  - **Storage**: ATA/IDE PIO 28-bit e ATAPI per CD-ROM, DVD e Blu-Ray Disc.
  - **USB Stack**: Controller xHCI (USB 3.x/USB4), UHCI (USB 1.1), OHCI, periferiche HID (mouse e tastiera) e USB Mass Storage BOT (Bulk-Only Transport).
  - **Audio**: Codec AC97 stereo a 44.1 kHz con riproduzione reale di file audio PCM `.WAV`.
  - **Grafica**: VESA VBE e Bochs/QEMU BGA fino a 1920x1080 @ 32bpp.
  - **Rete**: Driver Intel PRO/1000 MT (E1000 Gigabit), Realtek RTL8139 e Wi-Fi 7 / 6E.
- **Supporto File System Completo**:
  - **exFAT**: Driver nativo completo con lettura, scrittura, allocazione cluster e directory entries.
  - **NTFS-3G**: Driver completo con supporto sia in **lettura che in scrittura (R/W)**, parsing MFT (Master File Table), attributi standard e Data Runs (resident e non-resident).
  - **ISO 9660 & Joliet**: Supporto per dischi CD/DVD dati con codifica estesa UCS-2 Unicode.
  - **UDF 1.02 & 2.50**: Supporto per supporti DVD-Video e Blu-Ray Disc (BDMV).
  - **RamFS**: Unità disco in memoria RAM (`R:\`).
  - **Riconoscimento Automatico USB**: Rilevamento automatico di file system (NTFS, exFAT, FAT32) all'inserimento di chiavette e dischi USB.
- **Interfaccia Grafica e Window Manager**:
  - Look & Feel Classic Windows 2000 con palette ufficiale `COLOR_BTNFACE = 0xD4D0C8`, bordi 3D bevel/sunken ed icone 32-bit ARGB.
  - **Esplora Risorse (Explorer)**:
    - Barra dei menu a tendina (`File`, `Modifica`, `Visualizza`, `Strumenti`, `?`)
    - Visualizzazione ad albero delle cartelle (`Tree View`)
    - Menu contestuale con tasto destro del mouse
    - Dialogo modale *Proprietà* con tab Generale, dimensioni e attributi
    - Filtro di ricerca in tempo reale
    - Barra degli indirizzi editabile e scrollbar proporzionali
  - **Applicazioni**: Prompt dei comandi (CMD con 25 comandi), Blocco note, Calcolatrice, Gestione periferiche, Editor del Registro di Sistema, Proprietà schermo, Proprietà audio, AutoPlay USB.
- **Procedura Guidata OOBE (Out-Of-Box Experience)**:
  - Wizard iniziale pre-desktop per test display, audio AC97, connettività rete e personalizzazione profilo utente.
- **Sicurezza e Moduli Protetti**:
  - Moduli esterni `.MOD` (`BOOTSCR.MOD`, `LOGIN.MOD`, `LOGOUT.MOD`, `BOOTSETUP.MOD`) firmati crittograficamente con checksum CRC32.
  - Archivio cifrato **AES-256** `SYSTEM.PAK` per l'installazione.
  - File di regole ufficiali di sistema con formato proprietario `.NOR`.
  - Registro di configurazione con accesso in sola lettura pre-desktop e scrittura abilitata solo a sessione autenticata.

---

## Struttura del Repository

```
├── .agents/                 # Regole comportamentali per l'agente AI (comportamenti.md)
├── boot/                    # Sorgenti Bootloader (bootsect.asm, stage2.asm)
├── bootscreen/              # Sorgenti Bootscreen (BOOTSCR.MOD)
├── BootSetup/               # Programma di installazione con decifratore AES-256 (BOOTSETUP.MOD)
├── kernel/                  # Codice sorgente del Kernel monolitico 64-bit
│   ├── apps/                # Applicazioni grafiche integrate (Explorer, CMD, Notepad, OOBE, ecc.)
│   ├── arch/x86_64/         # Strato architettura, IDT, PIC, entry assembly, linker script
│   ├── drivers/             # Driver AC97, ATA, PCI, VGA, Power, Timer, USB (xHCI/UHCI), Net (E1000/Wi-Fi)
│   ├── fs/                  # File system exFAT, NTFS-3G, ISO 9660, UDF, RamFS, VFS, Registry
│   ├── gui/                 # Window Manager, rendering 2D, font bitmap, icone, moduli grafici
│   ├── lib/                 # Librerie freestanding, crittografia AES-256, gestione utenti
│   ├── mm/                  # Allocatore heap dinamico
│   └── net/                 # Network stack (Ethernet, ARP, IPv4, ICMP Ping, UDP, TCP, DHCP, TLS)
├── loginscreen/             # Schermata di autenticazione utente (LOGIN.MOD)
├── logoutscreen/            # Schermata di disconnessione e spegnimento (LOGOUT.MOD)
├── resources/               # Icone, wallpaper, suoni PCM .WAV e font
├── rules/                   # Regole di comportamento e sviluppo (comportamenti.md)
├── Rules Win App/           # Applicazione host Windows per visualizzazione regole .NOR
├── sources/                 # Snapshot portabile dei sorgenti distribuito nell'immagine ISO
├── tools/                   # Toolchain Node.js/PowerShell per compilazione, packaging e test QEMU
├── AGENTS.md                # Direttive principali agentic AI
├── CHECKPOINT.md            # Checkpoint cronologico dettagliato dello stato di avanzamento
└── STRUTTURA_PROGETTO.md    # Inventario completo di tutti i file e sottocartelle
```

---

## Compilazione e Avvio

### Prerequisiti
- [NASM](https://www.nasm.us/) (Netwide Assembler)
- [LLVM/Clang](https://llvm.org/) con target `x86_64-unknown-none-elf` e linker `ld.lld`
- [Node.js](https://nodejs.org/) (versione 18 o superiore)
- [QEMU](https://www.qemu.org/) per il collaudo in emulazione

### Compilazione del Sistema
Per compilare il bootloader, il kernel, i moduli protetti, cifrare il pacchetto `SYSTEM.PAK` e generare l'immagine ISO avviabile:
```bash
node tools/build.js
```

### Avvio in QEMU
Per eseguire il sistema operativo in emulazione:
```bash
tools\qemu-system-x86_64.exe -cdrom NewOS_V1.iso -m 128M -vga std -device AC97 -netdev user,id=net0 -device e1000,netdev=net0
```

---

## Licenza e Diritti
Copyright (C) @ Newspoint Software - NewSoft - NewOS V1. Tutti i diritti riservati.
