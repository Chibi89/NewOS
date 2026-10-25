const fs = require('fs');
const path = require('path');
const { execSync } = require('child_process');

function buildEfiBootloader(ROOT, BIN_DIR, OBJ_DIR, llvm) {
    console.log('\n--> Compilazione Bootloader Nativo UEFI (BOOTX64.EFI)...');

    const src = path.join(ROOT, 'boot', 'efi', 'bootx64.c');
    const obj = path.join(OBJ_DIR, 'bootx64.o');
    const outEfi = path.join(BIN_DIR, 'BOOTX64.EFI');
    const efiBootDir = path.join(BIN_DIR, 'EFI', 'BOOT');

    if (!fs.existsSync(efiBootDir)) {
        fs.mkdirSync(efiBootDir, { recursive: true });
    }

    const incDir = path.join(ROOT, 'boot', 'efi');
    const compCmd = `${llvm.clang} -target x86_64-w64-mingw32 -ffreestanding -fno-builtin -fshort-wchar -mno-red-zone -Wall -Wextra -I"${incDir}" -c "${src}" -o "${obj}"`;

    const binDir = llvm.dir || path.dirname(llvm.clang.replace(/"/g, ''));
    const env = { ...process.env, PATH: `${binDir};${process.env.PATH || ''}` };

    execSync(compCmd, { stdio: 'inherit', env });

    const linkCmd = `${llvm.lld} -flavor link /subsystem:efi_application /entry:efi_main "${obj}" /out:"${outEfi}"`;
    execSync(linkCmd, { stdio: 'inherit', env });

    if (fs.existsSync(outEfi)) {
        const stat = fs.statSync(outEfi);
        const finalEfi = path.join(efiBootDir, 'BOOTX64.EFI');
        fs.copyFileSync(outEfi, finalEfi);

        const newOsBootDir = path.join(BIN_DIR, 'NewOS', 'Boot');
        if (!fs.existsSync(newOsBootDir)) fs.mkdirSync(newOsBootDir, { recursive: true });
        fs.copyFileSync(outEfi, path.join(newOsBootDir, 'BOOTX64.EFI'));
        fs.copyFileSync(outEfi, path.join(newOsBootDir, 'bootx64.bin'));

        console.log(`[OK] Bootloader UEFI generato: ${outEfi} (${stat.size} byte) -> ${finalEfi} e ${newOsBootDir}`);
        return { success: true, size: stat.size, path: outEfi, espPath: finalEfi };
    } else {
        throw new Error('Generazione BOOTX64.EFI fallita: file non trovato.');
    }
}

module.exports = { buildEfiBootloader };

if (require.main === module) {
    const ROOT = path.resolve(__dirname, '..');
    const BUILD_DIR = path.join(ROOT, 'build');
    const BIN_DIR = path.join(BUILD_DIR, 'bin');
    const OBJ_DIR = path.join(BUILD_DIR, 'obj');

    const llvm = {
        dir: path.join(ROOT, 'tools', 'llvm-mingw', 'llvm-mingw-20260616-ucrt-x86_64', 'bin'),
        clang: `"${path.join(ROOT, 'tools', 'llvm-mingw', 'llvm-mingw-20260616-ucrt-x86_64', 'bin', 'clang.exe')}"`,
        lld: `"${path.join(ROOT, 'tools', 'llvm-mingw', 'llvm-mingw-20260616-ucrt-x86_64', 'bin', 'ld.lld.exe')}"`
    };

    buildEfiBootloader(ROOT, BIN_DIR, OBJ_DIR, llvm);
}
