const fs = require('fs');
const path = require('path');
const { execSync } = require('child_process');

const ROOT = path.resolve(__dirname, '..');
const BUILD_DIR = path.join(ROOT, 'build');
const BIN_DIR = path.join(BUILD_DIR, 'bin');
const OBJ_DIR = path.join(BUILD_DIR, 'obj');

if (!fs.existsSync(BUILD_DIR)) fs.mkdirSync(BUILD_DIR, { recursive: true });
if (!fs.existsSync(BIN_DIR)) fs.mkdirSync(BIN_DIR, { recursive: true });
if (!fs.existsSync(OBJ_DIR)) fs.mkdirSync(OBJ_DIR, { recursive: true });

console.log('=== NewOS V1 64-bit Build System ===\n');

// 1. Trova NASM
function findNasm() {
    const candidates = [
        path.join(process.env.LOCALAPPDATA || '', 'bin', 'NASM', 'nasm.exe'),
        'C:\\Program Files\\NASM\\nasm.exe',
        path.join(process.env.LOCALAPPDATA || '', 'Programs', 'NASM', 'nasm.exe')
    ];
    for (const c of candidates) {
        if (fs.existsSync(c)) return `"${c}"`;
    }
    try {
        execSync('nasm -v', { stdio: 'ignore' });
        return 'nasm';
    } catch (e) {
        return null;
    }
}

// 2. Trova LLVM / Clang / LLD
function findClangTools() {
    const candidates = [
        path.join(ROOT, 'tools', 'llvm-mingw', 'llvm-mingw-20260616-ucrt-x86_64', 'bin')
    ];

    const wingetPackages = path.join(process.env.LOCALAPPDATA || '', 'Microsoft', 'WinGet', 'Packages');

    if (fs.existsSync(wingetPackages)) {
        const pkgs = fs.readdirSync(wingetPackages);
        for (const p of pkgs) {
            if (p.includes('LLVM-MinGW') || p.includes('LLVM')) {
                const sub = path.join(wingetPackages, p);
                const subEntries = fs.readdirSync(sub);
                for (const se of subEntries) {
                    const binPath = path.join(sub, se, 'bin');
                    if (fs.existsSync(path.join(binPath, 'clang.exe'))) {
                        candidates.push(binPath);
                    }
                }
                if (fs.existsSync(path.join(sub, 'bin', 'clang.exe'))) {
                    candidates.push(path.join(sub, 'bin'));
                }
            }
        }
    }

    candidates.push('C:\\Program Files\\LLVM\\bin');
    const linksDir = path.join(process.env.LOCALAPPDATA || '', 'Microsoft', 'WinGet', 'Links');
    if (fs.existsSync(path.join(linksDir, 'clang.exe'))) {
        candidates.push(linksDir);
    }

    for (const c of candidates) {
        const clang = path.join(c, 'clang.exe');
        if (fs.existsSync(clang)) {
            return {
                dir: c,
                clang: `"${clang}"`,
                lld: fs.existsSync(path.join(c, 'ld.lld.exe')) ? `"${path.join(c, 'ld.lld.exe')}"` : `"${path.join(c, 'lld.exe')}"`,
                objcopy: fs.existsSync(path.join(c, 'llvm-objcopy.exe')) ? `"${path.join(c, 'llvm-objcopy.exe')}"` : 'llvm-objcopy'
            };
        }
    }

    try {
        execSync('clang -v', { stdio: 'ignore' });
        return { dir: '', clang: 'clang', lld: 'ld.lld', objcopy: 'llvm-objcopy' };
    } catch (e) {
        return null;
    }
}

const nasm = findNasm();
const llvm = findClangTools();

if (!nasm) {
    console.error('[ERRORE] NASM non trovato. Esegui setup_tools.ps1.');
    process.exit(1);
}
console.log(`[OK] NASM: ${nasm}`);

if (!llvm) {
    console.error('[ERRORE] Clang/LLVM non trovato. Attendi il completamento del setup o esegui setup_tools.ps1.');
    process.exit(1);
}
console.log(`[OK] LLVM Tools trovati in: ${llvm.dir || 'PATH'}`);

// 3. Assembla Bootloader
console.log('\n--> Assemblaggio Bootloader (MBR + Stage 2)...');
const bootsectSrc = path.join(ROOT, 'boot', 'bootsect.asm');
const bootsectBin = path.join(BIN_DIR, 'bootsect.bin');
execSync(`${nasm} -f bin "${bootsectSrc}" -o "${bootsectBin}"`, { stdio: 'inherit' });

