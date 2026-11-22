/**
 * tools/test_all.js
 * Suite di Regressione Automatica Unificata — NewOS V1 (64-bit)
 * Verifica: Budget Kernel (<480 KB), Firma NOSM dei moduli Out-of-Kernel,
 * presenza immagini disco/ISO e Boot QEMU BIOS completo fino al Desktop.
 */

const fs = require('fs');
const path = require('path');
const { spawn } = require('child_process');

const ROOT = path.resolve(__dirname, '..');
const BIN_DIR = path.join(ROOT, 'build', 'bin');
const SYSLIB = path.join(BIN_DIR, 'NewOS', 'SystemLibrary');
const MAX_KERNEL_BYTES = 491520;

let passed = 0;
let failed = 0;

function check(name, cond, detail = '') {
    if (cond) {
        console.log(`  [PASS] ${name}${detail ? ' — ' + detail : ''}`);
        passed++;
    } else {
        console.error(`  [FAIL] ${name}${detail ? ' — ' + detail : ''}`);
        failed++;
    }
}

async function runQemuBootTest() {
    const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
    const imgPath = path.join(ROOT, 'myos.img');
    const isoPath = path.join(ROOT, 'NewOS_V1.iso');
    const serialLog = path.join(ROOT, 'tools', 'serial_test_all.log');
    if (fs.existsSync(serialLog)) try { fs.unlinkSync(serialLog); } catch (e) {}

    if (!fs.existsSync(qemuBin)) {
        check('QEMU BIOS Boot Test', false, 'qemu-system-x86_64.exe mancante');
        return;
    }

    const net = require('net');
    const qemu = spawn(qemuBin, [
        '-drive', `format=raw,file=${imgPath}`,
        '-cdrom', isoPath,
        '-m', '256M',
        '-vga', 'std',
        '-serial', `file:${serialLog}`,
        '-display', 'none',
        '-monitor', 'tcp:127.0.0.1:4539,server,nowait'
    ], { stdio: 'ignore' });

    let bootMenuOk = false;
    let explorerOk = false;
    let sentBootKey = false;
    for (let i = 0; i < 80; i++) {
        await new Promise(r => setTimeout(r, 300));
        if (fs.existsSync(serialLog)) {
            const log = fs.readFileSync(serialLog, 'utf8');
            if (log.includes('[BOOT_MENU] Pronto!')) {
                bootMenuOk = true;
                if (!sentBootKey) {
                    sentBootKey = true;
                    const c = net.createConnection({ host: '127.0.0.1', port: 4539 }, () => {
                        c.write('sendkey 1\n');
                        setTimeout(() => { try { c.end(); } catch (e) {} }, 150);
                    });
                    c.on('error', () => {});
                }
            }
            if (log.includes("Modulo 'EXPLORER' verificato")) explorerOk = true;
            if (bootMenuOk && explorerOk) break;
        }
    }

    try { qemu.kill(); } catch (e) {}
    check('QEMU BIOS Boot -> Boot Menu', bootMenuOk);
    check('QEMU Desktop -> EXPLORER.DLL NOSM+CRC32', explorerOk);
}

async function main() {
    console.log('=== NewOS V1 — Suite di Regressione Automatica Unificata ===\n');

    const kbin = path.join(ROOT, 'build', 'bin', 'NewOS', 'Boot', 'KERNEL.BIN');
    const kExists = fs.existsSync(kbin);
    const kSize = kExists ? fs.statSync(kbin).size : 0;
    check('Budget Kernel (< 491.520 B)', kExists && kSize > 0 && kSize < MAX_KERNEL_BYTES,
          `${kSize} B (${((kSize / MAX_KERNEL_BYTES) * 100).toFixed(1)}%)`);

    const requiredDlls = [
        'EXPLORER.DLL', 'LAUNCHER.DLL', 'SECCPL.DLL', 'COMCTL.DLL', 'COMDLG.DLL',
        'STARTMENU.DLL', 'FONTSCPL.DLL', 'PKGSVC.DLL', 'PORTS.DLL', 'BENCH.DLL', 'IMGVIEW.DLL'
    ];
    for (const dll of requiredDlls) {
        const p = path.join(SYSLIB, dll);
        let ok = false;
        let sz = 0;
        if (fs.existsSync(p)) {
            const buf = fs.readFileSync(p);
            sz = buf.length;
            ok = sz > 64 && buf.slice(0, 4).toString('ascii') === 'NOSM';
        }
        check(`Modulo Out-of-Kernel ${dll}`, ok, `${sz} B`);
    }

    check('Immagine Disco myos.img', fs.existsSync(path.join(ROOT, 'myos.img')));
    check('Immagine ISO NewOS_V1.iso', fs.existsSync(path.join(ROOT, 'NewOS_V1.iso')));

    await runQemuBootTest();

    console.log(`\n=== RISULTATO: ${passed} PASS / ${failed} FAIL ===`);
    process.exit(failed > 0 ? 1 : 0);
}

main();
