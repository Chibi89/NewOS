# NewOS V1 64-bit — Pacchetto Sorgenti Completo e Portatile
Copyright (C) Newspoint Software - NewSoft - NewOS V1

Questo pacchetto contiene tutti i sorgenti completi, modulari e portatili per lo sviluppo di NewOS V1.

---

## Struttura della Cartella Sorgenti

```
sources/
├── boot/                        # Bootloader x86 a 16 e 32-bit (MBR + Stage 2)
│   ├── bootsect.asm             # Settore MBR 512 byte (carica Stage 2)
│   └── stage2.asm               # Passaggio da Real Mode a Protected Mode e Long Mode 64-bit
│
├── kernel/                      # Kernel monolitico modulare a 64-bit (target: x86_64-unknown-none-elf)
│   ├── arch/x86_64/             # Inizializzazione CPU, IDT, PIC, ISR, GDT, Linker Script
│   ├── apps/                    # Applicazioni GUI e console integrate:
│   │   ├── cmd.c / cmd.h        # Prompt dei comandi interattivo con supporto comandi completi
│   │   ├── explorer.c           # Gestore risorse (Risorse del PC, C:\, Cestino, Pannello di Controllo)
│   │   ├── notepad.c            # Blocco Note con editor di testo
│   │   ├── calculator.c         # Calcolatrice scientifica/standard
│   │   ├── devmgmt.c            # Gestione periferiche e driver hardware (.SYS + .INI)
│   │   ├── regedit.c            # Editor del Registro di Sistema (REGEDIT.EXE) con avviso di sicurezza
│   │   ├── display.c            # Risoluzioni video dinamiche (800x600..1080p) e sfondi
│   │   ├── sound_props.c        # Controllo volume e mixer AC'97
│   │   ├── power_props.c        # Opzioni risparmio energia e standby
│   │   ├── installer.c          # Wizard di installazione guidata su disco fisso
│   │   └── about.c              # Informazioni sul sistema operativo
│   ├── drivers/                 # Sottosistema modulare dei driver (.SYS / .INI):
│   │   ├── driver_mgr.c         # Manager di caricamento e registrazione periferiche
│   │   ├── vga.c                # Driver video VBE BGA per risoluzioni dinamiche a 32bpp
│   │   ├── ac97.c / sound.c     # Driver audio Intel AC'97 e altoparlante PC
│   │   ├── ata.c                # Controller dischi IDE/ATA e CD-ROM ATAPI
│   │   ├── keyboard.c / mouse.c # Driver di input PS/2 tastiera e mouse
│   │   ├── rtc.c                # Orologio in tempo reale CMOS
│   │   ├── power.c              # Gestione alimentazione ACPI/APM
│   │   └── net/                 # Driver schede di rete fisiche:
│   │       ├── e1000.c          # Intel 82540EM Gigabit Ethernet (DMA allineato 128 byte)
│   │       ├── rtl8139.c        # Realtek Fast Ethernet
│   │       └── net_nic.c        # Astrazione hardware scheda di rete
│   ├── net/                     # Stack di rete completo RFC:
│   │   ├── eth.c                # Framing Ethernet II
│   │   ├── arp.c                # Tabella ARP dinamica
│   │   ├── ipv4.c               # Routing IP e checksum
│   │   ├── icmp.c               # Invio e risposta ping ICMP Echo reale con lettura TTL
│   │   ├── udp.c                # Datagrammi UDP
│   │   ├── tcp.c                # Connessioni affidabili TCP e handshake a 3 vie
│   │   ├── dhcp.c               # Negoziazione dinamica lease IP/Subnet/Gateway/DNS
│   │   ├── tls.c                # Substrato di sicurezza e hashing SHA-256
│   │   └── socket.c             # BSD Socket API
│   ├── fs/                      # File System ed I/O:
│   │   ├── exfat.c / exfat.h    # Driver Microsoft exFAT 1.00 nativo
│   │   ├── vfs.c / vfs.h        # Virtual File System universale
│   │   └── registry.c           # Database del Registro di Sistema (C:\SYSTEM\REGISTRY.DAT)
│   ├── gui/                     # Sottosistema grafico Window Manager (WM) e font:
│   │   ├── graphics.c           # Rendering primitive 2D, alpha blending, liquid glass
│   │   ├── wm.c                 # Gestione finestre, taskbar, start menu, icone desktop
│   │   ├── font.c               # Render font bitmap 8x16 leggibile
│   │   └── icons.c              # Repository icone di sistema a 16x16 e 32x32 pixel
│   ├── mm/                      # Gestione Memoria:
│   │   └── heap.c               # Allocatore dinamico heap kernel da 64 MB (kmalloc/kfree)
│   └── kernel.c                 # Entry point principale e bootstrap dei sottosistemi
│
├── resources/                   # Sfondi, suoni .WAV e icone sorgente
├── tools/                       # Strumenti di build, formattazione exFAT e ISO
│   ├── build.js                 # Script principale di compilazione automatica
│   ├── mkexfat.js               # Formattatore e generatore partizione Microsoft exFAT
│   ├── mkiso.js                 # Generatore ISO ibrida El Torito CD-ROM + MBR
│   └── sync_sources.js          # Sincronizzazione automatica del pacchetto sorgenti
├── build.bat                    # Script di compilazione per Windows (un clic)
└── README.md                    # Questa documentazione
```

---

## Requisiti di Compilazione
1. **Node.js** (versione 16 o superiore)
2. **NASM** (Netwide Assembler)
3. **Clang / LLVM** con target `x86_64-unknown-none-elf` e linker `ld.lld`

## Istruzioni di Compilazione
Per compilare il sistema operativo ed esportare l'immagine avviabile ISO e VirtualBox VDI:
```cmd
build.bat
```
oppure:
```cmd
node tools\build.js
```

I file binari generati saranno:
- `NewOS_V1.iso`: Immagine CD-ROM avviabile conforme ad El Torito.
- `NewOS_V1.vdi`: Disco virtuale nativo per Oracle VM VirtualBox.
- `NewOS_V1.img`: Immagine disco raw da 32 MB con partizione exFAT nativa.
