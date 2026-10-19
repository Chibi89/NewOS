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

async function runAutoplayCapture() {
    console.log('[AUTOPLAY CAPTURE] Avvio QEMU con NewOS_V1.iso...');

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
        '-monitor', 'tcp:127.0.0.1:4545,server,nowait',
        '-serial', `file:${path.join(ROOT, 'serial_autoplay.log')}`
    ], { stdio: 'ignore' });

    setTimeout(() => {
        const client = net.createConnection({ port: 4545 }, async () => {
            console.log('[AUTOPLAY CAPTURE] Connesso al monitor QEMU...');
            await sleep(2000);

            // 1. Boot option 4: Prompt CLI
            console.log('[AUTOPLAY CAPTURE] Invio scelta boot 4 (Prompt CLI)...');
            client.write('sendkey 4\n');
            await sleep(3500);

            // -------------------------------------------------------------
            // SCREEN 1: CASO 1 - MASS STORAGE
            // -------------------------------------------------------------
            console.log('[AUTOPLAY CAPTURE] Apertura AutoPlay Caso 1: MassStorage...');
            await typeString(client, 'autoplay 1');
            await sleep(1500);

            const ppm1 = path.join(ROOT, 'screen_ap_msc.ppm');
            const bmp1 = path.join(ROOT, 'screen_ap_msc.bmp');
            const png1 = path.join(brainDir, 'screen_autoplay_msc.png');
            client.write(`screendump "${ppm1.replace(/\\/g, '/')}"\n`);
            await sleep(1200);

            // -------------------------------------------------------------
            // SCREEN 2: CASO 2 - MULTIMEDIA
            // -------------------------------------------------------------
            console.log('[AUTOPLAY CAPTURE] Passaggio a Caso 2: Multimedia Player (Key 2)...');
            client.write('sendkey 2\n');
            await sleep(1500);

            const ppm2 = path.join(ROOT, 'screen_ap_media.ppm');
            const bmp2 = path.join(ROOT, 'screen_ap_media.bmp');
            const png2 = path.join(brainDir, 'screen_autoplay_multimedia.png');
            client.write(`screendump "${ppm2.replace(/\\/g, '/')}"\n`);
            await sleep(1200);

            // -------------------------------------------------------------
            // SCREEN 3: CASO 3 - PHOTOMANAGER
            // -------------------------------------------------------------
            console.log('[AUTOPLAY CAPTURE] Passaggio a Caso 3: PhotoManager Galleria (Key 3)...');
            client.write('sendkey 3\n');
            await sleep(1500);

            const ppm3 = path.join(ROOT, 'screen_ap_photo.ppm');
            const bmp3 = path.join(ROOT, 'screen_ap_photo.bmp');
            const png3 = path.join(brainDir, 'screen_autoplay_photomanager.png');
            client.write(`screendump "${ppm3.replace(/\\/g, '/')}"\n`);
            await sleep(1200);

            // -------------------------------------------------------------
            // SCREEN 4: CASO 4 - STANDARD
            // -------------------------------------------------------------
            console.log('[AUTOPLAY CAPTURE] Passaggio a Caso 4: Standard Dialog (Key 4)...');
            client.write('sendkey 4\n');
            await sleep(1500);

            const ppm4 = path.join(ROOT, 'screen_ap_std.ppm');
            const bmp4 = path.join(ROOT, 'screen_ap_std.bmp');
            const png4 = path.join(brainDir, 'screen_autoplay_standard.png');
            client.write(`screendump "${ppm4.replace(/\\/g, '/')}"\n`);
            await sleep(1200);

            // Chiusura monitor e QEMU
            client.end();
            qemu.kill();
            console.log('[AUTOPLAY CAPTURE] QEMU terminato. Conversione immagini...');

            // Conversione
            convertPpmToPng(ppm1, bmp1, png1);
            convertPpmToPng(ppm2, bmp2, png2);
            convertPpmToPng(ppm3, bmp3, png3);
            convertPpmToPng(ppm4, bmp4, png4);

            console.log('[AUTOPLAY CAPTURE] Tutte le schermate AutoPlay sono state generate!');
            process.exit(0);
        });

        client.on('error', (err) => {
            console.error('[AUTOPLAY ERR] Connessione al monitor fallita:', err.message);
            try { qemu.kill(); } catch (e) {}
            process.exit(1);
        });
    }, 2000);
}

runAutoplayCapture();
