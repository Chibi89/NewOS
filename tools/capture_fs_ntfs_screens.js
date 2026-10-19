const { spawn, execSync } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const isoPath = path.join(ROOT, 'NewOS_V1.iso');
const brainDir = 'C:\\Users\\morga\\.gemini\\antigravity-ide\\brain\\327df2b4-88f2-409e-bc1c-ac357035f353';

if (!fs.existsSync(brainDir)) {
    fs.mkdirSync(brainDir, { recursive: true });
}

function sleep(ms) {
    return new Promise(res => setTimeout(res, ms));
}

async function typeString(client, str) {
    for (const ch of str) {
        if (ch === ' ') {
            client.write('sendkey spc\n');
        } else if (ch === ':') {
            client.write('sendkey shift-semicolon\n');
        } else if (ch === '.') {
            client.write('sendkey dot\n');
        } else if (ch === '-') {
            client.write('sendkey minus\n');
        } else if (ch === '_') {
            client.write('sendkey shift-minus\n');
        } else if (ch === '\\') {
            client.write('sendkey backslash\n');
        } else {
            client.write(`sendkey ${ch.toLowerCase()}\n`);
        }
        await sleep(80);
    }
    client.write('sendkey ret\n');
    await sleep(700);
}

function convertPpmToPng(ppmFile, bmpFile, pngFile) {
    if (fs.existsSync(ppmFile)) {
        execSync(`node tools/ppm2bmp.js "${ppmFile}" "${bmpFile}"`, { stdio: 'ignore' });
        execSync(`powershell -ExecutionPolicy Bypass -File tools/bmp2png.ps1 "${bmpFile}" "${pngFile}"`, { stdio: 'ignore' });
        console.log(`[OK] Generato screenshot: ${pngFile}`);
    } else {
        console.error(`[ERR] File PPM non trovato: ${ppmFile}`);
    }
}

async function runFsNtfsCapture() {
    console.log('[FS NTFS CAPTURE] Avvio QEMU con NewOS_V1.iso...');

    const qemu = spawn(qemuBin, [
        '-cdrom', isoPath,
        '-m', '128M',
        '-vga', 'std',
        '-device', 'piix3-usb-uhci',
        '-device', 'qemu-xhci',
        '-netdev', 'user,id=net0',
        '-device', 'e1000,netdev=net0',
        '-audiodev', 'none,id=snd0',
        '-device', 'AC97,audiodev=snd0',
        '-display', 'none',
        '-monitor', 'tcp:127.0.0.1:4550,server,nowait',
        '-serial', `file:${path.join(ROOT, 'serial_fs_ntfs.log')}`
    ], { stdio: 'ignore' });

    setTimeout(() => {
        const client = net.createConnection({ port: 4550 }, async () => {
            console.log('[FS NTFS CAPTURE] Connesso al monitor QEMU...');
            await sleep(2000);

            // 1. Boot option 4: Prompt CLI
            console.log('[FS NTFS CAPTURE] Invio scelta boot 4 (Prompt CLI)...');
            client.write('sendkey 4\n');
            await sleep(3500);

            // -------------------------------------------------------------
            // SCREEN 1: VERIFICA UNITA E FILESYSTEM ESTESI (DRIVES)
            // -------------------------------------------------------------
            console.log('[FS NTFS CAPTURE] Test 1: DRIVES (Riconoscimento automatico C:, D:, R:, U:)...');
            await typeString(client, 'drives');
            await sleep(1000);

            const ppm1 = path.join(ROOT, 'screen_drives_ext.ppm');
            const bmp1 = path.join(ROOT, 'screen_drives_ext.bmp');
            const png1 = path.join(brainDir, 'screen_drives_extended.png');
            client.write(`screendump "${ppm1.replace(/\\/g, '/')}"\n`);
            await sleep(1200);

            // -------------------------------------------------------------
            // SCREEN 2: STATO DRIVER NTFS-3G E ARCHITETTURA
            // -------------------------------------------------------------
            console.log('[FS NTFS CAPTURE] Test 2: CLS e NTFS...');
            await typeString(client, 'cls');
            await sleep(400);
            await typeString(client, 'ntfs');
            await sleep(1000);

            const ppm2 = path.join(ROOT, 'screen_ntfs_info.ppm');
            const bmp2 = path.join(ROOT, 'screen_ntfs_info.bmp');
            const png2 = path.join(brainDir, 'screen_ntfs_info.png');
            client.write(`screendump "${ppm2.replace(/\\/g, '/')}"\n`);
            await sleep(1200);

            // -------------------------------------------------------------
            // SCREEN 3: COLLAUDO SCRITTURA NTFS-3G (R/W) E LETTURA (DIR & TYPE)
            // -------------------------------------------------------------
            console.log('[FS NTFS CAPTURE] Test 3: CLS, NTFS WRITE e DIR U:...');
            await typeString(client, 'cls');
            await sleep(400);
            await typeString(client, 'ntfs write');
            await sleep(1000);
            await typeString(client, 'dir u:');
            await sleep(1200);

            const ppm3 = path.join(ROOT, 'screen_ntfs_write.ppm');
            const bmp3 = path.join(ROOT, 'screen_ntfs_write.bmp');
            const png3 = path.join(brainDir, 'screen_ntfs_write.png');
            client.write(`screendump "${ppm3.replace(/\\/g, '/')}"\n`);
            await sleep(1200);

            // Chiusura monitor e QEMU
            client.end();
            qemu.kill();
            console.log('[FS NTFS CAPTURE] QEMU terminato. Conversione immagini...');

            // Conversione
            convertPpmToPng(ppm1, bmp1, png1);
            convertPpmToPng(ppm2, bmp2, png2);
            convertPpmToPng(ppm3, bmp3, png3);

            console.log('[FS NTFS CAPTURE] Tutte le schermate sono state generate con successo!');
            process.exit(0);
        });

        client.on('error', (err) => {
            console.error('[FS NTFS ERR] Connessione al monitor fallita:', err.message);
            try { qemu.kill(); } catch (e) {}
            process.exit(1);
        });
    }, 2000);
}

runFsNtfsCapture();
