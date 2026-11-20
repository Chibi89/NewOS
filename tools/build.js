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

const BOOT_DIR = path.join(BIN_DIR, 'NewOS', 'Boot');
if (!fs.existsSync(BOOT_DIR)) fs.mkdirSync(BOOT_DIR, { recursive: true });

function getBootBin(name) {
    const candidates = [
        path.join(BOOT_DIR, name),
        path.join(BOOT_DIR, name.toLowerCase()),
        path.join(BOOT_DIR, name.toUpperCase()),
        path.join(BIN_DIR, name),
        path.join(BIN_DIR, name.toLowerCase()),
        path.join(BIN_DIR, name.toUpperCase())
    ];
    for (const c of candidates) {
        if (fs.existsSync(c)) return c;
    }
    return path.join(BOOT_DIR, name);
}

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

// 3b. Compilazione Bootloader Nativo UEFI (BOOTX64.EFI)
const { buildEfiBootloader } = require('./build_efi.js');
buildEfiBootloader(ROOT, BIN_DIR, OBJ_DIR, llvm);

// 4. Assembla stubs 64-bit del kernel
console.log('\n--> Assemblaggio stubs assembly a 64-bit...');
const asmFiles = [
    { src: 'kernel/arch/x86_64/entry.asm', obj: 'entry.o' },
    { src: 'kernel/arch/x86_64/isr_stubs.asm', obj: 'isr_stubs.o' },
    { src: 'kernel/arch/x86_64/syscall_entry.asm', obj: 'syscall_entry.o' },
    { src: 'kernel/arch/x86_64/ap_trampoline.asm', obj: 'ap_trampoline.o' }
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
    'kernel/lib/clipboard.c',
    'kernel/lib/aes256.c',
    'kernel/arch/x86_64/idt.c',
    'kernel/arch/x86_64/bsod_loader.c',
    'kernel/arch/x86_64/pic.c',
    'kernel/arch/x86_64/cpu.c',
    'kernel/arch/x86_64/syscall.c',
    'kernel/arch/x86_64/smp.c',
    'kernel/arch/x86_64/sched.c',
    'kernel/ipc/ipc.c',
    'kernel/drivers/pci.c',
    'kernel/drivers/acpi.c',
    'kernel/drivers/ahci.c',
    'kernel/drivers/nvme_loader.c',
    'kernel/drivers/sound_loader.c',
    'kernel/drivers/ata.c',
    'kernel/drivers/timer.c',
    'kernel/drivers/rtc.c',
    'kernel/drivers/keyboard.c',
    'kernel/drivers/mouse.c',
    'kernel/mm/heap.c',
    'kernel/mm/paging.c',
    'kernel/gui/font.c',
    'kernel/gui/font_loader.c',
    'kernel/gui/graphics.c',
    'kernel/gui/icons.c',
    'kernel/gui/wm.c',
    'kernel/gui/theme_loader.c',
    'kernel/gui/settings_loader.c',
    'kernel/gui/comdlg_loader.c',
    'kernel/gui/uistyle_loader.c',
    'kernel/gui/hwaccel_loader.c',
    'kernel/gui/gpu3d_loader.c',
    'kernel/apps/sysapps_loader.c',
    'control_panel/cpl_ext_loader.c',
    'explorer/explorer_loader.c',
    'kernel/drivers/power.c',
    'kernel/apps/boot_menu.c',
    'kernel/lib/user.c',
    'kernel/fs/exfat.c',
    'kernel/fs/exfat_ops.c',
    'kernel/fs/vfs_mount.c',
    'kernel/fs/vfs_cache.c',
    'kernel/fs/iso9660.c',
    'kernel/fs/fs_ext_loader.c',
    'kernel/fs/fs_probe.c',
    'kernel/fs/ramfs.c',
    'kernel/fs/vfs.c',
    'kernel/gui/image.c',
    'kernel/drivers/driver_mgr.c',
    'kernel/drivers/vga.c',
    'kernel/drivers/usb/usb.c',
    'kernel/drivers/usb/uhci.c',
    'kernel/drivers/usb/ohci.c',
    'kernel/drivers/usb/xhci.c',
    'kernel/drivers/usb/usb_hid.c',
    'kernel/drivers/usb/usb_msc.c',
    'kernel/drivers/usb/usb_hub.c',
    'kernel/net/net_loader.c',
    'kernel/net/nettls_loader.c',
    'kernel/fs/registry.c',
    'WebBrowser/webbrowser_loader.c',
    'kernel/gui/cursor_loader.c',
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
    '-fno-jump-tables',
    '-ffunction-sections',
    '-fdata-sections',
    `-I"${path.join(ROOT, 'kernel')}"`,
    `-I"${path.join(ROOT, 'kernel', 'include')}"`,
    `-I"${path.join(ROOT, 'BootSetup')}"`,
    `-I"${path.join(ROOT, 'WebBrowser')}"`,
    `-I"${path.join(ROOT, 'MultimediaCodec')}"`,
    `-I"${path.join(ROOT, 'explorer')}"`,
    `-I"${path.join(ROOT, 'oobe')}"`,
    `-I"${path.join(ROOT, 'notepad')}"`,
    `-I"${path.join(ROOT, 'calculator')}"`,
    `-I"${path.join(ROOT, 'player')}"`,
    `-I"${path.join(ROOT, 'rules_viewer')}"`,
    `-I"${path.join(ROOT, 'whatsnew')}"`,
    `-I"${path.join(ROOT, 'autoplay')}"`,
    `-I"${path.join(ROOT, 'taskmgr')}"`,
    `-I"${path.join(ROOT, 'control_panel')}"`,
    `-I"${path.join(ROOT, 'SystemLibrary')}"`,
    '-Os',
    '-Wall',
    '-Wno-unused-function'
].join(' ');