const bootsectStat = fs.statSync(bootsectBin);
if (bootsectStat.size !== 512) {
    console.error(`[ERRORE] MBR bootsector dimensione non valida: ${bootsectStat.size} byte (richiesti 512)`);
    process.exit(1);
}

const stage2Src = path.join(ROOT, 'boot', 'stage2.asm');
const stage2Bin = path.join(BIN_DIR, 'stage2.bin');
execSync(`${nasm} -f bin "${stage2Src}" -o "${stage2Bin}"`, { stdio: 'inherit' });

// Padding di stage2 a 64 settori (32768 byte)
const STAGE2_SIZE = 64 * 512;
const stage2Data = fs.readFileSync(stage2Bin);
if (stage2Data.length > STAGE2_SIZE) {
    console.error(`[ERRORE] Stage 2 troppo grande: ${stage2Data.length} byte (massimo ${STAGE2_SIZE})`);
    process.exit(1);
}
const stage2Padded = Buffer.alloc(STAGE2_SIZE, 0);
stage2Data.copy(stage2Padded);
fs.writeFileSync(stage2Bin, stage2Padded);
console.log(`[OK] Bootloader pronto (MBR: 512B, Stage2: ${STAGE2_SIZE}B)`);

// 4. Assembla stubs 64-bit del kernel
console.log('\n--> Assemblaggio stubs assembly a 64-bit...');
const asmFiles = [
    { src: 'kernel/arch/x86_64/entry.asm', obj: 'entry.o' },
    { src: 'kernel/arch/x86_64/isr_stubs.asm', obj: 'isr_stubs.o' },
    { src: 'kernel/gui/icons_data.asm', obj: 'icons_data.o' }
];

for (const a of asmFiles) {
    const srcPath = path.join(ROOT, a.src);
    const objPath = path.join(OBJ_DIR, a.obj);
    execSync(`${nasm} -f elf64 -i "${ROOT}/" "${srcPath}" -o "${objPath}"`, { stdio: 'inherit' });
    console.log(`  ASM: ${a.src} -> obj/${a.obj}`);
}

// 5. Compila moduli C per target x86_64 freestanding
console.log('\n--> Compilazione moduli C (target: x86_64-unknown-none-elf)...');
const cFiles = [
    'kernel/lib/string.c',
    'kernel/lib/aes256.c',
    'kernel/arch/x86_64/idt.c',
    'kernel/arch/x86_64/pic.c',
    'kernel/arch/x86_64/cpu.c',
    'kernel/drivers/pci.c',
    'kernel/drivers/speaker.c',
    'kernel/drivers/ac97.c',
    'kernel/drivers/sound_data.c',
    'kernel/drivers/sound.c',
    'kernel/drivers/ata.c',
    'kernel/drivers/timer.c',
    'kernel/drivers/rtc.c',
    'kernel/drivers/keyboard.c',
    'kernel/drivers/mouse.c',
    'kernel/mm/heap.c',
    'kernel/gui/font.c',
    'kernel/gui/graphics.c',
    'kernel/gui/icons.c',
    'kernel/gui/wm.c',
    'kernel/apps/cmd.c',
    'kernel/apps/notepad.c',
    'kernel/apps/calculator.c',
    'kernel/apps/about.c',
    'kernel/apps/display.c',
    'kernel/apps/sound_props.c',
    'kernel/apps/explorer.c',
    'kernel/apps/autoplay.c',
    'kernel/drivers/power.c',
    'kernel/apps/power_props.c',
    'kernel/apps/boot_menu.c',
    'kernel/apps/installer.c',
    'kernel/lib/user.c',
    'kernel/fs/exfat.c',
    'kernel/fs/exfat_ops.c',
    'kernel/fs/vfs_mount.c',
    'kernel/fs/vfs_cache.c',
    'kernel/fs/iso9660.c',
    'kernel/fs/udf.c',
    'kernel/fs/ntfs.c',
    'kernel/fs/fs_probe.c',
    'kernel/fs/ramfs.c',
    'kernel/fs/vfs.c',
    'kernel/gui/image.c',
    'kernel/drivers/sound_wav.c',
    'kernel/drivers/driver_mgr.c',
    'kernel/apps/devmgmt.c',
    'kernel/drivers/vga.c',
    'kernel/drivers/usb/usb.c',
    'kernel/drivers/usb/uhci.c',
    'kernel/drivers/usb/ohci.c',
    'kernel/drivers/usb/xhci.c',
    'kernel/drivers/usb/usb_hid.c',
    'kernel/drivers/usb/usb_msc.c',
    'kernel/drivers/usb/usb_hub.c',
    'kernel/drivers/usb/usb_net.c',
    'kernel/drivers/net/net_nic.c',
    'kernel/drivers/net/e1000.c',
    'kernel/drivers/net/rtl8139.c',
    'kernel/drivers/net/wifi.c',
    'kernel/net/eth.c',
    'kernel/net/arp.c',
    'kernel/net/ipv4.c',
    'kernel/net/icmp.c',
    'kernel/net/udp.c',
    'kernel/net/tcp.c',
    'kernel/net/dhcp.c',
    'kernel/net/tls.c',
    'kernel/net/socket.c',
    'kernel/fs/registry.c',
    'kernel/apps/regedit.c',
    'kernel/apps/oobe.c',
    'kernel/apps/rules_viewer.c',
    'WebBrowser/webbrowser.c',
    'kernel/gui/screen_mgr.c',
    'kernel/kernel.c'
];

