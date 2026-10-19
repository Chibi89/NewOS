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

async function runCapture() {
    console.log('[CAPTURE] Avvio QEMU con NewOS_V1.iso...');

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
        '-monitor', 'tcp:127.0.0.1:4540,server,nowait',
        '-serial', `file:${path.join(ROOT, 'serial_capture.log')}`
    ], { stdio: 'ignore' });

    setTimeout(() => {
        const client = net.createConnection({ port: 4540 }, async () => {
            console.log('[CAPTURE] Connesso al monitor QEMU...');
            await sleep(2000);

            // 1. Boot option 4: Prompt dei Comandi CLI
            console.log('[CAPTURE] Invio scelta boot 4 (Prompt CLI)...');
            client.write('sendkey 4\n');
            await sleep(3500);

            // -------------------------------------------------------------
            // SCREEN 1: VERIFICA FILESYSTEM ESTESO (DRIVES & DIR R:)
            // -------------------------------------------------------------
            console.log('[CAPTURE] Test 1: DRIVES e DIR R:...');
            await typeString(client, 'drives');
            await sleep(600);
            await typeString(client, 'dir r:');
            await sleep(1000);

            const ppm1 = path.join(ROOT, 'screen_fs.ppm');
            const bmp1 = path.join(ROOT, 'screen_fs.bmp');
            const png1 = path.join(brainDir, 'screen_fs_verification.png');
            client.write(`screendump "${ppm1.replace(/\\/g, '/')}"\n`);
            await sleep(1200);

            // -------------------------------------------------------------
            // SCREEN 2: VERIFICA WI-FI 7 (5.0 GB/s) E SCANSIONE RETI
            // -------------------------------------------------------------
            console.log('[CAPTURE] Test 2: CLS, WIFI e WIFI SCAN...');
            await typeString(client, 'cls');
            await sleep(400);
            await typeString(client, 'wifi');
            await sleep(600);
            await typeString(client, 'wifi scan');
            await sleep(1000);

            const ppm2 = path.join(ROOT, 'screen_wifi_5gb.ppm');
            const bmp2 = path.join(ROOT, 'screen_wifi_5gb.bmp');
            const png2 = path.join(brainDir, 'screen_wifi_5gb.png');
            client.write(`screendump "${ppm2.replace(/\\/g, '/')}"\n`);
            await sleep(1200);

            // -------------------------------------------------------------
            // SCREEN 3: VERIFICA VINCOLO HARDWARE FISICO (CAP 1000 MB/s = 1 GB/s)
            // -------------------------------------------------------------
            console.log('[CAPTURE] Test 3: CLS, WIFI CAP 1000 e WIFI...');
            await typeString(client, 'cls');
            await sleep(400);
            await typeString(client, 'wifi cap 1000');
            await sleep(600);
            await typeString(client, 'wifi');
            await sleep(1000);

            const ppm3 = path.join(ROOT, 'screen_wifi_cap.ppm');
            const bmp3 = path.join(ROOT, 'screen_wifi_cap.bmp');
            const png3 = path.join(brainDir, 'screen_wifi_cap.png');
            client.write(`screendump "${ppm3.replace(/\\/g, '/')}"\n`);
            await sleep(1200);

            // -------------------------------------------------------------
            // SCREEN 4: VERIFICA MODULO DRIVER CARICATO (DRIVERS / WIFI7.SYS)
            // -------------------------------------------------------------
            console.log('[CAPTURE] Test 4: CLS e DRIVERS...');
            await typeString(client, 'cls');
            await sleep(400);
            await typeString(client, 'drivers');
            await sleep(1000);

            const ppm4 = path.join(ROOT, 'screen_drivers.ppm');
            const bmp4 = path.join(ROOT, 'screen_drivers.bmp');
            const png4 = path.join(brainDir, 'screen_drivers.png');
            client.write(`screendump "${ppm4.replace(/\\/g, '/')}"\n`);
            await sleep(1200);

            // -------------------------------------------------------------
            // SCREEN 5: VERIFICA PERIFERICHE USB E HOST CONTROLLERS (UHCI/OHCI/xHCI)
            // -------------------------------------------------------------
            console.log('[CAPTURE] Test 5: CLS e USB...');
            await typeString(client, 'cls');
            await sleep(400);
            await typeString(client, 'usb');
            await sleep(1000);

            const ppm5 = path.join(ROOT, 'screen_usb.ppm');
            const bmp5 = path.join(ROOT, 'screen_usb.bmp');
            const png5 = path.join(brainDir, 'screen_usb_devices.png');
            client.write(`screendump "${ppm5.replace(/\\/g, '/')}"\n`);
            await sleep(1200);

            // -------------------------------------------------------------
            // SCREEN 6: VERIFICA AGGANCIO BUS HW WI-FI
            // -------------------------------------------------------------
            console.log('[CAPTURE] Test 6: CLS e WIFI...');
            await typeString(client, 'cls');
            await sleep(400);
            await typeString(client, 'wifi');
            await sleep(1000);

            const ppm6 = path.join(ROOT, 'screen_wifi_bus.ppm');
            const bmp6 = path.join(ROOT, 'screen_wifi_bus.bmp');
            const png6 = path.join(brainDir, 'screen_wifi_bus.png');
            client.write(`screendump "${ppm6.replace(/\\/g, '/')}"\n`);
            await sleep(1200);

            // Chiusura monitor e QEMU
            client.end();
            qemu.kill();
            console.log('[CAPTURE] QEMU terminato. Conversione immagini...');

            // Conversione
            convertPpmToPng(ppm1, bmp1, png1);
            convertPpmToPng(ppm2, bmp2, png2);
            convertPpmToPng(ppm3, bmp3, png3);
            convertPpmToPng(ppm4, bmp4, png4);
            convertPpmToPng(ppm5, bmp5, png5);
            convertPpmToPng(ppm6, bmp6, png6);

            console.log('[CAPTURE] Operazione completata con successo!');
            process.exit(0);
        });

        client.on('error', (err) => {
            console.error('[CAPTURE ERR] Connessione al monitor fallita:', err.message);
            try { qemu.kill(); } catch (e) {}
            process.exit(1);
        });
    }, 2000);
}

runCapture();