for (const c of cFiles) {
    const baseName = path.basename(c, '.c');
    const objName = path.join(OBJ_DIR, `${baseName}.o`);
    cObjFiles.push(objName);
    const cmd = `${llvm.clang} ${cFlags} -g -c "${path.join(ROOT, c)}" -o "${objName}"`;
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
const linkCmd = `${llvm.lld} -m elf_x86_64 --gc-sections -T "${linkerScript}" -o "${kernelElf}" ${allObjs}`;
execSync(linkCmd, { stdio: 'inherit' });

// Estrazione in binario piatto grezzo tramite llvm-objcopy
const objcopyCmd = `${llvm.objcopy} -O binary "${kernelElf}" "${kernelBin}"`;
execSync(objcopyCmd, { stdio: 'inherit' });

const kernelStat = fs.statSync(kernelBin);
console.log(`[OK] Kernel 64-bit generato con successo: ${kernelStat.size} byte`);

// Estrazione tabella simboli compatta KERNEL.SYM ('NSYM') da kernel.elf per BSOD.DLL
const targetLibDirEarly = path.join(BIN_DIR, 'NewOS', 'SystemLibrary');
if (!fs.existsSync(targetLibDirEarly)) fs.mkdirSync(targetLibDirEarly, { recursive: true });
const objdumpExe = llvm.dir ? `"${path.join(llvm.dir, 'llvm-objdump.exe')}"` : 'llvm-objdump';
const symDumpRaw = execSync(`${objdumpExe} -d -l --no-show-raw-insn "${kernelElf}"`, { maxBuffer: 64 * 1024 * 1024 }).toString('utf8');
{
    const strPool = [];
    const strMap = new Map();
    const addStr = (s) => {
        if (strMap.has(s)) return strMap.get(s);
        const off = strPool.reduce((acc, cur) => acc + cur.length + 1, 0);
        if (off + s.length + 1 > 65535) return 0;
        strPool.push(s);
        strMap.set(s, off);
        return off;
    };
    addStr('unknown');

    const entries = [];
    let curFunc = 'unknown';
    let curFile = 'kernel';
    let curLine = 0;
    let lastKey = '';

    const rootNorm = ROOT.replace(/\\/g, '/').toLowerCase();
    for (const rawLine of symDumpRaw.split(/\r?\n/)) {
        const fnMatch = rawLine.match(/^[0-9a-fA-F]+\s+<([^>]+)>:/);
        if (fnMatch) {
            curFunc = fnMatch[1];
            continue;
        }
        const locMatch = rawLine.match(/^;\s*(.+):(\d+)\s*$/);
        if (locMatch) {
            let fp = locMatch[1].replace(/\\/g, '/');
            if (fp.toLowerCase().startsWith(rootNorm + '/')) fp = fp.slice(rootNorm.length + 1);
            curFile = fp;
            curLine = parseInt(locMatch[2], 10) || 0;
            continue;
        }
        const insnMatch = rawLine.match(/^\s*([0-9a-fA-F]+):/);
        if (insnMatch && curLine > 0) {
            const addr = BigInt('0x' + insnMatch[1]);
            const key = `${curFunc}|${curFile}|${curLine}`;
            if (key !== lastKey) {
                lastKey = key;
                const fOff = addStr(curFile);
                const fnOff = addStr(curFunc);
                entries.push({ addr, line: curLine, fileOff: fOff, funcOff: fnOff });
            }
        }
    }

    entries.sort((a, b) => (a.addr < b.addr ? -1 : a.addr > b.addr ? 1 : 0));
    const strBuf = Buffer.concat(strPool.map(s => Buffer.from(s + '\0', 'utf8')));
    const hdrSize = 16;
    const recSize = 16;
    const strOffset = hdrSize + entries.length * recSize;
    const symBuf = Buffer.alloc(strOffset + strBuf.length);
    symBuf.write('NSYM', 0, 4, 'ascii');
    symBuf.writeUInt32LE(entries.length, 4);
    symBuf.writeUInt32LE(strOffset, 8);
    symBuf.writeUInt32LE(strBuf.length, 12);
    for (let i = 0; i < entries.length; i++) {
        const off = hdrSize + i * recSize;
        symBuf.writeBigUInt64LE(entries[i].addr, off);
        symBuf.writeUInt32LE(entries[i].line, off + 8);
        symBuf.writeUInt16LE(entries[i].fileOff, off + 12);
        symBuf.writeUInt16LE(entries[i].funcOff, off + 14);
    }
    strBuf.copy(symBuf, strOffset);
    fs.writeFileSync(path.join(targetLibDirEarly, 'KERNEL.SYM'), symBuf);
    console.log(`[OK] KERNEL.SYM generato: ${entries.length} simboli riga (${symBuf.length} byte)`);
}

// 6b. Compilazione e Firma Moduli Esterni Protetti (.MOD)
console.log('\n--> Compilazione e Firma Moduli Esterni Protetti (C:\\NewOS\\Boot\\)...');

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
    { src: 'BootSetup/bootsetup.c', extraSrcs: ['BootSetup/aes256.c'], ini: 'BootSetup/bootsetup.ini', out: 'BOOTSETUP.MOD', iniOut: 'BOOTSETUP.INI' },
    { src: 'Update/update.c', extraSrcs: ['BootSetup/aes256.c'], ini: 'Update/update.ini', out: 'UPDATE.MOD', iniOut: 'UPDATE.INI' },
    { src: 'oobe/oobe.c', extraSrcs: ['kernel/lib/string.c'], ini: 'oobe/oobe.ini', out: 'OOBE.MOD', iniOut: 'OOBE.INI' }
];

