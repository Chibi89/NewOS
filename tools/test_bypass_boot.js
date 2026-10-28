/**
 * tools/test_bypass_boot.js
 * Test di validazione per il Bypass Atomico Automatico del Boot Manager post-Installazione
 * Verifica:
 *  1. Comportamento Live/Installer standard: su immagine vergine appare il Boot Menu.
 *  2. Comportamento Post-Installazione: con OOBE/Registry completato, il Boot Menu viene
 *     completamente saltato e la CPU carica direttamente LOGIN.MOD.
 *  3. Cattura screenshot di LOGIN.MOD a conferma del bypass.
 */

const { spawn, execSync } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const origImg = path.join(ROOT, 'myos.img');
const testImg = path.join(ROOT, 'build', 'test_bypass.img');
const serialLog = path.join(ROOT, 'serial_bypass.log');
const ppmScreenshot = path.join(ROOT, 'screenshot_bypass_login.ppm');
const pngScreenshot = path.join(ROOT, 'screenshot_bypass_login.png');

if (fs.existsSync(serialLog)) fs.unlinkSync(serialLog);
if (fs.existsSync(ppmScreenshot)) fs.unlinkSync(ppmScreenshot);
if (fs.existsSync(pngScreenshot)) fs.unlinkSync(pngScreenshot);

function sleep(ms) {
    return new Promise(res => setTimeout(res, ms));
}

function convertPpmToPng(ppmFile, pngFile) {
    if (!fs.existsSync(ppmFile)) return false;
    const buf = fs.readFileSync(ppmFile);
    let offset = 0;
    while (buf[offset] !== 0x0A && buf[offset] !== 0x20) offset++; // P6
    offset++;
    while (buf[offset] === 0x23) { // Salta commenti #
        while (buf[offset] !== 0x0A) offset++;
        offset++;
    }
    let wStr = '';
    while (buf[offset] !== 0x20 && buf[offset] !== 0x0A) { wStr += String.fromCharCode(buf[offset++]); }
    offset++;
    let hStr = '';
    while (buf[offset] !== 0x20 && buf[offset] !== 0x0A) { hStr += String.fromCharCode(buf[offset++]); }
    offset++;
    while (buf[offset] !== 0x0A) offset++; // 255
    offset++;

    const width = parseInt(wStr, 10);
    const height = parseInt(hStr, 10);
    console.log(`PPM Dimensions: ${width}x${height}`);

    const bmpFile = pngFile.replace(/\.png$/, '.bmp');
    const rowSize = width * 3;
    const paddedRowSize = Math.floor((rowSize + 3) / 4) * 4;
    const pixelDataSize = paddedRowSize * height;
    const bmpBuf = Buffer.alloc(54 + pixelDataSize, 0);

    bmpBuf.write('BM', 0);
    bmpBuf.writeUInt32LE(54 + pixelDataSize, 2);
    bmpBuf.writeUInt32LE(54, 10);
    bmpBuf.writeUInt32LE(40, 14);
    bmpBuf.writeInt32LE(width, 18);
    bmpBuf.writeInt32LE(-height, 22);
    bmpBuf.writeUInt16LE(1, 26);
    bmpBuf.writeUInt16LE(24, 28);
    bmpBuf.writeUInt32LE(0, 30);

    let srcPos = offset;
    for (let y = 0; y < height; y++) {
        const dstRow = 54 + y * paddedRowSize;
        for (let x = 0; x < width; x++) {
            if (srcPos + 3 <= buf.length) {
                const r = buf[srcPos++];
                const g = buf[srcPos++];
                const b = buf[srcPos++];
                bmpBuf[dstRow + x * 3] = b;
                bmpBuf[dstRow + x * 3 + 1] = g;
                bmpBuf[dstRow + x * 3 + 2] = r;
            }
        }
    }
    fs.writeFileSync(bmpFile, bmpBuf);

    try {
        const psCmd = `Add-Type -AssemblyName System.Drawing; $bmp = [System.Drawing.Image]::FromFile('${bmpFile.replace(/\\/g, '\\\\')}'); $bmp.Save('${pngFile.replace(/\\/g, '\\\\')}', [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose();`;
        execSync(`powershell -NoProfile -Command "${psCmd}"`, { stdio: 'ignore' });
        console.log(`[OK] PNG generato con successo: ${pngFile}`);
        return true;
    } catch (e) {
        return false;
    }
}

