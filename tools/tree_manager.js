/**
 * tools/tree_manager.js
 * Modulo per la generazione, organizzazione e popolamento della struttura C:\ e build\bin\
 * Direttive ufficiali NewOS V1:
 * C:\NewOS\Driver, Resources, Resource\cursor, Resource\wallpapers, Font, Help, Diagnostic, Themes, Language, SystemProgram, OOBE
 * C:\User, C:\ProgramFile
 */

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

function createPeStub(name, type = 'App') {
    const buf = Buffer.alloc(1024, 0);
    buf[0] = 0x4D; buf[1] = 0x5A; // MZ
    buf.writeUInt32LE(0x80, 0x3C);
    buf[0x80] = 0x50; buf[0x81] = 0x45; // PE\0\0
    buf.writeUInt16LE(0x8664, 0x84); // Machine: x86_64
    buf.writeUInt16LE(1, 0x86);      // 1 section
    buf.writeUInt16LE(0x020B, 0x98); // PE32+
    buf.writeUInt32LE(0x00000200, 0x98 + 16);
    buf[0x200] = 0x31; buf[0x201] = 0xC0; buf[0x202] = 0xC3; // xor eax, eax; ret
    return buf;
}

function createDriverSys(name, typeId, ioBase, irq) {
    const buf = Buffer.alloc(1024, 0);
    buf[0] = 0x4D; buf[1] = 0x5A;
    buf.writeUInt32LE(0x80, 0x3C);
    buf[0x80] = 0x50; buf[0x81] = 0x45;
    buf.writeUInt16LE(0x8664, 0x84);
    buf.writeUInt16LE(2, 0x86);
    buf.writeUInt32LE(0x20261018, 0x88);
    buf.writeUInt16LE(0x00F0, 0x94);
    buf.writeUInt16LE(0x2000, 0x96);
    buf.writeUInt16LE(0x020B, 0x98);
    buf.writeUInt32LE(0x00000200, 0x98 + 16);
    buf.writeBigUInt64LE(0x0000000140000000n, 0x98 + 24);
    buf.writeUInt16LE(0x0001, 0x98 + 68);

    buf.write('.text\0\0\0', 0x180, 8, 'ascii');
    buf.writeUInt32LE(0x0200, 0x188);
    buf.writeUInt32LE(0x1000, 0x18C);
    buf.writeUInt32LE(0x0200, 0x190);
    buf.writeUInt32LE(0x0200, 0x194);
    buf.writeUInt32LE(0x60000020, 0x19C);

    buf.write('.data\0\0\0', 0x1A8, 8, 'ascii');
    buf.writeUInt32LE(0x0200, 0x1B0);
    buf.writeUInt32LE(0x2000, 0x1B4);
    buf.writeUInt32LE(0x0200, 0x1B8);
    buf.writeUInt32LE(0x0400, 0x1BC);
    buf.writeUInt32LE(0xC0000040, 0x1C4);

    buf.writeUInt32LE(0x53595331, 0x200); // "SYS1"
    buf.write(name.padEnd(32, '\0'), 0x204, 32, 'ascii');
    buf.writeUInt32LE(typeId, 0x224);
    buf.writeUInt16LE(ioBase, 0x228);
    buf.writeUInt8(irq, 0x22A);
    buf.writeUInt8(1, 0x22B);
    buf[0x240] = 0x31; buf[0x241] = 0xC0; buf[0x242] = 0xC3;
    return buf;
}