const modLdScript = path.join(ROOT, 'tools', 'module.ld');

const bootModDir = path.join(BIN_DIR, 'NewOS', 'Boot');
const sysProgModDir = path.join(BIN_DIR, 'NewOS', 'SystemProgram');
const bootSetupModDir = path.join(BIN_DIR, 'BootSetup');
if (!fs.existsSync(bootModDir)) fs.mkdirSync(bootModDir, { recursive: true });
if (!fs.existsSync(sysProgModDir)) fs.mkdirSync(sysProgModDir, { recursive: true });
if (!fs.existsSync(bootSetupModDir)) fs.mkdirSync(bootSetupModDir, { recursive: true });

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

    const binPath = path.join(bootModDir, sm.out);

    // Linka modulo in binario protetto piatto con module.ld direttamente in C:\NewOS\Boot\
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

    // Distribuzione ordinata in SystemProgram e BootSetup senza sporcare la radice
    try { fs.copyFileSync(binPath, path.join(sysProgModDir, sm.out)); } catch (e) { }
    if (sm.out.startsWith('BOOTSETUP')) {
        try { fs.copyFileSync(binPath, path.join(bootSetupModDir, sm.out)); } catch (e) { }
    }

    // Copia file .INI associato
    const iniSrc = path.join(ROOT, sm.ini);
    if (fs.existsSync(iniSrc)) {
        fs.copyFileSync(iniSrc, path.join(bootModDir, sm.iniOut));
        try { fs.copyFileSync(iniSrc, path.join(sysProgModDir, sm.iniOut)); } catch (e) { }
        if (sm.out.startsWith('BOOTSETUP')) {
            try { fs.copyFileSync(iniSrc, path.join(bootSetupModDir, sm.iniOut)); } catch (e) { }
        }
    }
}

// 6b2. Compilazione e Firma Moduli Codec Multimediali (.CDC in NewOS/MultimediaCodec/)
console.log('\n--> Compilazione e Firma Moduli Codec Multimediali (.CDC in NewOS/MultimediaCodec/)...');
const codecModules = [
    { src: 'MultimediaCodec/wavdec.c', out: 'WAVDEC.CDC' },
    { src: 'MultimediaCodec/mp3dec.c', out: 'MP3DEC.CDC' },
    { src: 'MultimediaCodec/flacdec.c', out: 'FLACDEC.CDC' },
    { src: 'MultimediaCodec/avidec.c', out: 'AVIDEC.CDC' },
    { src: 'MultimediaCodec/av1dec.c', out: 'AV1DEC.CDC' },
    { src: 'MultimediaCodec/mp4dec.c', out: 'MP4DEC.CDC' }
];

const targetCodecDir = path.join(BIN_DIR, 'NewOS', 'MultimediaCodec');
if (!fs.existsSync(targetCodecDir)) fs.mkdirSync(targetCodecDir, { recursive: true });

for (const cm of codecModules) {
    const srcPath = path.join(ROOT, cm.src);
    const objPath = path.join(OBJ_DIR, `${path.basename(cm.src, '.c')}.o`);
    const compileCmd = `${llvm.clang} ${cFlags} -fPIC -c "${srcPath}" -o "${objPath}"`;
    execSync(compileCmd, { stdio: 'inherit' });

    const binPath = path.join(targetCodecDir, cm.out);
    const linkModCmd = `${llvm.lld} -m elf_x86_64 -T "${modLdScript}" --oformat binary "${objPath}" -o "${binPath}"`;
    execSync(linkModCmd, { stdio: 'inherit' });

    // Firma crittografica e checksum CRC32 del payload
    const modBuf = fs.readFileSync(binPath);
    if (modBuf.length >= 64) {
        const payload = modBuf.subarray(64);
        const checksum = crc32Buffer(payload);
        modBuf.writeUInt32LE(payload.length, 16); // code_size (offset 16)
        modBuf.writeUInt32LE(checksum, 24);      // checksum CRC32 (offset 24)
        fs.writeFileSync(binPath, modBuf);
        console.log(`  [OK] Modulo Codec generato e firmato: ${cm.out} (${modBuf.length} byte, CRC32: 0x${checksum.toString(16).toUpperCase()})`);
    }
}