const cObjFiles = [];
const cFlags = [
    '-target x86_64-unknown-none-elf',
    '-ffreestanding',
    '-mgeneral-regs-only',
    '-fno-builtin',
    '-nostdlib',
    '-mno-red-zone',
    `-I"${path.join(ROOT, 'kernel')}"`,
    `-I"${path.join(ROOT, 'kernel', 'include')}"`,
    `-I"${path.join(ROOT, 'BootSetup')}"`,
    `-I"${path.join(ROOT, 'WebBrowser')}"`,
    '-O2',
    '-Wall',
    '-Wno-unused-function'
].join(' ');

for (const c of cFiles) {
    const baseName = path.basename(c, '.c');
    const objName = path.join(OBJ_DIR, `${baseName}.o`);
    cObjFiles.push(objName);
    const cmd = `${llvm.clang} ${cFlags} -c "${path.join(ROOT, c)}" -o "${objName}"`;
    execSync(cmd, { stdio: 'inherit' });
    console.log(`  CC: ${c} -> ${baseName}.o`);
}

// 6. Link del Kernel 64-bit
console.log('\n--> Linking del Kernel con LLD...');
const allObjs = asmFiles.map(a => `"${path.join(OBJ_DIR, a.obj)}"`).concat(cObjFiles.map(o => `"${o}"`)).join(' ');
const linkerScript = path.join(ROOT, 'kernel', 'arch', 'x86_64', 'linker.ld');
const kernelElf = path.join(BIN_DIR, 'kernel.elf');
const kernelBin = path.join(BIN_DIR, 'kernel.bin');

// Link a ELF64 prima
const linkCmd = `${llvm.lld} -m elf_x86_64 -T "${linkerScript}" -o "${kernelElf}" ${allObjs}`;
execSync(linkCmd, { stdio: 'inherit' });

// Estrazione in binario piatto grezzo tramite llvm-objcopy
const objcopyCmd = `${llvm.objcopy} -O binary "${kernelElf}" "${kernelBin}"`;
execSync(objcopyCmd, { stdio: 'inherit' });

const kernelStat = fs.statSync(kernelBin);
console.log(`[OK] Kernel 64-bit generato con successo: ${kernelStat.size} byte`);

// 6b. Compilazione e Firma Moduli Esterni Protetti (.MOD)
console.log('\n--> Compilazione e Firma Moduli Esterni Protetti (C:\\SYSTEM\\SCREENS\\)...');

