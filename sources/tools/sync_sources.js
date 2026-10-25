const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..');
const SOURCES_DIR = path.join(ROOT, 'sources');

function copyFolderRecursiveSync(source, target) {
    if (!fs.existsSync(target)) {
        fs.mkdirSync(target, { recursive: true });
    }

    const files = fs.readdirSync(source);
    for (const file of files) {
        // Ignora file binari pesanti e directory temporanee
        if (file === 'node_modules' || file === '.git' || file.endsWith('.o') || 
            file.endsWith('.obj') || file.endsWith('.elf') || file.endsWith('.bin') ||
            file.endsWith('.iso') || file.endsWith('.vdi') || file.endsWith('.img') ||
            file.endsWith('.log') || file.endsWith('.ppm') || (file.startsWith('screenshot') && file.endsWith('.bmp'))) {
            continue;
        }

        const curSource = path.join(source, file);
        const curTarget = path.join(target, file);

        if (fs.lstatSync(curSource).isDirectory()) {
            copyFolderRecursiveSync(curSource, curTarget);
        } else {
            try {
                if (fs.existsSync(curTarget)) {
                    try { fs.chmodSync(curTarget, 0o666); } catch (e) {}
                }
                fs.copyFileSync(curSource, curTarget);
            } catch (e) {}
        }
    }
}