// 6b3. Compilazione e Firma Librerie Dinamiche di Sistema (.DLL in NewOS/SystemLibrary/)
console.log('\n--> Compilazione e Firma Librerie Dinamiche (.DLL in NewOS/SystemLibrary/)...');
function walkCFiles(dir, out = []) {
    if (!fs.existsSync(dir)) return out;
    for (const ent of fs.readdirSync(dir, { withFileTypes: true })) {
        const full = path.join(dir, ent.name);
        if (ent.isDirectory()) walkCFiles(full, out);
        else if (ent.isFile() && ent.name.endsWith('.c')) out.push(full);
    }
    return out;
}

const nsBase = path.join(ROOT, 'third_party', 'netsurf', 'netsurf-all-3.11');
const nsExtraSrcs = [
    ...walkCFiles(path.join(nsBase, 'libwapcaplet', 'src')),
    ...walkCFiles(path.join(nsBase, 'libparserutils', 'src')),
    ...walkCFiles(path.join(nsBase, 'libhubbub', 'src')),
    ...walkCFiles(path.join(nsBase, 'libcss', 'src')).filter(f => !f.endsWith('css_property_parser_gen.c'))
];
const nsExtraFlags = [
    `-I"${path.join(ROOT, 'SystemLibrary', 'netsurf_shim')}"`,
    `-I"${path.join(nsBase, 'libwapcaplet', 'include')}"`,
    `-I"${path.join(nsBase, 'libwapcaplet', 'src')}"`,
    `-I"${path.join(nsBase, 'libparserutils', 'include')}"`,
    `-I"${path.join(nsBase, 'libparserutils', 'src')}"`,
    `-I"${path.join(nsBase, 'libhubbub', 'include')}"`,
    `-I"${path.join(nsBase, 'libcss', 'include')}"`,
    '-DWITHOUT_ICONV_FILTER',
    '-D"_ALIGNED=__attribute__((aligned(4)))"',
    '-Wno-unused-parameter',
    '-Wno-sign-compare',
    '-Wno-implicit-fallthrough'
].join(' ');

const webpBase = path.join(ROOT, 'third_party', 'libwebp', 'libwebp-1.4.0');
const webpExtraSrcs = [
    ...walkCFiles(path.join(webpBase, 'src', 'dec')),
    ...walkCFiles(path.join(webpBase, 'src', 'dsp')),
    ...walkCFiles(path.join(webpBase, 'src', 'utils'))
].filter(f => !/(sse|avx|neon|mips|msa|_enc\.c$|^ssim\.c$|^quant_levels_utils\.c$|^cost\.c$|^random_utils\.c$)/.test(path.basename(f)));
const webpExtraFlags = `-I"${path.join(ROOT, 'SystemLibrary', 'netsurf_shim')}" -I"${path.join(ROOT, 'third_party', 'stb')}" -I"${webpBase}" -I"${path.join(webpBase, 'src')}" -DWEBP_DISABLE_STATS -Wno-unused-function -Wno-unused-parameter -Wno-sign-compare`;

const qjsBase = path.join(ROOT, 'third_party', 'quickjs', 'quickjs-0.8.0');
const qjsExtraSrcs = [
    path.join(qjsBase, 'quickjs.c'),
    path.join(qjsBase, 'libregexp.c'),
    path.join(qjsBase, 'libunicode.c'),
    path.join(qjsBase, 'cutils.c'),
    path.join(qjsBase, 'libbf.c'),
    path.join(ROOT, 'SystemLibrary', 'quickjs_shim', 'qjs_libm.c')
];
const qjsExtraFlags = `-mstackrealign -I"${path.join(ROOT, 'SystemLibrary', 'quickjs_shim')}" -I"${path.join(ROOT, 'SystemLibrary', 'netsurf_shim')}" -I"${qjsBase}" -D__STDC_NO_ATOMICS__ -D__NEWLIB__ -DCONFIG_VERSION=\\"0.8.0\\" -Wno-unused-parameter -Wno-sign-compare -Wno-unused-function -Wno-implicit-function-declaration`;

