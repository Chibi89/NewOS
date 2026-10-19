const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..');
const outputFile = path.join(ROOT, 'STRUTTURA_PROGETTO.md');

const ignoreList = ['.git', 'node_modules', '.gemini'];

function formatBytes(bytes) {
    if (bytes === 0) return '0 B';
    const k = 1024;
    const sizes = ['B', 'KB', 'MB', 'GB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return (bytes / Math.pow(k, i)).toFixed(1) + ' ' + sizes[i];
}

console.log('[TREE GEN] Scansione ricorsiva di tutti i file e cartelle in:', ROOT);

function scan(dir) {
    let items = [];
    let entries;
    try {
        entries = fs.readdirSync(dir);
    } catch(e) {
        return items;
    }
    entries.sort((a, b) => a.localeCompare(b, undefined, { sensitivity: 'base' }));

    for (const name of entries) {
        if (ignoreList.includes(name)) continue;
        const fullPath = path.join(dir, name);
        let stat;
        try {
            stat = fs.statSync(fullPath);
        } catch(e) {
            continue;
        }

        const relPath = path.relative(ROOT, fullPath).replace(/\\/g, '/');
        if (stat.isDirectory()) {
            items.push({
                type: 'dir',
                relPath: relPath,
                name: name,
                size: 0
            });
            items = items.concat(scan(fullPath));
        } else {
            items.push({
                type: 'file',
                relPath: relPath,
                name: name,
                size: stat.size
            });
        }
    }
    return items;
}

const allItems = scan(ROOT);
const totalDirs = allItems.filter(i => i.type === 'dir').length;
const totalFiles = allItems.filter(i => i.type === 'file').length;
const totalBytes = allItems.filter(i => i.type === 'file').reduce((acc, cur) => acc + cur.size, 0);

console.log(`[TREE GEN] Scansione completata: ${totalDirs} cartelle, ${totalFiles} file (${formatBytes(totalBytes)} totali).`);

let md = [];

md.push('# Struttura Completa del Progetto — NewOS V1 (64-bit)\n');
md.push(`**Data Generazione**: ${new Date().toLocaleString('it-IT')}`);
md.push(`**Cartella Root**: \`${ROOT}\``);
md.push(`**Statistiche Progetto**: **${totalDirs}** cartelle, **${totalFiles}** file totali (**${formatBytes(totalBytes)}** complessivi).\n`);
md.push('---\n');

md.push('## 1. Mappa e Ruolo delle Cartelle Primarie\n');
md.push('| Cartella Primaria | Descrizione e Scopo nel Progetto |');
md.push('|---|---|');
md.push('| [`.agents/`](file:///c:/Users/morga/Desktop/MyOS/.agents/) | Regole, comportamenti e personalizzazioni per l\'agente AI pair programmer (`rules/comportamenti.md`). |');
md.push('| [`boot/`](file:///c:/Users/morga/Desktop/MyOS/boot/) | Codice sorgente del bootloader: MBR a 16-bit (`bootsect.asm`) e Stage 2 a 16/32/64-bit (`stage2.asm`). |');
md.push('| [`bootscreen/`](file:///c:/Users/morga/Desktop/MyOS/bootscreen/) | Schermata grafica di caricamento avvio compilata come modulo protetto firmato (`BOOTSCR.MOD`). |');
md.push('| [`BootSetup/`](file:///c:/Users/morga/Desktop/MyOS/BootSetup/) | Programma di installazione grafica guidata su disco fisico e decifratore AES-256 (`BOOTSETUP.MOD`). |');
md.push('| [`build/`](file:///c:/Users/morga/Desktop/MyOS/build/) | Directory di output build contenente binari intermedi (`obj/`), immagini disco (`NewOS_V1.iso`, `myos.img`, `NewOS_V1.vdi`) e l\'albero `bin/` pronto per il packaging. |');
md.push('| [`kernel/`](file:///c:/Users/morga/Desktop/MyOS/kernel/) | Codice sorgente del Kernel monolitico 64-bit: architettura (`arch/x86_64`), driver hardware (`drivers/`), file system (`fs/`), grafica/GUI (`gui/`), librerie interne (`lib/`), memoria (`mm/`), networking (`net/`) e applicazioni integrate (`apps/`). |');
md.push('| [`loginscreen/`](file:///c:/Users/morga/Desktop/MyOS/loginscreen/) | Modulo protetto della schermata di autenticazione e accesso credenziali utente (`LOGIN.MOD`). |');
md.push('| [`logoutscreen/`](file:///c:/Users/morga/Desktop/MyOS/logoutscreen/) | Modulo protetto di disconnessione e arresto sistema (`LOGOUT.MOD`). |');
md.push('| [`resources/`](file:///c:/Users/morga/Desktop/MyOS/resources/) | Repository risorse del sistema: icone 32x32 e 16x16, wallpaper, suoni PCM .WAV e font bitmap. |');
md.push('| [`rules/`](file:///c:/Users/morga/Desktop/MyOS/rules/) | Regole comportamentali e operative per l\'ambiente e l\'assistente AI (`comportamenti.md`). |');
md.push('| [`Rules Win App/`](file:///c:/Users/morga/Desktop/MyOS/Rules%20Win%20App/) | Applicazione host Windows nativa per visualizzare e validare le 5 regole proprietarie `.NOR` di NewOS. |');
md.push('| [`sources/`](file:///c:/Users/morga/Desktop/MyOS/sources/) | Snapshot sincronizzato dell\'intero albero dei codici sorgenti portabili distribuito all\'interno della ISO. |');
md.push('| [`tools/`](file:///c:/Users/morga/Desktop/MyOS/tools/) | Script di automazione Node.js e PowerShell per compilazione, assemblaggio MBR/Stage2, crittografia AES-256, formattazione exFAT/ISO 9660, conversione immagini e collaudo QEMU. |');
md.push('\n---\n');

md.push('## 2. Dettaglio Architettura Sorgenti del Kernel (`kernel/`)\n');
md.push('```');
md.push('kernel/');
md.push('├── apps/                   # Applicazioni grafiche integrate');
md.push('│   ├── about.c/h           # Informazioni su NewOS');
md.push('│   ├── autoplay.c/h        # Gestore AutoPlay per USB e dischi ottici (Casi 1-4)');
md.push('│   ├── boot_menu.c/h       # Menu grafico di avvio iniziale (Live / Installa)');
md.push('│   ├── calculator.c/h      # Calcolatrice da tavolo');
md.push('│   ├── cmd.c/h             # Prompt dei comandi (interprete shell con 25 comandi)');
md.push('│   ├── devmgmt.c/h         # Gestione periferiche e bus');
md.push('│   ├── display.c/h         # Proprieta dello schermo, risoluzione e temi');
md.push('│   ├── explorer.c/h        # Esplora Risorse (Menu, Albero, Cerca, Proprieta, Drag)');
md.push('│   ├── installer.c/h       # Interfaccia grafica di installazione');
md.push('│   ├── notepad.c/h         # Blocco note editor di testo');
md.push('│   ├── oobe.c/h            # Configurazione iniziale guidata pre-desktop (6 passi)');
md.push('│   ├── power_props.c/h     # Opzioni risparmio energia');
md.push('│   ├── regedit.c/h         # Editor del registro di configurazione');
md.push('│   ├── rules_viewer.c/h    # Visualizzatore integrato regole proprietarie .NOR');
md.push('│   └── sound_props.c/h     # Controllo volume e proprieta audio');
md.push('├── arch/x86_64/            # Strato architettura x86_64 Long Mode');
md.push('│   ├── cpu.c/h             # CPUID, MSR, cr0/cr3/cr4');
md.push('│   ├── entry.asm           # Punto di ingresso kernel 64-bit (0x100000)');
md.push('│   ├── idt.c/h             # Interrupt Descriptor Table');
md.push('│   ├── io.h                # I/O ports inb/outb/inw/outw/inl/outl');
md.push('│   ├── isr_stubs.asm       # ISR low-level assembly stubs');
md.push('│   ├── linker.ld           # Linker script kernel LLD ELF64 piatto');
md.push('│   └── pic.c/h             # Programmable Interrupt Controller (8259 PIC)');
md.push('├── drivers/                # Driver hardware nativi');
md.push('│   ├── ac97.c/h            # Audio AC97 stereo');
md.push('│   ├── ata.c/h             # Storage ATA/IDE PIO 28-bit e ATAPI CD/DVD/BD');
md.push('│   ├── driver_mgr.c/h      # Gestore modulare driver e configurazioni .INI');
md.push('│   ├── keyboard.c/h        # Tastiera PS/2 e layout');
md.push('│   ├── mouse.c/h           # Mouse PS/2 standard 3 tasti');
md.push('│   ├── pci.c/h             # Bus PCI scansione ed enumerazione periferiche');
md.push('│   ├── power.c/h           # Power Management ACPI, shutdown, reboot, standby');
md.push('│   ├── rtc.c/h             # Real-Time Clock & CMOS');
md.push('│   ├── sound.c/h           # Sintesi suoni ed effetti sonori');
md.push('│   ├── sound_data.c/h      # Tabelle campioni audio');
md.push('│   ├── sound_wav.c/h       # Parser file audio PCM .WAV reali da disco');
md.push('│   ├── speaker.c/h         # PC Speaker integrato (PIT timer 2)');
md.push('│   ├── timer.c/h           # PIT Timer 8254 100 Hz IRQ0');
md.push('│   ├── vga.c/h             # Scheda video VBE/BGA Bochs');
md.push('│   ├── net/                # Schede di rete Ethernet e Wi-Fi');
md.push('│   │   ├── e1000.c/h       # Intel PRO/1000 Gigabit');
md.push('│   │   ├── rtl8139.c/h     # Realtek RTL8139 Fast Ethernet');
md.push('│   │   ├── wifi.c/h        # Controller Wi-Fi 7 / 6E');
md.push('│   │   └── net_nic.c/h     # Astrazione schede di rete');
md.push('│   └── usb/                # Stack USB completo');
md.push('│       ├── uhci.c/h        # Host controller USB 1.1 UHCI');
md.push('│       ├── ohci.c/h        # Host controller USB 1.1 OHCI');
md.push('│       ├── xhci.c/h        # Host controller USB 3.x/USB4 xHCI');
md.push('│       ├── usb.c/h         # Core stack USB, enumerazione e descrittori');
md.push('│       ├── usb_hid.c/h     # Periferiche input HID (mouse e tastiere)');
md.push('│       ├── usb_hub.c/h     # Gestione hub USB');
md.push('│       ├── usb_msc.c/h     # Mass Storage Class protocollo SCSI BOT');
md.push('│       └── usb_net.c/h     # Adattatori di rete USB');
md.push('├── fs/                     # Sottosistema File System e VFS');
md.push('│   ├── exfat.c/h           # File System exFAT nativo completo');
md.push('│   ├── exfat_ops.c/h       # Operazioni estese su file/directory exFAT');
md.push('│   ├── fs_probe.c/h        # Euristica rilevamento firme MBR/VBR dischi USB');
md.push('│   ├── iso9660.c/h         # File System ISO 9660 e Joliet UCS-2 Unicode');
md.push('│   ├── ntfs.c/h            # File System NTFS-3G completo (Lettura e Scrittura R/W)');
md.push('│   ├── ramfs.c/h           # Ramdisk in memoria per unita R:');
md.push('│   ├── registry.c/h        # Registro di configurazione di sistema');
md.push('│   ├── udf.c/h             # File System UDF 1.02 / 2.50 per DVD e Blu-Ray');
md.push('│   ├── vfs.c/h             # Virtual File System universale');
md.push('│   ├── vfs_cache.c/h       # Cache blocchi VFS');
md.push('│   └── vfs_mount.c/h       # Punti di montaggio unita (A:, C:, D:, R:, U:)');
md.push('├── gui/                    # Interfaccia Utente Grafica (GUI)');
md.push('│   ├── font.c/h            # Rendering font bitmap VGA e proporzionale');
md.push('│   ├── graphics.c/h        # Primitive grafiche, rettangoli, gradienti, linee');
md.push('│   ├── icons.c/h           # Decodifica e disegno icone');
md.push('│   ├── icons_data.asm      # Repository binario icone 32x32 e 16x16');
md.push('│   ├── image.c/h           # Parser immagini bitmap BMP');
md.push('│   ├── screen_mgr.c/h      # Gestore e caricatore moduli protetti .MOD');
md.push('│   └── wm.c/h              # Window Manager Classic Win2000 (Z-order, drag, click)');
md.push('├── include/                # Header globali e tipi di base');
md.push('├── lib/                    # Funzioni di supporto freestanding');
md.push('│   ├── aes256.c/h          # Cifratura e decifratura simmetrica AES-256');
md.push('│   ├── string.c/h          # Implementazione standard string/memoria freestanding');
md.push('│   └── user.c/h            # Gestione profili utente e credenziali');
md.push('├── mm/                     # Gestione della Memoria');
md.push('│   └── heap.c/h            # Allocatore dinamico kernel (kmalloc/kfree)');
md.push('├── net/                    # Network Protocol Stack');
md.push('│   ├── arp.c/h             # Address Resolution Protocol');
md.push('│   ├── dhcp.c/h            # Dynamic Host Configuration Protocol');
md.push('│   ├── eth.c/h             # Framing Ethernet II');
md.push('│   ├── icmp.c/h            # Internet Control Message Protocol (Echo Ping)');
md.push('│   ├── ipv4.c/h            # Internet Protocol versione 4');
md.push('│   ├── socket.c/h          # Astrazione socket');
md.push('│   ├── tcp.c/h             # Transmission Control Protocol');
md.push('│   ├── tls.c/h             # TLS Crypto Substrate');
md.push('│   └── udp.c/h             # User Datagram Protocol');
md.push('└── kernel.c                # Entrypoint principale ed orchestra iniziale');
md.push('```\n');
md.push('---\n');

md.push('## 3. Inventario Completo di Tutti i File e Cartelle nel Progetto\n');
md.push('Questo elenco cataloga ricorsivamente la posizione di **ciascuna cartella e file** all\'interno del workspace NewOS V1:\n');
md.push('| Tipo | Percorso Relativo dal Root | Dimensione |');
md.push('|:---:|---|---:|');

for (const item of allItems) {
    if (item.type === 'dir') {
        md.push(`| **[DIR]** | \`${item.relPath}/\` | *cartella* |`);
    } else {
        md.push(`| [FILE] | \`${item.relPath}\` | ${formatBytes(item.size)} |`);
    }
}

const content = md.join('\n');
fs.writeFileSync(outputFile, content, 'utf8');
console.log(`[TREE GEN] File generato con successo: ${outputFile} (${formatBytes(fs.statSync(outputFile).size)})`);