function syncSources() {
    console.log('[SOURCES] Creazione e sincronizzazione cartella sorgenti portatile (sources/)...');

    if (!fs.existsSync(SOURCES_DIR)) {
        fs.mkdirSync(SOURCES_DIR, { recursive: true });
    }

    // 1. Bootloader
    copyFolderRecursiveSync(path.join(ROOT, 'boot'), path.join(SOURCES_DIR, 'boot'));

    // 2. Kernel C & ASM
    copyFolderRecursiveSync(path.join(ROOT, 'kernel'), path.join(SOURCES_DIR, 'kernel'));

    // 2b. Modulo WebBrowser
    copyFolderRecursiveSync(path.join(ROOT, 'WebBrowser'), path.join(SOURCES_DIR, 'WebBrowser'));

    // 2c. Modulo Esplora Risorse (explorer)
    copyFolderRecursiveSync(path.join(ROOT, 'explorer'), path.join(SOURCES_DIR, 'explorer'));
    const oldSourcesExp = path.join(SOURCES_DIR, 'kernel', 'apps', 'explorer.c');
    if (fs.existsSync(oldSourcesExp)) { try { fs.unlinkSync(oldSourcesExp); } catch (e) {} }

    // 2d. Modulo OOBE (Out-of-Box Experience)
    copyFolderRecursiveSync(path.join(ROOT, 'oobe'), path.join(SOURCES_DIR, 'oobe'));
    const oldSourcesOobe = path.join(SOURCES_DIR, 'kernel', 'apps', 'oobe.c');
    if (fs.existsSync(oldSourcesOobe)) { try { fs.unlinkSync(oldSourcesOobe); } catch (e) {} }

    // 2e. Sottosistemi scorporati dal Kernel Core (Applicazioni e Applet)
    const unbundledModules = ['notepad', 'calculator', 'player', 'rules_viewer', 'whatsnew', 'autoplay', 'taskmgr', 'control_panel'];
    for (const mod of unbundledModules) {
        copyFolderRecursiveSync(path.join(ROOT, mod), path.join(SOURCES_DIR, mod));
    }
    // Rimozione vecchi sorgenti C da sources/kernel/apps
    const oldAppsToRemove = [
        'notepad.c', 'calculator.c', 'about.c', 'display.c', 'sound_props.c',
        'power_props.c', 'devmgmt.c', 'taskmgr.c', 'autoplay.c', 'player.c',
        'rules_viewer.c', 'whatsnew.c'
    ];
    for (const oldApp of oldAppsToRemove) {
        const oldP = path.join(SOURCES_DIR, 'kernel', 'apps', oldApp);
        if (fs.existsSync(oldP)) { try { fs.unlinkSync(oldP); } catch (e) {} }
    }

    // 3. Risorse grafiche e audio
    copyFolderRecursiveSync(path.join(ROOT, 'resources'), path.join(SOURCES_DIR, 'resources'));

    // 4. Tools essenziali di compilazione
    const toolsTarget = path.join(SOURCES_DIR, 'tools');
    if (!fs.existsSync(toolsTarget)) fs.mkdirSync(toolsTarget, { recursive: true });

    const essentialTools = [
        'build.js',
        'mkexfat.js',
        'mkiso.js',
        'sync_sources.js',
        'setup_tools.ps1',
        'run.ps1',
        'run_vbox.ps1',
        'make_wallpapers.ps1',
        'bmp2png.ps1'
    ];

    for (const t of essentialTools) {
        const src = path.join(ROOT, 'tools', t);
        if (fs.existsSync(src)) {
            fs.copyFileSync(src, path.join(toolsTarget, t));
        }
    }

    // 5. Script batch di compilazione immediata
    const buildBat = 
`@echo off
echo ==============================================================================
echo Compilazione Portatile NewOS V1 (64-bit)
echo ==============================================================================
node tools\\build.js
pause
`;
    fs.writeFileSync(path.join(SOURCES_DIR, 'build.bat'), buildBat, 'utf8');

    // 6. Script batch di esecuzione
    const runBat = 
`@echo off
echo Avvio NewOS V1 in QEMU...
call tools\\run.ps1
pause
`;
    fs.writeFileSync(path.join(SOURCES_DIR, 'run.bat'), runBat, 'utf8');

    // 7. README dettagliato dei sorgenti
    const readme = 
`# NewOS V1 64-bit — Pacchetto Sorgenti Completo e Portatile
Copyright (C) Newspoint Software - NewSoft - NewOS V1

Questo pacchetto contiene tutti i sorgenti completi, modulari e portatili per lo sviluppo di NewOS V1.

---

## Struttura della Cartella Sorgenti

\`\`\`
sources/
├── boot/                        # Bootloader x86 a 16 e 32-bit (MBR + Stage 2)
│   ├── bootsect.asm             # Settore MBR 512 byte (carica Stage 2)
│   └── stage2.asm               # Passaggio da Real Mode a Protected Mode e Long Mode 64-bit
│
├── kernel/                      # Kernel monolitico modulare a 64-bit (target: x86_64-unknown-none-elf)
│   ├── arch/x86_64/             # Inizializzazione CPU, IDT, PIC, ISR, GDT, Linker Script
│   ├── apps/                    # Applicazioni GUI e console integrate:
│   │   ├── cmd.c / cmd.h        # Prompt dei comandi interattivo con supporto comandi completi
│   │   ├── explorer.c           # Gestore risorse (Risorse del PC, C:\\, Cestino, Pannello di Controllo)
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
│   │   └── registry.c           # Database del Registro di Sistema (C:\\SYSTEM\\REGISTRY.DAT)
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
\`\`\`

---

## Requisiti di Compilazione
1. **Node.js** (versione 16 o superiore)
2. **NASM** (Netwide Assembler)
3. **Clang / LLVM** con target \`x86_64-unknown-none-elf\` e linker \`ld.lld\`

## Istruzioni di Compilazione
Per compilare il sistema operativo ed esportare l'immagine avviabile ISO e VirtualBox VDI:
\`\`\`cmd
build.bat
\`\`\`
oppure:
\`\`\`cmd
node tools\\build.js
\`\`\`

I file binari generati saranno:
- \`NewOS_V1.iso\`: Immagine CD-ROM avviabile conforme ad El Torito.
- \`NewOS_V1.vdi\`: Disco virtuale nativo per Oracle VM VirtualBox.
- \`NewOS_V1.img\`: Immagine disco raw da 32 MB con partizione exFAT nativa.
`;
    fs.writeFileSync(path.join(SOURCES_DIR, 'README.md'), readme, 'utf8');

    console.log(`[SOURCES] Sorgenti sincronizzati con successo in: ${SOURCES_DIR}`);
}

module.exports = { syncSources };

if (require.main === module) {
    syncSources();
}