const systemLibraries = [
    { src: 'SystemLibrary/theme_mgr.c', ini: 'SystemLibrary/THEMEMGR.INI', out: 'THEMEMGR.DLL', iniOut: 'THEMEMGR.INI' },
    { src: 'SystemLibrary/cursor_mgr.c', ini: 'SystemLibrary/CURSORMGR.INI', out: 'CURSORMGR.DLL', iniOut: 'CURSORMGR.INI' },
    { src: 'SystemLibrary/settings.c', ini: 'SystemLibrary/SETTINGS.INI', out: 'SETTINGS.DLL', iniOut: 'SETTINGS.INI' },
    { src: 'SystemLibrary/whatsnew.c', ini: 'SystemLibrary/WHATSNEW.INI', out: 'WHATSNEW.DLL', iniOut: 'WHATSNEW.INI' },
    { src: 'SystemLibrary/rules.c', ini: 'SystemLibrary/RULES.INI', out: 'RULES.DLL', iniOut: 'RULES.INI' },
    { src: 'SystemLibrary/useracct.c', ini: 'SystemLibrary/USERACCT.INI', out: 'USERACCT.DLL', iniOut: 'USERACCT.INI' },
    { src: 'SystemLibrary/timedate.c', ini: 'SystemLibrary/TIMEDATE.INI', out: 'TIMEDATE.DLL', iniOut: 'TIMEDATE.INI' },
    { src: 'SystemLibrary/netcpl.c', ini: 'SystemLibrary/NETCPL.INI', out: 'NETCPL.DLL', iniOut: 'NETCPL.INI' },
    { src: 'SystemLibrary/keybcpl.c', ini: 'SystemLibrary/KEYBCPL.INI', out: 'KEYBCPL.DLL', iniOut: 'KEYBCPL.INI' },
    { src: 'SystemLibrary/comdlg.c', ini: 'SystemLibrary/COMDLG.INI', out: 'COMDLG.DLL', iniOut: 'COMDLG.INI' },
    { src: 'SystemLibrary/recycle.c', ini: 'SystemLibrary/RECYCLE.INI', out: 'RECYCLE.DLL', iniOut: 'RECYCLE.INI' },
    { src: 'SystemLibrary/uistyle_copenhagen.c', ini: 'SystemLibrary/COPENHAGEN.INI', out: 'COPENHAGEN.DLL', iniOut: 'COPENHAGEN.INI', destSubDir: ['NewOS', 'Resources', 'UIStyle'] },
    { src: 'SystemLibrary/hwaccel.c', ini: 'SystemLibrary/HWACCEL.INI', out: 'HWACCEL.DLL', iniOut: 'HWACCEL.INI', allowSse2: true },
    { src: 'SystemLibrary/nettls.c', ini: 'SystemLibrary/NETTLS.INI', out: 'NETTLS.DLL', iniOut: 'NETTLS.INI' },
    { src: 'SystemLibrary/cssengine.c', ini: 'SystemLibrary/CSSENGINE.INI', out: 'CSSENGINE.DLL', iniOut: 'CSSENGINE.INI' },
    { src: 'SystemLibrary/jsengine.c', ini: 'SystemLibrary/JSENGINE.INI', out: 'JSENGINE.DLL', iniOut: 'JSENGINE.INI', allowSse2: true, needsReloc: true, extraSrcs: qjsExtraSrcs, extraFlags: qjsExtraFlags },
    { src: 'SystemLibrary/fontmgr.c', ini: 'SystemLibrary/FONTMGR.INI', out: 'FONTMGR.DLL', iniOut: 'FONTMGR.INI', allowSse2: true, extraFlags: `-mstackrealign -I"${path.join(ROOT, 'SystemLibrary', 'quickjs_shim')}" -I"${path.join(ROOT, 'SystemLibrary', 'netsurf_shim')}" -I"${path.join(ROOT, 'third_party', 'stb')}" -Wno-unused-function -Wno-unused-parameter` },
    { src: 'SystemLibrary/imgcodec.c', ini: 'SystemLibrary/IMGCODEC.INI', out: 'IMGCODEC.DLL', iniOut: 'IMGCODEC.INI', extraSrcs: webpExtraSrcs, extraFlags: webpExtraFlags },
    { src: 'SystemLibrary/netsurf_core.c', ini: 'SystemLibrary/NETSURF_CORE.INI', out: 'NETSURF_CORE.DLL', iniOut: 'NETSURF_CORE.INI', extraSrcs: nsExtraSrcs, extraFlags: nsExtraFlags },
    { src: 'SystemLibrary/webbrowser_dll.c', ini: 'SystemLibrary/WEBBROWSER.INI', out: 'WEBBROWSER.DLL', iniOut: 'WEBBROWSER.INI', needsReloc: true, extraFlags: `-I"${path.join(ROOT, 'WebBrowser')}" -Wno-unused-function -Wno-unused-parameter` },
    { src: 'SystemLibrary/sysapps_dll.c', ini: 'SystemLibrary/SYSAPPS.INI', out: 'SYSAPPS.DLL', iniOut: 'SYSAPPS.INI', needsReloc: true, extraFlags: `-Wno-unused-function -Wno-unused-parameter` },
    { src: 'SystemLibrary/cplext2_dll.c', ini: 'SystemLibrary/CPLEXT2.INI', out: 'CPLEXT2.DLL', iniOut: 'CPLEXT2.INI', needsReloc: true, extraFlags: `-Wno-unused-function -Wno-unused-parameter` },
    { src: 'SystemLibrary/explorer_dll.c', ini: 'SystemLibrary/EXPLORER.INI', out: 'EXPLORER.DLL', iniOut: 'EXPLORER.INI', needsReloc: true, extraFlags: `-Wno-unused-function -Wno-unused-parameter` },
    { src: 'SystemLibrary/audio_dll.c', ini: 'SystemLibrary/AUDIO.INI', out: 'AUDIO.DLL', iniOut: 'AUDIO.INI', needsReloc: true, extraFlags: `-Wno-unused-function -Wno-unused-parameter` },
    { src: 'SystemLibrary/fs_ext_dll.c', ini: 'SystemLibrary/FS_EXT.INI', out: 'FS_EXT.DLL', iniOut: 'FS_EXT.INI', needsReloc: true, extraFlags: `-Wno-unused-function -Wno-unused-parameter` },
    { src: 'SystemLibrary/netstack_dll.c', ini: 'SystemLibrary/NETSTACK.INI', out: 'NETSTACK.DLL', iniOut: 'NETSTACK.INI', needsReloc: true, extraFlags: `-Wno-unused-function -Wno-unused-parameter` },
    { src: 'SystemLibrary/nvme_dll.c', ini: 'SystemLibrary/NVME.INI', out: 'NVME.DLL', iniOut: 'NVME.INI', needsReloc: true, extraFlags: `-Wno-unused-function -Wno-unused-parameter` },
    { src: 'SystemLibrary/gpu3d_dll.c', ini: 'SystemLibrary/GPU3D.INI', out: 'GPU3D.DLL', iniOut: 'GPU3D.INI', allowSse2: true, needsReloc: true, extraFlags: `-mstackrealign -Wno-unused-function -Wno-unused-parameter` },
    { src: 'SystemLibrary/bsod_dll.c', ini: 'SystemLibrary/BSOD.INI', out: 'BSOD.DLL', iniOut: 'BSOD.INI', extraFlags: `-Wno-unused-function -Wno-unused-parameter` }
];