function crc32Buffer(buf) {
    let crc = 0xFFFFFFFF;
    for (let i = 0; i < buf.length; i++) {
        crc ^= buf[i];
        for (let j = 0; j < 8; j++) {
            crc = (crc >>> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
        }
    }
    return (crc ^ 0xFFFFFFFF) >>> 0;
}

const screenModules = [
    { src: 'bootscreen/bootscreen.c', ini: 'bootscreen/bootscreen.ini', out: 'BOOTSCR.MOD', iniOut: 'BOOTSCR.INI' },
    { src: 'loginscreen/loginscreen.c', ini: 'loginscreen/loginscreen.ini', out: 'LOGIN.MOD', iniOut: 'LOGIN.INI' },
    { src: 'logoutscreen/logoutscreen.c', ini: 'logoutscreen/logoutscreen.ini', out: 'LOGOUT.MOD', iniOut: 'LOGOUT.INI' },
    { src: 'BootSetup/bootsetup.c', extraSrcs: ['BootSetup/aes256.c'], ini: 'BootSetup/bootsetup.ini', out: 'BOOTSETUP.MOD', iniOut: 'BOOTSETUP.INI' }
];

const modLdScript = path.join(ROOT, 'tools', 'module.ld');

for (const sm of screenModules) {
    const allSrcs = [sm.src, ...(sm.extraSrcs || [])];
    const allObjs = [];
    for (const s of allSrcs) {
        const srcPath = path.join(ROOT, s);
        const objPath = path.join(OBJ_DIR, `${path.basename(s, '.c')}.o`);
        allObjs.push(`"${objPath}"`);
        const compileCmd = `${llvm.clang} ${cFlags} -fPIC -c "${srcPath}" -o "${objPath}"`;
        execSync(compileCmd, { stdio: 'inherit' });
    }

    const binPath = path.join(BIN_DIR, sm.out);

    // Linka modulo in binario protetto piatto con module.ld
    const linkModCmd = `${llvm.lld} -m elf_x86_64 -T "${modLdScript}" --oformat binary ${allObjs.join(' ')} -o "${binPath}"`;
    execSync(linkModCmd, { stdio: 'inherit' });

    // Firma crittografica e checksum CRC32 del payload
    const modBuf = fs.readFileSync(binPath);
    if (modBuf.length >= 64) {
        const payload = modBuf.subarray(64);
        const checksum = crc32Buffer(payload);
        modBuf.writeUInt32LE(payload.length, 16); // code_size (offset 16)
        modBuf.writeUInt32LE(checksum, 24);      // checksum CRC32 (offset 24)
        fs.writeFileSync(binPath, modBuf);
        console.log(`  [OK] Modulo protetto generato e firmato: ${sm.out} (${modBuf.length} byte, CRC32: 0x${checksum.toString(16).toUpperCase()})`);
    }

    // Copia file .INI associato
    const iniSrc = path.join(ROOT, sm.ini);
    if (fs.existsSync(iniSrc)) {
        fs.copyFileSync(iniSrc, path.join(BIN_DIR, sm.iniOut));
    }
}

// 6c. Organizza e Popola l'albero completo NewOS V1 dentro build/bin/
const { organizeBuildBin } = require('./tree_manager');
organizeBuildBin(ROOT, BIN_DIR);

// 6d. Generazione archivio cifrato AES-256 SYSTEM.PAK per BootSetup (Regola 1)
const { packSystem } = require('./pack_system');
const pakPath = path.join(BIN_DIR, 'BootSetup', 'SYSTEM.PAK');
packSystem(BIN_DIR, pakPath);

// 7. Assembla immagine disco myos.img
console.log('\n--> Creazione disco di boot myos.img...');
const TOTAL_IMG_SECTORS = 262144; // 128 MB
const imgBuf = Buffer.alloc(TOTAL_IMG_SECTORS * 512, 0);

// Settore 0: MBR
const mbrBuf = fs.readFileSync(bootsectBin);
mbrBuf.copy(imgBuf, 0);

// Settori 1..64: Stage 2
const s2Buf = fs.readFileSync(stage2Bin);
s2Buf.copy(imgBuf, 512);

// Settori 66..N: Kernel (LBA 66 = offset 66 * 512 = 33792)
const kBuf = fs.readFileSync(kernelBin);
kBuf.copy(imgBuf, 66 * 512);

// Iniezione repository icone native (LBA 1024 per fallback)
const iconsPath = path.join(ROOT, 'resources', 'icons.bin');
if (fs.existsSync(iconsPath)) {
    const iconsBuf = fs.readFileSync(iconsPath);
    iconsBuf.copy(imgBuf, 1024 * 512);
    console.log(`[OK] Repository Icone (fallback) iniettato a LBA 1024 (${iconsBuf.length} byte)`);
}

// 8. Generazione e Iniezione Volume File System exFAT (LBA 2048 in poi)
console.log('\n--> Formattazione e popolamento Volume File System exFAT (C:\\)...');
const { buildMyOsFileSystem } = require('./mkexfat.js');
const exfatVolumeSectors = TOTAL_IMG_SECTORS - 2048; // 63488 settori (31 MB)
const exfatBuf = buildMyOsFileSystem(ROOT, exfatVolumeSectors, 2048);
exfatBuf.copy(imgBuf, 2048 * 512);
console.log(`[OK] Partizione exFAT iniettata a LBA 2048 (${exfatBuf.length} byte, C:\\ pronta)`);

const buildImg = path.join(BUILD_DIR, 'NewOS_V1.img');
fs.writeFileSync(buildImg, imgBuf);
fs.writeFileSync(path.join(BUILD_DIR, 'myos.img'), imgBuf);
fs.writeFileSync(path.join(ROOT, 'NewOS_V1.img'), imgBuf);
fs.writeFileSync(path.join(ROOT, 'myos.img'), imgBuf);
console.log(`[OK] Immagine disco: ${buildImg} (${TOTAL_IMG_SECTORS * 512 / 1024 / 1024} MB, Kernel a LBA 66, exFAT a LBA 2048)`);

// 9. Aggiornamento opzionale immagini VirtualBox VDI
const vboxManage = 'C:\\Program Files\\Oracle\\VirtualBox\\VBoxManage.exe';
if (fs.existsSync(vboxManage)) {
    try {
        const buildVdi = path.join(BUILD_DIR, 'NewOS_V1.vdi');
        const rootVdi = path.join(ROOT, 'NewOS_V1.vdi');
        try { execSync(`"${vboxManage}" closemedium disk "${buildVdi}" --delete`, { stdio: 'ignore' }); } catch(e) {}
        try { execSync(`"${vboxManage}" closemedium disk "${rootVdi}" --delete`, { stdio: 'ignore' }); } catch(e) {}
        if (fs.existsSync(buildVdi)) fs.unlinkSync(buildVdi);
        execSync(`"${vboxManage}" convertfromraw "${buildImg}" "${buildVdi}" --format VDI`, { stdio: 'ignore' });
        fs.copyFileSync(buildVdi, rootVdi);
        fs.copyFileSync(buildVdi, path.join(BUILD_DIR, 'myos.vdi'));
        fs.copyFileSync(buildVdi, path.join(ROOT, 'myos.vdi'));
        console.log(`[OK] Immagine VirtualBox: ${rootVdi} sincronizzata`);
    } catch (e) {}
}

// 10. Generazione Immagine ISO Avviabile (El Torito CD-ROM + Hybrid MBR)
console.log('\n--> Creazione immagine ISO avviabile (NewOS_V1.iso)...');
const { createIsoFromDiskImage } = require('./mkiso.js');
const buildIso = path.join(BUILD_DIR, 'NewOS_V1.iso');
const rootIso = path.join(ROOT, 'NewOS_V1.iso');

if (fs.existsSync(vboxManage)) {
    try { execSync(`"${vboxManage}" closemedium dvd "${buildIso}"`, { stdio: 'ignore' }); } catch(e) {}
    try { execSync(`"${vboxManage}" closemedium dvd "${rootIso}"`, { stdio: 'ignore' }); } catch(e) {}
}

createIsoFromDiskImage(buildImg, buildIso, BIN_DIR);

function safeCopy(src, dst) {
    try {
        if (fs.existsSync(dst)) fs.unlinkSync(dst);
    } catch(e) {}
    fs.copyFileSync(src, dst);
}

safeCopy(buildIso, rootIso);
safeCopy(buildIso, path.join(BUILD_DIR, 'newos.iso'));
safeCopy(buildIso, path.join(ROOT, 'newos.iso'));
safeCopy(buildIso, path.join(BUILD_DIR, 'myos.iso'));
safeCopy(buildIso, path.join(ROOT, 'myos.iso'));
console.log(`[OK] Immagine ISO avviabile: ${rootIso} pronta (disponibile anche come newos.iso e myos.iso)`);

// 11. Sincronizzazione cartella sorgenti portatile (sources/)
const { syncSources } = require('./sync_sources.js');
syncSources();

console.log('\n=== BUILD COMPLETATA: NUOVO FILE ISO PRONTO: NewOS_V1.iso ===');
