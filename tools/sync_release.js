const fs = require('fs');
const path = require('path');

function syncRelease(rootDir) {
    const ROOT = rootDir || path.resolve(__dirname, '..');
    const BIN_DIR = path.join(ROOT, 'build', 'bin');
    const RELEASE_DIR = path.join(ROOT, 'Release');
    const OLD_RELASE_DIR = path.join(ROOT, 'Relase');

    console.log('[RELEASE] Generazione e sincronizzazione cartella Release ufficiale...');

    // 1. Rimuovi vecchia cartella 'Relase' se presente per evitare duplicati obsoleti
    if (fs.existsSync(OLD_RELASE_DIR)) {
        try {
            fs.rmSync(OLD_RELASE_DIR, { recursive: true, force: true });
            console.log('[RELEASE] Rimossa vecchia cartella "Relase" disordinata.');
        } catch (e) {}
    }

    // 2. Crea struttura cartelle Release
    const dirs = [
        path.join(RELEASE_DIR, 'BootSetup'),
        path.join(RELEASE_DIR, 'EFI', 'BOOT'),
        path.join(RELEASE_DIR, 'NewOS', 'Boot')
    ];

    for (const d of dirs) {
        if (!fs.existsSync(d)) {
            fs.mkdirSync(d, { recursive: true });
        }
    }

    function safeCopy(src, dst) {
        try {
            if (fs.existsSync(dst)) fs.unlinkSync(dst);
        } catch(e) {}
        try {
            fs.copyFileSync(src, dst);
        } catch(e) {
            try {
                fs.writeFileSync(dst, fs.readFileSync(src));
            } catch(e2) {}
        }
    }

    // 3. Copia file in Release/BootSetup/
    const bootSetupSrc = path.join(BIN_DIR, 'BootSetup');
    const bootSetupDst = path.join(RELEASE_DIR, 'BootSetup');
    const bootSetupFiles = ['BOOTSETUP.INI', 'BOOTSETUP.MOD', 'SYSTEM.PAK'];
    for (const f of bootSetupFiles) {
        const src = path.join(bootSetupSrc, f);
        if (fs.existsSync(src)) {
            safeCopy(src, path.join(bootSetupDst, f));
        }
    }

    // 4. Copia file in Release/EFI/BOOT/
    const efiSrc = path.join(BIN_DIR, 'EFI', 'BOOT', 'BOOTX64.EFI');
    const efiDst = path.join(RELEASE_DIR, 'EFI', 'BOOT', 'BOOTX64.EFI');
    if (fs.existsSync(efiSrc)) {
        safeCopy(efiSrc, efiDst);
    }

    // 5. Copia file di boot in Release/NewOS/Boot/ (Escluso kernel.elf di debug)
    const newOsBootSrc = path.join(BIN_DIR, 'NewOS', 'Boot');
    const newOsBootDst = path.join(RELEASE_DIR, 'NewOS', 'Boot');
    const bootFiles = [
        'bootsect.bin',
        'stage2.bin',
        'kernel.bin',
        'bootx64.bin',
        'BOOTX64.EFI',
        'BOOTSCR.MOD',
        'BOOTSCR.INI',
        'BOOTSETUP.MOD',
        'BOOTSETUP.INI',
        'LOGIN.MOD',
        'LOGIN.INI',
        'LOGOUT.MOD',
        'LOGOUT.INI'
    ];

    for (const f of bootFiles) {
        const src = path.join(newOsBootSrc, f);
        if (fs.existsSync(src)) {
            safeCopy(src, path.join(newOsBootDst, f));
        }
    }

    // File LEGGIMI esplicativo per gli utenti del pacchetto Release
    const readmeContent = `================================================================================
NewOS V1 64-bit - Pacchetto di Distribuzione e Installazione (Release)
(C) Newspoint Software - NewSoft
================================================================================
Questo pacchetto contiene tutti i componenti necessari per creare un supporto
avviabile (Live USB / ISO / Disco Fisico) o eseguire l'installazione guidata
di NewOS V1 su silicio reale bare-metal x86_64 o macchina virtuale.

STRUTTURA DELLE CARTELLE:
1. \\EFI\\BOOT\\BOOTX64.EFI
   -> Bootloader nativo UEFI a 64 bit compatibile con qualsiasi PC moderno.
      Configura GOP (Graphics Output Protocol), legge il kernel e avvia NewOS.

2. \\BootSetup\\
   -> Contiene il programma di installazione guidata BOOTSETUP.MOD e l'archivio
      crittografato SYSTEM.PAK (AES-256) con l'intero sistema operativo.

3. \\NewOS\\Boot\\
   -> Cartella centralizzata dei componenti di avvio:
      - kernel.bin    : Kernel 64-bit monolitico ottimizzato (< 480 KB)
      - stage2.bin    : Bootloader Stage 2 (BIOS Legacy MBR)
      - bootsect.bin  : Settore MBR primario a 512 byte (LBA 0)
      - BOOTSCR.MOD   : Modulo protetto dello Splash Screen di avvio
      - LOGIN.MOD     : Modulo protetto di autenticazione e logon utente
      - LOGOUT.MOD    : Modulo protetto di disconnessione e arresto sistema

GUIDA ALL'AVVIO SU CHIAVETTA USB (UEFI):
Copiare l'intero contenuto di questa cartella nella radice di una chiavetta USB
formattata in FAT32 per avviare istantaneamente NewOS V1 su qualsiasi computer.
================================================================================
`;
    fs.writeFileSync(path.join(RELEASE_DIR, 'LEGGIMI.TXT'), readmeContent);
    fs.writeFileSync(path.join(RELEASE_DIR, 'README.TXT'), readmeContent);

    console.log('[RELEASE OK] Cartella Release sincronizzata e organizzata con successo.');
}

module.exports = { syncRelease };

if (require.main === module) {
    syncRelease();
}