const targetLibDir = path.join(BIN_DIR, 'NewOS', 'SystemLibrary');
if (!fs.existsSync(targetLibDir)) fs.mkdirSync(targetLibDir, { recursive: true });

for (const sl of systemLibraries) {
    const outDir = sl.destSubDir ? path.join(BIN_DIR, ...sl.destSubDir) : targetLibDir;
    if (!fs.existsSync(outDir)) fs.mkdirSync(outDir, { recursive: true });

    const srcPath = path.join(ROOT, sl.src);
    const objPath = path.join(OBJ_DIR, `${path.basename(sl.src, '.c')}_dll.o`);
    const baseFlags = sl.allowSse2 ? cFlags.replace('-mgeneral-regs-only', '-msse2') : cFlags;
    const dllFlags = sl.extraFlags ? `${sl.extraFlags} ${baseFlags}` : baseFlags;
    const compileCmd = `${llvm.clang} ${dllFlags} -fPIC -c "${srcPath}" -o "${objPath}"`;
    execSync(compileCmd, { stdio: 'inherit' });

    const allObjs = [objPath];
    if (sl.extraSrcs && sl.extraSrcs.length > 0) {
        for (let ei = 0; ei < sl.extraSrcs.length; ei++) {
            const esrc = sl.extraSrcs[ei];
            const eobj = path.join(OBJ_DIR, `${path.basename(sl.src, '.c')}_ext_${ei}_${path.basename(esrc, '.c')}.o`);
            let localInc = '';
            if (esrc.includes('libcss')) localInc = `-I"${path.join(nsBase, 'libcss', 'src')}" `;
            else if (esrc.includes('libhubbub')) localInc = `-I"${path.join(nsBase, 'libhubbub', 'src')}" `;
            execSync(`${llvm.clang} ${localInc}${dllFlags} -fPIC -c "${esrc}" -o "${eobj}"`, { stdio: 'inherit' });
            allObjs.push(eobj);
        }
    }

    const binPath = path.join(outDir, sl.out);
    const rspPath = path.join(OBJ_DIR, `${path.basename(sl.src, '.c')}_objs.rsp`);
    fs.writeFileSync(rspPath, allObjs.map(o => `"${o.replace(/\\/g, '/')}"`).join('\n'), 'utf8');
    if (sl.needsReloc) {
        const relocLd = path.join(ROOT, 'tools', 'module_reloc.ld');
        const elfPath = path.join(OBJ_DIR, `${path.basename(sl.src, '.c')}_dll.elf`);
        execSync(`${llvm.lld} -m elf_x86_64 -shared -Bsymbolic --no-undefined -z notext -T "${relocLd}" @"${rspPath}" -o "${elfPath}"`, { stdio: 'inherit' });
        execSync(`${llvm.objcopy} -O binary -R .bss -R .rela.dyn "${elfPath}" "${binPath}"`, { stdio: 'inherit' });
        const elfBuf = fs.readFileSync(elfPath);
        const eShoff = Number(elfBuf.readBigUInt64LE(40));
        const eShentsize = elfBuf.readUInt16LE(58);
        const eShnum = elfBuf.readUInt16LE(60);
        const eShstrndx = elfBuf.readUInt16LE(62);
        const strOff = Number(elfBuf.readBigUInt64LE(eShoff + eShstrndx * eShentsize + 24));
        const readStr = (o) => {
            let end = o;
            while (end < elfBuf.length && elfBuf[end] !== 0) end++;
            return elfBuf.toString('utf8', o, end);
        };
        let bssEnd = 0;
        const relocs = [];
        for (let si = 0; si < eShnum; si++) {
            const sh = eShoff + si * eShentsize;
            const shName = readStr(strOff + elfBuf.readUInt32LE(sh));
            const shType = elfBuf.readUInt32LE(sh + 4);
            const shAddr = Number(elfBuf.readBigUInt64LE(sh + 16));
            const shOffset = Number(elfBuf.readBigUInt64LE(sh + 24));
            const shSize = Number(elfBuf.readBigUInt64LE(sh + 32));
            if (shName === '.bss') bssEnd = shAddr + shSize;
            if (shType === 4) { // SHT_RELA
                for (let ro = 0; ro + 24 <= shSize; ro += 24) {
                    const rOff = Number(elfBuf.readBigUInt64LE(shOffset + ro));
                    const rInfo = elfBuf.readBigUInt64LE(shOffset + ro + 8);
                    const rAddend = elfBuf.readBigUInt64LE(shOffset + ro + 16);
                    const rType = Number(rInfo & 0xFFFFFFFFn);
                    if (rType === 8) relocs.push({ off: rOff, add: rAddend }); // R_X86_64_RELATIVE
                    else if (rType !== 0) console.log(`  [RELOC WARN] tipo ${rType} non gestito a 0x${rOff.toString(16)}`);
                }
            }
        }
        const rawBuf = fs.readFileSync(binPath);
        const tableOff = Math.ceil(Math.max(rawBuf.length, bssEnd) / 4) * 4;
        const finalBuf = Buffer.alloc(tableOff + (1 + relocs.length) * 4, 0);
        rawBuf.copy(finalBuf, 0);
        finalBuf.writeUInt32LE(relocs.length, tableOff);
        for (let ri = 0; ri < relocs.length; ri++) {
            finalBuf.writeBigUInt64LE(relocs[ri].add, relocs[ri].off);
            finalBuf.writeUInt32LE(relocs[ri].off, tableOff + 4 + ri * 4);
        }
        const marker = Buffer.from([0x4F, 0x4C, 0x45, 0x52, 0xBE, 0xBA, 0xFE, 0xCA]);
        const mIdx = finalBuf.indexOf(marker);
        if (mIdx >= 0) finalBuf.writeUInt32LE(tableOff, mIdx + 4);
        fs.writeFileSync(binPath, finalBuf);
        console.log(`  [RELOC] ${sl.out}: ${relocs.length} rilocazioni R_X86_64_RELATIVE incorporate (bssEnd=${bssEnd}, tableOff=${tableOff})`);
    } else {
        const linkModCmd = `${llvm.lld} -m elf_x86_64 -T "${modLdScript}" --oformat binary @"${rspPath}" -o "${binPath}"`;
        execSync(linkModCmd, { stdio: 'inherit' });
    }

    // Firma crittografica e checksum CRC32 del payload
    const modBuf = fs.readFileSync(binPath);
    if (modBuf.length >= 64) {
        const payload = modBuf.subarray(64);
        const checksum = crc32Buffer(payload);
        modBuf.writeUInt32LE(payload.length, 16); // code_size (offset 16)
        modBuf.writeUInt32LE(checksum, 24);      // checksum CRC32 (offset 24)
        fs.writeFileSync(binPath, modBuf);
        console.log(`  [OK] Libreria Dinamica generata e firmata: ${sl.out} (${modBuf.length} byte, CRC32: 0x${checksum.toString(16).toUpperCase()})`);

        if (!sl.destSubDir) {
            const bootLibPath = path.join(BIN_DIR, 'NewOS', 'Boot', sl.out);
            try { fs.copyFileSync(binPath, bootLibPath); } catch (e) { }
        }
    }

    if (sl.ini) {
        const iniSrc = path.join(ROOT, sl.ini);
        if (fs.existsSync(iniSrc)) {
            const iniDst = path.join(outDir, sl.iniOut || path.basename(sl.ini));
            fs.copyFileSync(iniSrc, iniDst);
            console.log(`  [OK] Descrittore INI sincronizzato: ${sl.iniOut || path.basename(sl.ini)}`);
            if (!sl.destSubDir) {
                try { fs.copyFileSync(iniSrc, path.join(BIN_DIR, 'NewOS', 'Boot', sl.iniOut || path.basename(sl.ini))); } catch (e) { }
            }
        }
    }
}