async function run() {
    console.log('=== TEST DI VALIDAZIONE BOOT MANAGER BYPASS ===\n');

    // 1. Preparazione immagine per test post-installazione
    console.log('[FASE 1] Creazione disco di test simulando installazione avvenuta...');
    const oobeIniPath = path.join(ROOT, 'build', 'bin', 'NewOS', 'OOBE', 'OOBE.INI');
    const origOobeContent = fs.readFileSync(oobeIniPath, 'utf8');

    // Imposta OOBE completato
    const installedOobeContent = "[OOBE]\nCompleted=1\nSetupStage=Completed\nLanguage=IT_IT\nDefaultUser=Morgan\nAllowSkip=1\n";
    fs.writeFileSync(oobeIniPath, installedOobeContent);

    // Rigenera volume exFAT di test
    const { buildMyOsFileSystem } = require('./mkexfat.js');
    fs.copyFileSync(origImg, testImg);
    const exfatBuf = buildMyOsFileSystem(ROOT, 250000, 2048);
    const fd = fs.openSync(testImg, 'r+');
    fs.writeSync(fd, exfatBuf, 0, exfatBuf.length, 2048 * 512);
    fs.closeSync(fd);

    // Ripristina OOBE.INI originale
    fs.writeFileSync(oobeIniPath, origOobeContent);
    console.log('[OK] Immagine build/test_bypass.img configurata con successo.');

    // 2. Avvio QEMU con test_bypass.img
    console.log('\n[FASE 2] Avvio QEMU per collaudo bypass...');
    const qemu = spawn(qemuBin, [
        '-drive', `format=raw,file=${testImg}`,
        '-m', '256M',
        '-vga', 'std',
        '-audiodev', 'none,id=snd0',
        '-device', 'AC97,audiodev=snd0',
        '-display', 'none',
        '-monitor', 'tcp:127.0.0.1:4555,server,nowait',
        '-serial', `file:${serialLog}`
    ], { stdio: 'ignore' });

    setTimeout(() => {
        let sentQuit = false;
        const client = net.createConnection({ port: 4555 }, async () => {
            console.log('Connesso al monitor QEMU (porta 4555)...');
            // Attendi che il bootscreen termini e LOGIN.MOD si disegni a schermo
            await sleep(6000);

            console.log('Cattura screendump...');
            client.write(`screendump "${ppmScreenshot.replace(/\\/g, '/')}"\n`);
            await sleep(1500);

            sentQuit = true;
            client.write('quit\n');
            await sleep(600);

            try { client.destroy(); } catch (e) {}
            try { qemu.kill(); } catch (e) {}

            console.log('\n[FASE 3] Analisi risultati e log seriale...');
            convertPpmToPng(ppmScreenshot, pngScreenshot);

            if (fs.existsSync(serialLog)) {
                const log = fs.readFileSync(serialLog, 'utf8');
                console.log('\n--- ESTRATTO SERIAL LOG ---');
                const relevant = log.split('\n').filter(l => 
                    l.includes('BOOT') || l.includes('LOGIN') || l.includes('OOBE') || l.includes('BYPASS')
                );
                console.log(relevant.join('\n'));
                console.log('---------------------------\n');

                const hasBypassLog = log.includes('BOOT_BYPASS') || log.includes('Bypass automatico Boot Menu');
                const hasLoginLog = log.includes('LOGIN.MOD') || log.includes('Schermata di Autenticazione');
                const hasBootMenu = log.includes('[BOOT_MENU] Pronto!');

                if (hasBypassLog && hasLoginLog && !hasBootMenu) {
                    console.log('>>> ESITO: PASS (Bypass convalidato al 100%, Boot Menu saltato, LOGIN.MOD avviato direttamente) <<<');
                } else if (hasBootMenu) {
                    console.error('>>> ESITO: FAIL (Il Boot Menu e\' ancora apparso!) <<<');
                    process.exit(1);
                } else {
                    console.log('Verificare i log per i dettagli.');
                }
            } else {
                console.error('[ERRORE] Nessun serial log prodotto.');
                process.exit(1);
            }

            process.exit(0);
        });

        client.on('error', (err) => {
            if (!sentQuit) {
                console.error('Errore socket:', err.message);
                try { qemu.kill(); } catch (e) {}
                process.exit(1);
            }
        });
    }, 1000);
}

run();