function createNorFile(ruleId, category, severity, text) {
    const NOR_VERSION = 0x00010000;
    const NOR_HEADER_SIZE = 128;
    const NOR_FLAG_DESKTOP_ONLY = 0x00000001;
    const NOR_CIPHER_KEY = Buffer.from('NewSoft-NewOS-RuleSecurityKey-26', 'ascii');

    const plainBuf = Buffer.from(text, 'utf8');
    const payloadSize = plainBuf.length;

    let crc = 0xFFFFFFFF;
    for (let i = 0; i < plainBuf.length; i++) {
        crc ^= plainBuf[i];
        for (let j = 0; j < 8; j++) {
            crc = (crc >>> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
        }
    }
    const payloadCrc32 = (crc ^ 0xFFFFFFFF) >>> 0;

    const header = Buffer.alloc(128, 0);
    header.write('NOSR', 0, 4, 'ascii');
    header.writeUInt32LE(NOR_VERSION, 4);
    header.writeUInt32LE(NOR_HEADER_SIZE, 8);
    header.writeUInt32LE(NOR_FLAG_DESKTOP_ONLY, 12);
    header.write(ruleId.padEnd(32, '\0'), 16, 32, 'ascii');
    header.write(category.padEnd(32, '\0'), 48, 32, 'ascii');
    header.write(severity.padEnd(16, '\0'), 80, 16, 'ascii');
    header.write('NEWSOFT-RULE-V1\0', 96, 16, 'ascii');
    header.writeUInt32LE(20261018, 112);
    header.writeUInt32LE(payloadCrc32, 116);
    header.writeUInt32LE(payloadSize, 120);
    header.writeUInt32LE(0, 124);

    const cipherBuf = Buffer.alloc(payloadSize);
    for (let i = 0; i < payloadSize; i++) {
        cipherBuf[i] = plainBuf[i] ^ NOR_CIPHER_KEY[i % 32] ^ ((i * 11 + 0x6A) & 0xFF);
    }

    return Buffer.concat([header, cipherBuf]);
}

const DRIVERS_SPEC = [
    { file: 'PCI', typeId: 0, io: 0x0CF8, irq: 0, dma: 0, name: 'NewSoft PCI Bus Enumerator', type: 'Bus', vendor: '0x0000', device: '0x0000', settings: 'ScanSlots=32\nMaxBuses=256' },
    { file: 'ATA', typeId: 1, io: 0x01F0, irq: 14, dma: 0, name: 'NewSoft IDE/ATA Storage Controller', type: 'Storage', vendor: '0x0000', device: '0x0001', settings: 'Mode=PIO4\nDMA=Supported\nSectorsPerTransfer=1' },
    { file: 'VGA', typeId: 2, io: 0x01CE, irq: 0, dma: 0, name: 'NewSoft Advanced VGA & BGA Dynamic Video Driver', type: 'Display', vendor: '0x1234', device: '0x1111', settings: 'Resolution=1024x768\nColorDepth=32\nDoubleBuffer=1' },
    { file: 'AC97', typeId: 3, io: 0x1000, irq: 11, dma: 0, name: 'NewSoft AC97 Audio Subsystem', type: 'Audio', vendor: '0x8086', device: '0x2415', settings: 'SampleRate=44100\nChannels=2\nMasterVolume=85' },
    { file: 'PS2MOUSE', typeId: 4, io: 0x0060, irq: 12, dma: 0, name: 'NewSoft PS/2 Mouse Driver', type: 'Mouse', vendor: '0x0000', device: '0x0002', settings: 'Sensitivity=3\nAcceleration=Normal\nScrollWheel=1' },
    { file: 'PS2KBD', typeId: 5, io: 0x0060, irq: 1, dma: 0, name: 'NewSoft PS/2 Keyboard Driver', type: 'Keyboard', vendor: '0x0000', device: '0x0001', settings: 'Layout=IT\nRepeatDelay=500\nRepeatRate=30' },
    { file: 'RTC', typeId: 6, io: 0x0070, irq: 8, dma: 0, name: 'NewSoft Real-Time Clock & CMOS Subsystem', type: 'Timer', vendor: '0x0000', device: '0x0004', settings: 'Format=24h\nTimeZone=Europe/Rome\nNTP=0' },
    { file: 'POWER', typeId: 7, io: 0x0604, irq: 9, dma: 0, name: 'NewSoft Power Management Subsystem', type: 'Power', vendor: '0x0000', device: '0x0005', settings: 'APM=Enabled\nACPI=Enabled\nSleepTimeout=15\nDisplayStandby=10' },
    { file: 'E1000', ext: 'SYS', typeId: 8, io: 0x0000, irq: 11, dma: 0, name: 'Intel PRO/1000 MT Gigabit Network Adapter', type: 'Network', vendor: '0x8086', device: '0x100E', settings: 'LinkSpeed=1000Mbps\nDuplex=Full\nFlowControl=1' },
    { file: 'RTL8139', ext: 'SYS', typeId: 8, io: 0xC000, irq: 10, dma: 0, name: 'Realtek RTL8139 Fast Ethernet Subsystem', type: 'Network', vendor: '0x10EC', device: '0x8139', settings: 'LinkSpeed=100Mbps\nDuplex=Full\nRxBufferSize=8192' },
    { file: 'WIFI7', ext: 'SYS', typeId: 8, io: 0xE000, irq: 11, dma: 0, name: 'Wi-Fi 7 / 6E Ultra-Gigabit Wireless Adapter', type: 'Network', vendor: '0x8086', device: '0x272B', settings: 'MaxThroughput=5.0GB/s\nNegotiationPolicy=StrictHardwareLimit\nBands=6GHz,5GHz,2.4GHz\nMLO=Enabled\nChannelWidth=320MHz' },
    { file: 'USBXHCI', ext: 'SYS', typeId: 0, io: 0x0000, irq: 11, dma: 0, name: 'USB 3.x/USB4 xHCI Host Controller', type: 'Bus', vendor: '0x8086', device: '0x0B80', settings: 'Specs=xHCI 1.2\nMaxBandwidth=40Gbps\nAutoDetect=Enabled\nRootHubPorts=4' },
    { file: 'USBUHCI', ext: 'SYS', typeId: 0, io: 0xD000, irq: 11, dma: 0, name: 'USB 1.1 UHCI Host Controller', type: 'Bus', vendor: '0x8086', device: '0x7112', settings: 'Specs=UHCI 1.1\nMaxBandwidth=12Mbps\nFrameList=4KB\nPorts=2' },
    { file: 'USBOHCI', ext: 'SYS', typeId: 0, io: 0x0000, irq: 10, dma: 0, name: 'USB 1.1 OHCI Host Controller', type: 'Bus', vendor: '0x106B', device: '0x003F', settings: 'Specs=OpenHCI 1.1\nMaxBandwidth=12Mbps\nMMIO=Enabled\nPorts=2' },
    { file: 'USBHID', ext: 'SYS', typeId: 5, io: 0x0000, irq: 0, dma: 0, name: 'USB Generic HID Foundation Driver', type: 'Keyboard', vendor: '0x0000', device: '0x0003', settings: 'BootProtocol=Enabled\nSubclass=BootInterface\nDevices=Keyboard,Mouse' },
    { file: 'USBMSC', ext: 'SYS', typeId: 1, io: 0x0000, irq: 0, dma: 0, name: 'USB Generic Mass Storage BOT Subsystem', type: 'Storage', vendor: '0x0000', device: '0x0008', settings: 'Protocol=BulkOnlyTransport\nSubclass=SCSI_Transparent\nBlockSize=512' },
    { file: 'USBHUB', ext: 'SYS', typeId: 0, io: 0x0000, irq: 0, dma: 0, name: 'USB Generic Hub Foundation Driver', type: 'Bus', vendor: '0x0000', device: '0x0009', settings: 'Class=0x09\nPorts=4\nPowerSwitching=Ganged' },
    { file: 'USBNET', ext: 'SYS', typeId: 8, io: 0x0000, irq: 11, dma: 0, name: 'USB Wireless & Ethernet Dongle Subsystem', type: 'Network', vendor: '0x0BDA', device: '0x8852', settings: 'MaxThroughput=5.0GB/s\nBusConstraint=AutoNegotiated\nLinkBridge=Enabled' },
    { file: 'ETH', ext: 'SYS.NET', typeId: 8, io: 0x0000, irq: 0, dma: 0, name: 'Ethernet II & ARP Framing Engine', type: 'Network', vendor: '0x0000', device: '0x0010', settings: 'MTU=1500\nFraming=DIX_II\nArpCacheSize=16' },
    { file: 'IP', ext: 'SYS.NET', typeId: 8, io: 0x0000, irq: 0, dma: 0, name: 'IPv4 & ICMP Network Layer Subsystem', type: 'Network', vendor: '0x0000', device: '0x0011', settings: 'Version=4\nTTL=64\nFragmentation=Disabled\nIcmpEcho=Enabled' },
    { file: 'UDP', ext: 'SYS.NET', typeId: 8, io: 0x0000, irq: 0, dma: 0, name: 'UDP Transport Protocol Subsystem', type: 'Network', vendor: '0x0000', device: '0x0012', settings: 'MaxSockets=16\nChecksum=RFC768' },
    { file: 'TCP', ext: 'SYS.NET', typeId: 8, io: 0x0000, irq: 0, dma: 0, name: 'TCP Reliable Stream Protocol Engine', type: 'Network', vendor: '0x0000', device: '0x0013', settings: 'WindowSize=4096\nHandshake=3Way\nCongestionControl=Reno' },
    { file: 'DHCP', ext: 'SYS.NET', typeId: 8, io: 0x0000, irq: 0, dma: 0, name: 'Dynamic Host Configuration Client', type: 'Network', vendor: '0x0000', device: '0x0014', settings: 'ClientPort=68\nServerPort=67\nTimeout=3' },
    { file: 'TLS', ext: 'SYS.NET', typeId: 8, io: 0x0000, irq: 0, dma: 0, name: 'TLS Security & Cryptographic Substrate', type: 'Network', vendor: '0x0000', device: '0x0015', settings: 'Cipher=SHA256_HMAC\nTLS_Version=1.3' }
];

function organizeBuildBin(rootDir, binDir) {
    console.log('\n--> Organizzazione struttura NewOS V1 in ' + binDir + '...');

    const dirsToCreate = [
        'NewOS/Driver/NET',
        'NewOS/Resources/Wallpapers',
        'NewOS/Resources/cursor',
        'NewOS/Resources/Sounds',
        'NewOS/Resources/Screens',
        'NewOS/Resources/Icons',
        'NewOS/Resources/Web',
        'NewOS/Resource/cursor',
        'NewOS/Resource/wallpapers',
        'NewOS/Font',
        'NewOS/Help',
        'NewOS/Diagnostic',
        'NewOS/Themes',
        'NewOS/Language',
        'NewOS/SystemProgram',
        'NewOS/OOBE',
        'NewOS/Registry',
        'NewOS/Rules',
        'BootSetup',
        'User/Morgan/Desktop/PROGETTI',
        'User/Default',
        'NewOS/Dev/CONFIG',
        'NewOS/Dev/INCLUDE',
        'NewOS/Dev/TEMP',
        'NewOS/Dev/TMP',
        'NewOS/Dev/TCC/CONFIG',
        'NewOS/Dev/TCC/INCLUDE',
        'NewOS/Dev/TCC/TEMP',
        'NewOS/Dev/TCC/TMP',
        'ProgramFile'
    ];

    for (const d of dirsToCreate) {
        const fullPath = path.join(binDir, d);
        if (!fs.existsSync(fullPath)) {
            fs.mkdirSync(fullPath, { recursive: true });
        }
    }

    // 1. Popola Driver (NewOS/Driver/ e NET/)
    const driverDir = path.join(binDir, 'NewOS', 'Driver');
    const netDir = path.join(driverDir, 'NET');
    for (const d of DRIVERS_SPEC) {
        const ext = d.ext || 'SYS';
        const sysName = `${d.file}.${ext}`;
        const iniName = `${d.file}.INI`;
        const sysBuf = createDriverSys(sysName, d.typeId, d.io, d.irq);
        const iniContent = `[Driver]\nName=${d.name}\nVersion=1.0.0\nAuthor=Newspoint Software - NewSoft\nType=${d.type}\nFilename=${sysName}\nVendorID=${d.vendor}\nDeviceID=${d.device}\nAutoStart=1\nStatus=Active\n\n[Hardware]\nIRQ=${d.irq}\nIO_Base=0x${d.io.toString(16).toUpperCase().padStart(4, '0')}\nDMA=${d.dma}\n\n[Settings]\n${d.settings}\n`;

        fs.writeFileSync(path.join(driverDir, sysName), sysBuf);
        fs.writeFileSync(path.join(driverDir, iniName), iniContent);

        if (d.type === 'Network') {
            fs.writeFileSync(path.join(netDir, sysName), sysBuf);
            fs.writeFileSync(path.join(netDir, iniName), iniContent);
        }
    }
    fs.writeFileSync(path.join(driverDir, 'LEGGIMI.TXT'), 'NewOS V1 - Sottosistema Driver (C:\\NewOS\\Driver\\)\n');

    // 2. Popola Wallpapers e Risorse
    const wpDir = path.join(rootDir, 'resources', 'wallpaper');
    const dstWp1 = path.join(binDir, 'NewOS', 'Resources', 'Wallpapers');
    const dstWp2 = path.join(binDir, 'NewOS', 'Resource', 'wallpapers');
    const wpFiles = ['HERO.BMP', 'HERO.JPG', 'FLOW.BMP', 'FLOW.JPG', 'BLISS.BMP', 'BLISS.JPG', 'THEME_A.BMP', 'THEME_A.JPG'];
    for (const wf of wpFiles) {
        const src = path.join(wpDir, wf);
        if (fs.existsSync(src)) {
            fs.copyFileSync(src, path.join(dstWp1, wf));
            fs.copyFileSync(src, path.join(dstWp2, wf));
        }
    }

    // Cursori del mouse
    const curDst1 = path.join(binDir, 'NewOS', 'Resources', 'cursor');
    const curDst2 = path.join(binDir, 'NewOS', 'Resource', 'cursor');
    const curInfo = "NewOS V1 Cursor Graphics Resource Data\nFormat: 32x32 RGBA / Monochrome 1-bit mask\n";
    for (const c of ['ARROW.CUR', 'BEAM.CUR', 'WAIT.CUR', 'HAND.CUR']) {
        fs.writeFileSync(path.join(curDst1, c), curInfo);
        fs.writeFileSync(path.join(curDst2, c), curInfo);
    }

    // Suoni
    const sndDir = path.join(rootDir, 'resources', 'sounds');
    const dstSnd = path.join(binDir, 'NewOS', 'Resources', 'Sounds');
    const sndMap = {
        'STARTUP.WAV': 'Windows Startup.wav',
        'RECYCLE.WAV': 'Windows Recycle.wav',
        'DING.WAV': 'ding.wav',
        'CHORD.WAV': 'chord.wav',
        'TADA.WAV': 'tada.wav'
    };
    for (const [dst, srcName] of Object.entries(sndMap)) {
        const sp = path.join(sndDir, srcName);
        if (fs.existsSync(sp)) {
            fs.copyFileSync(sp, path.join(dstSnd, dst));
        }
    }

    // Screens (Immagini ufficiali Bootscreen, Loginscreen, Logoutscreen)
    const dstScreens = path.join(binDir, 'NewOS', 'Resources', 'Screens');
    const screenImgs = [
        { src: 'bootscreen/bootScreen_1024.jpg', dst: 'BOOTSCR.JPG' },
        { src: 'bootscreen/bootScreen_1024.bmp', dst: 'BOOTSCR.BMP' },
        { src: 'bootscreen/bootScreen.jpeg', dst: 'BOOTSCR.ORIG.JPG' },
        { src: 'loginscreen/loginscreen_1024.jpg', dst: 'LOGIN.JPG' },
        { src: 'loginscreen/loginscreen_1024.bmp', dst: 'LOGIN.BMP' },
        { src: 'logoutscreen/logoutscreen_1024.jpg', dst: 'LOGOUT.JPG' },
        { src: 'logoutscreen/logoutscreen_1024.bmp', dst: 'LOGOUT.BMP' }
    ];
    for (const si of screenImgs) {
        const sp = path.join(rootDir, si.src);
        if (fs.existsSync(sp)) {
            fs.copyFileSync(sp, path.join(dstScreens, si.dst));
        }
    }

    // Icone
    const iconSrc = path.join(rootDir, 'resources', 'icons.bin');
    if (fs.existsSync(iconSrc)) {
        fs.copyFileSync(iconSrc, path.join(binDir, 'NewOS', 'Resources', 'Icons', 'ICONS.BIN'));
    }

    // Risorse Web per il Web Browser (C:\NewOS\Resources\Web\)
    const webDst = path.join(binDir, 'NewOS', 'Resources', 'Web');
    if (!fs.existsSync(webDst)) fs.mkdirSync(webDst, { recursive: true });
    const webFiles = [
        { src: 'WebBrowser/home.htm', dst: 'HOME.HTM' },
        { src: 'WebBrowser/about.htm', dst: 'ABOUT.HTM' },
        { src: 'WebBrowser/webbrowser.ini', dst: 'BROWSER.INI' }
    ];
    for (const wf of webFiles) {
        const sp = path.join(rootDir, wf.src);
        if (fs.existsSync(sp)) {
            fs.copyFileSync(sp, path.join(webDst, wf.dst));
        }
    }

    // 3. Popola Font (C:\NewOS\Font\)
    const fontDir = path.join(binDir, 'NewOS', 'Font');
    fs.writeFileSync(path.join(fontDir, 'SYSTEM.FNT'), "NewOS V1 System Bitmap Font 8x16 ISO-8859-1 Western Latin\n");
    fs.writeFileSync(path.join(fontDir, 'VGA.FNT'), "NewOS V1 Standard VGA Font 8x8 Hardware ROM\n");
    fs.writeFileSync(path.join(fontDir, 'CONSOLE.FNT'), "NewOS V1 FixedSys Terminal Console Font 8x16 Monospace\n");
    fs.writeFileSync(path.join(fontDir, 'LEGGIMI.TXT'), "Cartella dei tipi di carattere (Font) di sistema di NewOS V1 (C:\\NewOS\\Font\\).\n");

    // 4. Popola Help (C:\NewOS\Help\)
    const helpDir = path.join(binDir, 'NewOS', 'Help');
    const manualeContent =
`================================================================
Manuale Utente Ufficiale - NewOS V1 (64-bit Edition)
(C) Newspoint Software - NewSoft - NewOS V1
================================================================

1. Architettura di Sistema:
   - Kernel monolitico 64-bit x86_64 in Long Mode
   - File System Microsoft exFAT nativo su partizione C:\\
   - Moduli protetti esterni firmati con CRC32 (.MOD)

2. Struttura Cartelle:
   - C:\\NewOS\\Driver         : Driver di sistema (.SYS e .INI)
   - C:\\NewOS\\Resources      : Sfondi, suoni, icone, grafiche
   - C:\\NewOS\\Resource\\cursor : Puntatori del mouse
   - C:\\NewOS\\Font           : Font e caratteri
   - C:\\NewOS\\Help           : Guide e documentazione
   - C:\\NewOS\\Diagnostic     : Utility di test hardware
   - C:\\NewOS\\Themes         : Temi grafici (Classic, Liquid Glass)
   - C:\\NewOS\\Language       : Pacchetti di localizzazione (IT, EN)
   - C:\\NewOS\\SystemProgram  : Applicativi e moduli nativi (CMD, CALC, NOTEPAD, EXPLORER, REGEDIT)
   - C:\\NewOS\\Registry       : Database cifrato del registro di sistema (REGISTRY.DAT)
   - C:\\NewOS\\Rules          : Regole e policy ufficiali di architettura e sistema
   - C:\\NewOS\\OOBE           : Setup e benvenuto primo avvio
   - C:\\NewOS\\Dev            : Compilatore C nativo (TCC 64-bit), include SDK e temp
   - C:\\User                 : Profili utente e desktop personali
   - C:\\ProgramFile          : Applicazioni e software installati manualmente dall'utente finale
`;
    fs.writeFileSync(path.join(helpDir, 'MANUALE.TXT'), manualeContent);
    fs.writeFileSync(path.join(helpDir, 'COMANDI.TXT'), "Comandi Shell: help, dir, cd, type, cls, ver, echo, reboot, shutdown, regedit, tcc\n");
    fs.writeFileSync(path.join(helpDir, 'FAQ.TXT'), "FAQ NewOS V1: Risoluzione problemi, configurazione rete e periferiche.\n");
    fs.writeFileSync(path.join(helpDir, 'GUIDA.HLP'), "NewOS V1 Help Catalog v1.0.0\n");

    // 5. Popola Diagnostic (C:\NewOS\Diagnostic\)
    const diagDir = path.join(binDir, 'NewOS', 'Diagnostic');
    const peStub = createPeStub('DIAG');
    fs.writeFileSync(path.join(diagDir, 'MEMTEST.EXE'), peStub);
    fs.writeFileSync(path.join(diagDir, 'NETDIAG.EXE'), peStub);
    fs.writeFileSync(path.join(diagDir, 'SYSINFO.TXT'), "NewOS V1 System Telemetry: CPU x86_64, VBE 1024x768x32bpp, RAM 256MB+, exFAT C:\\.\n");
    fs.writeFileSync(path.join(diagDir, 'DIAG.LOG'), "[DIAGNOSTIC] Verifica hardware e integrita' kernel completata con successo.\n");

    // 6. Popola Themes (C:\NewOS\Themes\)
    const themeDir = path.join(binDir, 'NewOS', 'Themes');
    const themesIni =
`[Themes]
Current=Classic
Available=Classic,LiquidGlass,DarkNavy

[Classic]
Name=Windows 2000 Classic
DesktopColor=0x008080
ActiveTitle=0x0A246A
BtnFace=0xD4D0C8
Window=0xFFFFFF
WindowText=0x000000

[LiquidGlass]
Name=Liquid Glass Modern
DesktopColor=0x0B192C
ActiveTitle=0x1E3E62
BtnFace=0x000000
Window=0x1E222B
WindowText=0xFFFFFF

[DarkNavy]
Name=Dark Navy Blue
DesktopColor=0x050C1A
ActiveTitle=0x18345C
BtnFace=0x1A2238
Window=0x0F172A
WindowText=0xE2E8F0
`;
    fs.writeFileSync(path.join(themeDir, 'THEMES.INI'), themesIni);
    fs.writeFileSync(path.join(themeDir, 'CLASSIC.THM'), "Theme=Classic\nStyle=Win2000\n");
    fs.writeFileSync(path.join(themeDir, 'LIQUID_GLASS.THM'), "Theme=LiquidGlass\nStyle=GlassModern\n");
    fs.writeFileSync(path.join(themeDir, 'DARK_NAVY.THM'), "Theme=DarkNavy\nStyle=DeepNavy\n");

    // 7. Popola Language (C:\NewOS\Language\)
    const langDir = path.join(binDir, 'NewOS', 'Language');
    const langIni = "[Language]\nActive=IT_IT\nDefault=IT_IT\nAvailable=IT_IT,EN_US\n\n[Locales]\nIT_IT=Italiano (Italia)\nEN_US=English (United States)\n";
    fs.writeFileSync(path.join(langDir, 'LANGUAGE.INI'), langIni);
    fs.writeFileSync(path.join(langDir, 'IT_IT.LNG'), "[Strings]\nAppName=NewOS V1 Edizione 64-bit\nLoginTitle=Accesso a NewOS V1\nUsername=Nome utente:\nPassword=Password:\nLoginBtn=Accedi\nGuestBtn=Modalita' Ospite\nShutdownBtn=Spegni PC\n");
    fs.writeFileSync(path.join(langDir, 'EN_US.LNG'), "[Strings]\nAppName=NewOS V1 64-bit Edition\nLoginTitle=Logon to NewOS V1\nUsername=Username:\nPassword=Password:\nLoginBtn=Log On\nGuestBtn=Guest Mode\nShutdownBtn=Shut Down\n");

    // 8. Popola SystemProgram (C:\NewOS\SystemProgram\)
    const sysProgDir = path.join(binDir, 'NewOS', 'SystemProgram');
    for (const prog of ['REGEDIT.EXE', 'TASKMGR.EXE', 'EXPLORER.EXE', 'CMD.EXE', 'CALC.EXE', 'NOTEPAD.EXE', 'RULES.EXE', 'WEBBROWSER.EXE', 'DISKMGMT.EXE', 'CHKDSK.EXE', 'EVENTVWR.EXE',
                       'DISPLAY.CPL', 'POWER.CPL', 'NETWORK.CPL', 'DEVMGMT.CPL', 'SOUND.CPL', 'DATEANDTIME.CPL', 'PROGRAMS.CPL', 'ACCESSIBILITY.CPL', 'REGEDIT.CPL']) {
        fs.writeFileSync(path.join(sysProgDir, prog), peStub);
    }
    // Copia i moduli protetti compilati
    for (const mod of ['BOOTSCR.MOD', 'BOOTSCR.INI', 'LOGIN.MOD', 'LOGIN.INI', 'LOGOUT.MOD', 'LOGOUT.INI', 'BOOTSETUP.MOD', 'BOOTSETUP.INI']) {
        const srcMod = path.join(binDir, mod);
        if (fs.existsSync(srcMod)) {
            fs.copyFileSync(srcMod, path.join(sysProgDir, mod));
        }
    }
    const bootSetupDir = path.join(binDir, 'BootSetup');
    for (const mod of ['BOOTSETUP.MOD', 'BOOTSETUP.INI']) {
        const srcMod = path.join(binDir, mod);
        if (fs.existsSync(srcMod)) {
            fs.copyFileSync(srcMod, path.join(bootSetupDir, mod));
        }
    }

    // 9. Popola OOBE (C:\NewOS\OOBE\)
    const oobeDir = path.join(binDir, 'NewOS', 'OOBE');
    const oobeIniInit = "[OOBE]\nCompleted=0\nSetupStage=Welcome\nLanguage=IT_IT\nDefaultUser=Morgan\nAllowSkip=1\n";
    fs.writeFileSync(path.join(oobeDir, 'OOBE.INI'), oobeIniInit.padEnd(256, ' ') + "\n");
    fs.writeFileSync(path.join(oobeDir, 'SETUP.EXE'), peStub);
    fs.writeFileSync(path.join(oobeDir, 'WELCOME.TXT'), "Esperienza iniziale di benvenuto NewOS V1 (OOBE)\n");

    // 9b. Database del Registro di Sistema (C:\NewOS\Registry\REGISTRY.DAT e REGISTRY.LOG)
    const NEWOS_REGISTRY_AES_KEY = Buffer.from("NewOS-V1SETUP-256BIT-AUTHENTIC26", "ascii");

    function encryptRegistryEnvelope(plainIniText) {
        const iv = crypto.randomBytes(16);
        const cipher = crypto.createCipheriv('aes-256-cbc', NEWOS_REGISTRY_AES_KEY, iv);
        let enc = cipher.update(plainIniText, 'utf8');
        enc = Buffer.concat([enc, cipher.final()]);

        const banner = 
`[NEWOS-V1-ENCRYPTED-REGISTRY]
AVVISO=Accesso in chiaro vincolato all'applicazione di sistema Registry (REGEDIT.EXE).
Algoritmo=AES-256-CBC-PKCS7
Status=Encrypted
KeyHash=0x3951E7A2
--- INIZIO BLOCCO CRITTOGRAFATO (AES-256) ---
IV=${iv.toString('hex').toUpperCase()}
DATA=${enc.toString('hex').toUpperCase()}
--- FINE BLOCCO CRITTOGRAFATO ---
`;
        return banner;
    }

    const registryDatContent = `; NewOS V1 Registry Database (C:\\NewOS\\Registry\\REGISTRY.DAT)
; (C) Newspoint Software - NewSoft - NewOS V1

[HKEY_LOCAL_MACHINE\\SYSTEM]
(Predefinito)=REG_SZ:(valore non impostato)
ProductName=REG_SZ:NewOS V1 64-bit
Version=REG_SZ:1.0.0
BuildNumber=REG_DWORD:2026
CurrentResolution=REG_SZ:1024x768
Bpp=REG_DWORD:32
CpuVendor=REG_SZ:AuthenticAMD
MemTotalMB=REG_DWORD:128
DebugSerial=REG_DWORD:1

[HKEY_LOCAL_MACHINE\\DRIVERS]
(Predefinito)=REG_SZ:(valore non impostato)
VGA=REG_SZ:C:\\NewOS\\Driver\\VGA.SYS
AC97=REG_SZ:C:\\NewOS\\Driver\\AC97.SYS
E1000=REG_SZ:C:\\NewOS\\Driver\\E1000.SYS
PS2MOUSE=REG_SZ:C:\\NewOS\\Driver\\PS2MOUSE.SYS
PS2KBD=REG_SZ:C:\\NewOS\\Driver\\PS2KBD.SYS
RTC=REG_SZ:C:\\NewOS\\Driver\\RTC.SYS
POWER=REG_SZ:C:\\NewOS\\Driver\\POWER.SYS

[HKEY_LOCAL_MACHINE\\NETWORK]
(Predefinito)=REG_SZ:(valore non impostato)
Adapter=REG_SZ:Intel PRO/1000 MT Desktop (82540EM)
DHCPEnabled=REG_DWORD:1
IPAddress=REG_SZ:10.0.2.15
SubnetMask=REG_SZ:255.255.255.0
DefaultGateway=REG_SZ:10.0.2.2
NameServer=REG_SZ:10.0.2.3
PingTarget=REG_SZ:95.141.32.1

[HKEY_LOCAL_MACHINE\\NEWOS\\SYSTEM]
(Predefinito)=REG_SZ:(valore non impostato)
SystemRoot=REG_SZ:C:\\NewOS
SystemProgramDir=REG_SZ:C:\\NewOS\\SystemProgram
RegistryDir=REG_SZ:C:\\NewOS\\Registry
DevDir=REG_SZ:C:\\NewOS\\Dev
RulesDir=REG_SZ:C:\\NewOS\\Rules
DriverDir=REG_SZ:C:\\NewOS\\Driver

[HKEY_LOCAL_MACHINE\\NEWOS\\POLICY]
(Predefinito)=REG_SZ:(valore non impostato)
AppInstallPolicy=REG_SZ:UserAppsMustInstallToProgramFile
SystemAppPolicy=REG_SZ:SystemToolsMustResideInSystemProgram
RegistryEncryption=REG_SZ:AES-256-CBC-Encrypted
AccessConstraint=REG_SZ:ClearTextRestrictedToRegedit
LogonPrivilege=REG_SZ:ReadOnlyPreDesktop
RulesAccessPolicy=REG_SZ:DesktopOnlyOrWinApp

[HKEY_LOCAL_MACHINE\\NEWOS\\INSTALLER]
(Predefinito)=REG_SZ:(valore non impostato)
ProgramFilesDir=REG_SZ:C:\\ProgramFile
DefaultInstallDir=REG_SZ:C:\\ProgramFile
SystemProgramDir=REG_SZ:C:\\NewOS\\SystemProgram
RulesPath=REG_SZ:C:\\NewOS\\SystemProgram\\RULES.EXE
DevInclude=REG_SZ:C:\\NewOS\\Dev\\TCC\\INCLUDE\\
TccPath=REG_SZ:C:\\NewOS\\Dev\\TCC\\TCC.EXE
CmdPath=REG_SZ:C:\\NewOS\\SystemProgram\\CMD.EXE
NotepadPath=REG_SZ:C:\\NewOS\\SystemProgram\\NOTEPAD.EXE
CalcPath=REG_SZ:C:\\NewOS\\SystemProgram\\CALC.EXE
BrowserPath=REG_SZ:C:\\NewOS\\SystemProgram\\WEBBROWSER.EXE

[HKEY_CURRENT_USER\\DESKTOP]
(Predefinito)=REG_SZ:(valore non impostato)
Wallpaper=REG_SZ:C:\\NewOS\\Resources\\Wallpapers\\BLISS.BMP
Theme=REG_DWORD:0
IconSpacing=REG_DWORD:72
TaskbarAutoHide=REG_DWORD:0

[HKEY_CURRENT_USER\\NEWOS\\PREFERENCES]
(Predefinito)=REG_SZ:(valore non impostato)
Language=REG_SZ:IT_IT
Sounds=REG_DWORD:1
Resolution=REG_SZ:1024x768
HomePage=REG_SZ:about:home

[HKEY_CLASSES_ROOT]
(Predefinito)=REG_SZ:(valore non impostato)
.TXT=REG_SZ:txtfile (NOTEPAD.EXE)
.BMP=REG_SZ:bmpfile (VIEWER.EXE)
.JPG=REG_SZ:jpegfile (VIEWER.EXE)
.WAV=REG_SZ:wavfile (SNDPLAYER.EXE)
.EXE=REG_SZ:exefile (Applicazione)
.NOR=REG_SZ:norfile (RULES.EXE)
.INI=REG_SZ:inifile (Configurazione)
.SYS=REG_SZ:sysfile (Driver di Sistema)
.DAT=REG_SZ:datfile (Database Registro)
.HTM=REG_SZ:htmlfile (WEBBROWSER.EXE)
.HTML=REG_SZ:htmlfile (WEBBROWSER.EXE)
.URL=REG_SZ:inetsave (WEBBROWSER.EXE)
`;

    const encryptedRegistryDat = encryptRegistryEnvelope(registryDatContent);
    const registryDir = path.join(binDir, 'NewOS', 'Registry');
    if (!fs.existsSync(registryDir)) fs.mkdirSync(registryDir, { recursive: true });

    fs.writeFileSync(path.join(registryDir, 'REGISTRY.DAT'), encryptedRegistryDat);
    fs.writeFileSync(path.join(registryDir, 'REGISTRY.LOG'), 
`[2026-10-18 12:00:00] [AUDIT] Inizializzazione registro NewOS V1 (C:\\NewOS\\Registry\\REGISTRY.DAT) completata.
[2026-10-18 12:00:00] [SICUREZZA] Crittografia AES-256 CBC attiva. Accesso in chiaro vincolato all'app REGEDIT.EXE.
[2026-10-18 12:00:00] [SICUREZZA] Permessi Logon pre-desktop: SOLA LETTURA. Modifiche consentite solo in ambiente Desktop.
`);

    // Pulizia file legacy posizionati nella radice di NewOS/
    const oldRegDat = path.join(binDir, 'NewOS', 'REGISTRY.DAT');
    const oldRegLog = path.join(binDir, 'NewOS', 'REGISTRY.LOG');
    if (fs.existsSync(oldRegDat)) { try { fs.unlinkSync(oldRegDat); } catch (e) {} }
    if (fs.existsSync(oldRegLog)) { try { fs.unlinkSync(oldRegLog); } catch (e) {} }

    // 9c. Popola Rules (C:\NewOS\Rules\) - Regole Architetturali e di Sicurezza di NewOS V1
    const rulesDir = path.join(binDir, 'NewOS', 'Rules');
    if (!fs.existsSync(rulesDir)) fs.mkdirSync(rulesDir, { recursive: true });

    const rule01Hierarchy = `================================================================================
REGOLE DI SISTEMA NEWOS V1 - 01: GERARCHIA DEL FILE SYSTEM
================================================================================
1. RADICE C:\\:
   - C:\\NewOS             : Directory principale del sistema operativo.
   - C:\\ProgramFile       : Riservata rigorosamente alle applicazioni terze parti.
   - C:\\User              : Profili utente e cartelle Desktop/Documenti.
   - C:\\BootSetup         : Moduli di installazione e recovery hardware.

2. STRUTTURA C:\\NewOS:
   - C:\\NewOS\\SystemProgram : Tutti i programmi e applet di sistema nativi:
                              CMD.EXE, CALC.EXE, NOTEPAD.EXE, EXPLORER.EXE,
                              REGEDIT.EXE, TASKMGR.EXE, applet .CPL e moduli .MOD.
   - C:\\NewOS\\Registry      : Database cifrato del registro (REGISTRY.DAT).
   - C:\\NewOS\\Rules         : Regole architetturali vincolanti per il sistema.
   - C:\\NewOS\\Dev           : Ambiente di sviluppo nativo TCC (Tiny C Compiler).
   - C:\\NewOS\\Driver        : Driver hardware (.SYS e .INI).
   - C:\\NewOS\\Resources     : Sfondi, suoni, schermate grafiche, icone.
   - C:\\NewOS\\Font          : Tipi di carattere bitmap nativi.
   - C:\\NewOS\\Help          : Manuali, FAQ e documentazione di bordo.
   - C:\\NewOS\\Diagnostic    : Utility diagnostiche RAM, disco e rete.
   - C:\\NewOS\\Themes        : Temi visivi (Classic Win2000, Liquid Glass).
   - C:\\NewOS\\Language      : Tabelle di localizzazione e pacchetti linguistici.
   - C:\\NewOS\\OOBE          : Setup interattivo Out-of-Box Experience.
`;

    const rule02Registry = `================================================================================
REGOLE DI SISTEMA NEWOS V1 - 02: SICUREZZA E ARCHITETTURA DEL REGISTRO
================================================================================
1. SCHEMA INTERNO NATIVO:
   - Il registro e' al 100% nativo di NewOS V1. E' rigorosamente vietato
     l'utilizzo di rami estranei o incompatibili quali 'Microsoft\\Windows'.
   - Struttura gerarchica:
       [HKEY_LOCAL_MACHINE\\NEWOS\\SYSTEM]
       [HKEY_LOCAL_MACHINE\\NEWOS\\POLICY]
       [HKEY_LOCAL_MACHINE\\NEWOS\\INSTALLER]
       [HKEY_CURRENT_USER\\NEWOS\\PREFERENCES]
       [HKEY_CURRENT_USER\\DESKTOP]
       [HKEY_CLASSES_ROOT]

2. CIFRATURA AES-256 A RIPOSO:
   - Il file C:\\NewOS\\Registry\\REGISTRY.DAT risiede su disco protetto da
     cifratura standard Rijndael AES-256 CBC con padding PKCS#7.
   - Se aperto manualmente (ad es. da Blocco Note o tramite comando 'type' da shell),
     il file mostra esclusivamente l'envelope di sicurezza e il testo cifrato in hex:
       [NEWOS-V1-ENCRYPTED-REGISTRY]
       AVVISO=Accesso in chiaro vincolato all'applicazione di sistema Registry.
   - L'accesso in chiaro e' consentito unicamente all'applicazione REGEDIT.EXE
     e al sottosistema kernel autorizzato.

3. PRIVILEGI DI ACCESSO E TRANSIZIONE STATO:
   - Fase di Logon / Pre-Desktop (post inserimento credenziali):
       * Privilegi di SOLA LETTURA rigorosamente attivi (registry_set_read_only(true)).
       * Il sistema interroga il registro per caricare sfondo, tema, lingua e policy.
       * Qualsiasi tentativo di scrittura o modifica viene intercettato, bloccato
         e registrato con audit 'ACCESSO_NEGATO'.
   - Ambiente Desktop:
       * Una volta completato il login e avviato il Window Manager (wm_init),
         vengono sbloccati i privilegi di modifica (registry_set_read_only(false)).
       * Le modifiche sono consentite solo in ambiente desktop tramite REGEDIT.EXE
         o le applet del Pannello di Controllo.
`;

    const rule03Install = `================================================================================
REGOLE DI SISTEMA NEWOS V1 - 03: POLICY INSTALLATORI E SOFTWARE TERZE PARTI
================================================================================
1. DESTINAZIONE SOFTWARE UTENTE:
   - Tutti gli installer e i pacchetti di installazione devono installare
     i software utente in sottocartelle dedicate dentro:
       -> C:\\ProgramFile\\<NomeApplicazione>\\

2. CHIAVE DI REGISTRO PER GLI INSTALLER:
   - Gli installer devono leggere il percorso predefinito da:
       [HKEY_LOCAL_MACHINE\\NEWOS\\INSTALLER]
       ProgramFilesDir = "C:\\ProgramFile"
       DefaultInstallDir = "C:\\ProgramFile"

3. DIVIETO DI MANOMISSIONE:
   - E' vietata l'installazione di programmi utente in C:\\NewOS\\SystemProgram,
     C:\\NewOS\\Dev o nella radice C:\\.
   - Gli strumenti nativi di sistema sono immutabili e risiedono solo in SystemProgram.
`;

    const rule04Privileges = `================================================================================
REGOLE DI SISTEMA NEWOS V1 - 04: PRIVILEGI E MACCHINA A STATI DI SESSIONE
================================================================================
1. STATO PRE-DESKTOP (OOBE / LOGON):
   - Accesso al filesystem vincolato ai moduli firmati .MOD.
   - Registro di sistema accessibile in SOLA LETTURA.
   - Blocco totale di chiamate a registry_set_value_*.
   - Il login convalida le credenziali utente e legge la configurazione sessione.

2. STATO DESKTOP ATTIVO:
   - Window Manager e Desktop operativi.
   - Scrittura del registro abilitata per gli utenti autenticati.
   - Tracciamento di tutte le modifiche critiche nell'Audit Trail.
`;

    const rule05Network = `================================================================================
REGOLE DI SISTEMA NEWOS V1 - 05: RETE, DIAGNOSTICA E PRIORITA' SERVER
================================================================================
1. LINK PRIORITARIO TEST RETE:
   - La verifica della connettivita' internet in OOBE e nel gestore di rete
     adotta quale target prioritario il server di test italiano:
       -> IP Primario: 95.141.32.1
   - Gateway locale o DNS rappresentano percorsi di fallback secondari.

2. DRIVER DI RETE:
   - Intel PRO/1000 MT (E1000.SYS) e Realtek RTL8139 (RTL8139.SYS)
     sono gestiti con configurazioni standard in C:\\NewOS\\Driver\\NET\\.
`;

    // Rimozione eventuali file .TXT legacy in Rules
    for (const txtName of ['01_SYSTEM_HIERARCHY.TXT', '02_REGISTRY_SECURITY.TXT', '03_INSTALL_POLICY.TXT', '04_USER_PRIVILEGES.TXT', '05_NETWORK_AND_DEVICES.TXT']) {
        const p = path.join(rulesDir, txtName);
        if (fs.existsSync(p)) { try { fs.unlinkSync(p); } catch (e) {} }
    }

    // Generazione file proprietari .NOR (NewOS Official Rule)
    fs.writeFileSync(path.join(rulesDir, '01_SYSTEM_HIERARCHY.NOR'),
        createNorFile('01_SYSTEM_HIERARCHY', 'Architettura File System', 'CRITICA', rule01Hierarchy));
    fs.writeFileSync(path.join(rulesDir, '02_REGISTRY_SECURITY.NOR'),
        createNorFile('02_REGISTRY_SECURITY', 'Sicurezza e Registro', 'MASSIMA', rule02Registry));
    fs.writeFileSync(path.join(rulesDir, '03_INSTALL_POLICY.NOR'),
        createNorFile('03_INSTALL_POLICY', 'Policy Installatori', 'ALTA', rule03Install));
    fs.writeFileSync(path.join(rulesDir, '04_USER_PRIVILEGES.NOR'),
        createNorFile('04_USER_PRIVILEGES', 'Privilegi e Sessioni', 'SICUREZZA', rule04Privileges));
    fs.writeFileSync(path.join(rulesDir, '05_NETWORK_AND_DEVICES.NOR'),
        createNorFile('05_NETWORK_AND_DEVICES', 'Rete e Connettivita\'', 'MEDIA', rule05Network));

    const rulesReadme = `================================================================================
CARTELLA DELLE REGOLE UFFICIALI DI SISTEMA (C:\\NewOS\\Rules\\)
================================================================================
I file con estensione .NOR (NewOS Official Rule) sono file protetti e cifrati
proprietari del sistema operativo NewOS V1.

MODALITA' DI ACCESSO:
1. Ambiente NewOS:
   - Accessibili ESCLUSIVAMENTE all'interno dell'ambiente Desktop autenticato
     tramite l'applicazione RULES.EXE ("Regole di Sistema" nel menu Avvio o
     facendo doppio clic sul file .NOR in Risorse del Computer / Explorer).
   - In fase pre-desktop o riga di comando shell il file rifiuta l'apertura grezza.

2. Ambiente Microsoft Windows:
   - Visualizzabili e verificabili tramite l'applicazione dedicata fornita
     nella cartella del pacchetto:
     "Rules Win App\\NewOS_Rules_Viewer.exe" (o script RulesViewer.bat).
================================================================================
`;
    fs.writeFileSync(path.join(rulesDir, 'LEGGIMI.TXT'), rulesReadme);


    // 10. Popola User (C:\User\)
    const userDir = path.join(binDir, 'User');
    const morganDir = path.join(userDir, 'Morgan');
    const morganDesk = path.join(morganDir, 'Desktop');
    const userCfgInit = "USER=Morgan\nPASS=\nHOST=NEWOS-PC\nTHEME=0\nPOWER=0\nINSTALLED=0\n";
    fs.writeFileSync(path.join(morganDir, 'USER.CFG'), userCfgInit.padEnd(256, ' ') + "\n");
    fs.writeFileSync(path.join(morganDesk, 'NOTE.TXT'), "Note e appunti personali in C:\\User\\Morgan\\Desktop\\\n");
    fs.writeFileSync(path.join(morganDesk, 'BENVENUTO.TXT'), "Benvenuto nel tuo Desktop personale di NewOS V1 (C:\\User\\Morgan\\Desktop\\)!\n");
    const blissSrc = path.join(wpDir, 'BLISS.BMP');
    if (fs.existsSync(blissSrc)) {
        fs.copyFileSync(blissSrc, path.join(morganDesk, 'SFONDO_BLISS.BMP'));
    }
    fs.writeFileSync(path.join(morganDesk, 'PROGETTI', 'PROGETTO1.TXT'), "Bozza progetto software in C per NewOS V1 64-bit.\n");
    fs.writeFileSync(path.join(userDir, 'Default', 'USER.CFG'), "USER=Default\nPASS=\nHOST=NEWOS-PC\nTHEME=0\nPOWER=0\nINSTALLED=0\n");

    // 11. Popola NewOS\Dev (C:\NewOS\Dev\) - Compilatore C Nativo TCC
    const devDir = path.join(binDir, 'NewOS', 'Dev');
    if (!fs.existsSync(devDir)) fs.mkdirSync(devDir, { recursive: true });

    const newosHStub = `/**
 * newos.h - NewOS V1 Software Development Kit (SDK)
 * Copyright (C) Newspoint Software - NewSoft
 */
#ifndef _NEWOS_H_
#define _NEWOS_H_

typedef unsigned long long uint64_t;
typedef unsigned int       uint32_t;
typedef unsigned short     uint16_t;
typedef unsigned char      uint8_t;

// API System Call & GUI Entrypoints
void sys_print(const char* str);
void sys_exit(int code);

#endif // _NEWOS_H_
`;

    // Root di C:\NewOS\Dev\
    fs.writeFileSync(path.join(devDir, 'TCC.EXE'), peStub);
    fs.writeFileSync(path.join(devDir, 'LEGGIMI.TXT'), "Ambiente di sviluppo nativo Tiny C Compiler (TCC 64-bit) per NewOS V1 (C:\\NewOS\\Dev\\)\n");

    // Sottocartella C:\NewOS\Dev\TCC\ e sottodirectory strutturate
    const tccDir = path.join(devDir, 'TCC');
    const tccDirs = ['CONFIG', 'INCLUDE', 'TEMP', 'TMP'];
    for (const td of tccDirs) {
        const p1 = path.join(devDir, td);
        const p2 = path.join(tccDir, td);
        if (!fs.existsSync(p1)) fs.mkdirSync(p1, { recursive: true });
        if (!fs.existsSync(p2)) fs.mkdirSync(p2, { recursive: true });
    }

    const tccCfgDev = "[Compiler]\nTarget=x86_64-newos\nOptimization=O2\nWarnings=All\nSysInclude=C:\\NewOS\\Dev\\INCLUDE\\\nSysLib=C:\\NewOS\\Dev\\LIB\\\nTempDir=C:\\NewOS\\Dev\\TEMP\\\nTmpDir=C:\\NewOS\\Dev\\TMP\\\nDefaultOutput=C:\\APPS\\\n";
    fs.writeFileSync(path.join(devDir, 'CONFIG', 'TCC.CFG'), tccCfgDev);
    fs.writeFileSync(path.join(devDir, 'INCLUDE', 'NEWOS.H'), newosHStub);
    fs.writeFileSync(path.join(devDir, 'TEMP', 'TEMP.TXT'), "Cartella per file temporanei e oggetti intermedi di compilazione TCC.\n");
    fs.writeFileSync(path.join(devDir, 'TMP', 'TMP.TXT'), "Directory temporanea TMP per il compilatore TCC.\n");

    const tccCfgSub = "[Compiler]\nTarget=x86_64-newos\nOptimization=O2\nWarnings=All\nSysInclude=C:\\NewOS\\Dev\\TCC\\INCLUDE\\\nSysLib=C:\\NewOS\\Dev\\TCC\\LIB\\\nTempDir=C:\\NewOS\\Dev\\TCC\\TEMP\\\nTmpDir=C:\\NewOS\\Dev\\TCC\\TMP\\\nDefaultOutput=C:\\APPS\\\n";
    fs.writeFileSync(path.join(tccDir, 'TCC.EXE'), peStub);
    fs.writeFileSync(path.join(tccDir, 'CONFIG', 'TCC.CFG'), tccCfgSub);
    fs.writeFileSync(path.join(tccDir, 'INCLUDE', 'NEWOS.H'), newosHStub);
    fs.writeFileSync(path.join(tccDir, 'TEMP', 'TEMP.TXT'), "Cartella per file temporanei e oggetti intermedi di compilazione TCC.\n");
    fs.writeFileSync(path.join(tccDir, 'TMP', 'TMP.TXT'), "Directory temporanea TMP per il compilatore TCC.\n");
    fs.writeFileSync(path.join(tccDir, 'LEGGIMI.TXT'), "Tiny C Compiler 64-bit per NewOS V1 (C:\\NewOS\\Dev\\TCC\\)\n");

    // 12. Popola ProgramFile (C:\ProgramFile\) - Riservata esclusivamente ai software installati dall'utente finale
    const progDir = path.join(binDir, 'ProgramFile');
    if (!fs.existsSync(progDir)) fs.mkdirSync(progDir, { recursive: true });

    // Rimuovi eventuale residuo di TCC da ProgramFile
    const oldTccProg = path.join(progDir, 'TCC');
    if (fs.existsSync(oldTccProg)) {
        try {
            fs.rmSync(oldTccProg, { recursive: true, force: true });
        } catch (e) {}
    }

    // Rimuovi eventuale cartella Accessories da ProgramFile
    const oldAccProg = path.join(progDir, 'Accessories');
    if (fs.existsSync(oldAccProg)) {
        try {
            fs.rmSync(oldAccProg, { recursive: true, force: true });
        } catch (e) {}
    }

    const installPolicy = `================================================================================
NewOS V1 - Linee Guida e Policy di Installazione (C:\\ProgramFile)
================================================================================
Questa cartella e' riservata ESCLUSIVAMENTE alle applicazioni e ai software
installati manualmente dall'utente finale o dai relativi installer di setup.

STRUTTURA E REGOLE DI SISTEMA:
1. Programmi Nativi di Sistema:
   Tutti gli applicativi di sistema (CMD.EXE, CALC.EXE, NOTEPAD.EXE, EXPLORER.EXE,
   REGEDIT.EXE, TASKMGR.EXE e applet .CPL) risiedono rigorosamente in:
     -> C:\\NewOS\\SystemProgram\\

2. Ambiente di Sviluppo:
   Il compilatore C nativo (TCC.EXE), gli header SDK e le directory temporanee
   risiedono nella cartella:
     -> C:\\NewOS\\Dev\\

3. Applicazioni Utente (Policy per gli Installer):
   Tutti gli installer software (.EXE / .PKG / .MSI) devono fare riferimento
   alla chiave di configurazione e registro di sistema:
     [HKEY_LOCAL_MACHINE\\NEWOS\\INSTALLER]
     ProgramFilesDir = "C:\\ProgramFile"
     DefaultInstallDir = "C:\\ProgramFile"
   I file dell'applicazione devono essere installati in una sottocartella dedicata:
     -> C:\\ProgramFile\\<NomeApplicazione>\\
`;
    fs.writeFileSync(path.join(progDir, 'LEGGIMI.TXT'), installPolicy);
    fs.writeFileSync(path.join(progDir, 'INSTALL_POLICY.TXT'), installPolicy);

    console.log('[OK] Struttura delle cartelle NewOS V1 in build/bin/ popolata con successo.');
}

module.exports = { organizeBuildBin, createDriverSys, createPeStub, DRIVERS_SPEC };