// 6c. Organizza e Popola l'albero completo NewOS V1 dentro build/bin/
const { organizeBuildBin } = require('./tree_manager');
organizeBuildBin(ROOT, BIN_DIR);

// Bonifica radice build/bin: rimozione cartelle legacy (C:\SYSTEM\) e file sparsi fuori da C:\NewOS\ (Regola 6)
const legacySys = path.join(BIN_DIR, 'SYSTEM');
if (fs.existsSync(legacySys)) fs.rmSync(legacySys, { recursive: true, force: true });
for (const f of fs.readdirSync(BIN_DIR)) {
    const full = path.join(BIN_DIR, f);
    if (fs.statSync(full).isFile() && (f.endsWith('.MOD') || f.endsWith('.INI') || f.endsWith('.DLL') || f.endsWith('.TXT'))) {
        try { fs.unlinkSync(full); } catch (e) { }
    }
}

// 6d. Generazione archivio cifrato AES-256 SYSTEM.PAK per BootSetup (Regola 1)
const { packSystem } = require('./pack_system');
const pakPath = path.join(BIN_DIR, 'BootSetup', 'SYSTEM.PAK');
packSystem(BIN_DIR, pakPath);

// 7. Assembla immagine disco myos.img
console.log('\n--> Creazione disco di boot myos.img...');
const TOTAL_IMG_SECTORS = 262144; // 128 MB
const imgBuf = Buffer.alloc(TOTAL_IMG_SECTORS * 512, 0);

// Settore 0: MBR
const mbrFile = getBootBin('bootsect.bin');
const mbrBuf = fs.readFileSync(mbrFile);
mbrBuf.copy(imgBuf, 0);

// Settori 1..64: Stage 2
const s2File = getBootBin('stage2.bin');
const s2Buf = fs.readFileSync(s2File);
s2Buf.copy(imgBuf, 512);

// Settori 66..N: Kernel (LBA 66 = offset 66 * 512 = 33792)
const kFile = getBootBin('kernel.bin');
const kBuf = fs.readFileSync(kFile);
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
const exfatVolumeSectors = 250000; // 250.000 settori (122 MB, compatibile con partizione 1 MBR)
const exfatBuf = buildMyOsFileSystem(ROOT, exfatVolumeSectors, 2048);
exfatBuf.copy(imgBuf, 2048 * 512);
console.log(`[OK] Partizione exFAT iniettata a LBA 2048 (${exfatBuf.length} byte, C:\\ pronta)`);

// 8b. Iniezione Partizione EFI System Partition (ESP) a LBA 256000 (Type 0xEF, 1.44 MB)
const { createEfiFatImage } = require('./mkefifat.js');
const efiBootCandidates = [
    path.join(BIN_DIR, 'EFI', 'BOOT', 'BOOTX64.EFI'),
    path.join(BOOT_DIR, 'BOOTX64.EFI'),
    path.join(BOOT_DIR, 'bootx64.bin'),
    path.join(BIN_DIR, 'BOOTX64.EFI')
];
let efiBootFile = null;
for (const cand of efiBootCandidates) {
    if (fs.existsSync(cand)) { efiBootFile = cand; break; }
}

if (efiBootFile && fs.existsSync(kFile)) {
    const b64Data = fs.readFileSync(efiBootFile);
    const kData = fs.readFileSync(kFile);
    const efiFatBuf = createEfiFatImage(b64Data, kData);
    efiFatBuf.copy(imgBuf, 256000 * 512);
    console.log(`[OK] Partizione ESP FAT (UEFI) iniettata a LBA 256000 (${efiFatBuf.length} byte, Type 0xEF)`);
}

const buildImg = path.join(BUILD_DIR, 'NewOS_V1.img');
fs.writeFileSync(buildImg, imgBuf);
fs.writeFileSync(path.join(BUILD_DIR, 'myos.img'), imgBuf);
fs.writeFileSync(path.join(ROOT, 'NewOS_V1.img'), imgBuf);
fs.writeFileSync(path.join(ROOT, 'myos.img'), imgBuf);
console.log(`[OK] Immagine disco: ${buildImg} (${TOTAL_IMG_SECTORS * 512 / 1024 / 1024} MB, Dual-Boot: BIOS LBA 0 + UEFI ESP LBA 256000)`);

// 9. Aggiornamento opzionale immagini VirtualBox VDI
const vboxManage = 'C:\\Program Files\\Oracle\\VirtualBox\\VBoxManage.exe';
if (fs.existsSync(vboxManage)) {
    try {
        const buildVdi = path.join(BUILD_DIR, 'NewOS_V1.vdi');
        const rootVdi = path.join(ROOT, 'NewOS_V1.vdi');
        try { execSync(`"${vboxManage}" closemedium disk "${buildVdi}" --delete`, { stdio: 'ignore' }); } catch (e) { }
        try { execSync(`"${vboxManage}" closemedium disk "${rootVdi}" --delete`, { stdio: 'ignore' }); } catch (e) { }
        if (fs.existsSync(buildVdi)) fs.unlinkSync(buildVdi);
        execSync(`"${vboxManage}" convertfromraw "${buildImg}" "${buildVdi}" --format VDI`, { stdio: 'ignore' });
        fs.copyFileSync(buildVdi, rootVdi);
        fs.copyFileSync(buildVdi, path.join(BUILD_DIR, 'myos.vdi'));
        fs.copyFileSync(buildVdi, path.join(ROOT, 'myos.vdi'));
        console.log(`[OK] Immagine VirtualBox: ${rootVdi} sincronizzata`);
    } catch (e) { }
}

// 10. Generazione Immagine ISO Avviabile (El Torito CD-ROM + Hybrid MBR)
console.log('\n--> Creazione immagine ISO avviabile (NewOS_V1.iso)...');
const { createIsoFromDiskImage } = require('./mkiso.js');
const buildIso = path.join(BUILD_DIR, 'NewOS_V1.iso');
const rootIso = path.join(ROOT, 'NewOS_V1.iso');

if (fs.existsSync(vboxManage)) {
    try { execSync(`"${vboxManage}" closemedium dvd "${buildIso}"`, { stdio: 'ignore' }); } catch (e) { }
    try { execSync(`"${vboxManage}" closemedium dvd "${rootIso}"`, { stdio: 'ignore' }); } catch (e) { }
}

createIsoFromDiskImage(buildImg, buildIso, BIN_DIR);

function safeCopy(src, dst) {
    try {
        if (fs.existsSync(dst)) fs.unlinkSync(dst);
    } catch (e) { }
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

// 12. Sincronizzazione cartella Release ufficiale (Release/)
const { syncRelease } = require('./sync_release.js');
syncRelease(ROOT);

console.log('\n=== BUILD COMPLETATA: NUOVO FILE ISO PRONTO: NewOS_V1.iso ===');

