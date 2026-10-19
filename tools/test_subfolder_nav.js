const { spawn } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const isoPath = path.join(ROOT, 'NewOS_V1.iso');
const brainDir = 'C:\\Users\\morga\\.gemini\\antigravity-ide\\brain\\201a47cf-7636-4592-93f8-cb22ec54e1bd';

const ppm1 = path.join(ROOT, 'subnav_1.ppm');
const bmp1 = path.join(ROOT, 'subnav_1.bmp');
const png1 = path.join(brainDir, 'screenshot_subfolder_system.png');

const ppm2 = path.join(ROOT, 'subnav_2.ppm');
const bmp2 = path.join(ROOT, 'subnav_2.bmp');
const png2 = path.join(brainDir, 'screenshot_subfolder_drivers.png');

const ppm3 = path.join(ROOT, 'subnav_3.ppm');
const bmp3 = path.join(ROOT, 'subnav_3.bmp');
const png3 = path.join(brainDir, 'screenshot_subfolder_drivers_net.png');

const ppm4 = path.join(ROOT, 'subnav_4.ppm');
const bmp4 = path.join(ROOT, 'subnav_4.bmp');
const png4 = path.join(brainDir, 'screenshot_subfolder_wallpapers.png');

const ppm5 = path.join(ROOT, 'subnav_5.ppm');
const bmp5 = path.join(ROOT, 'subnav_5.bmp');
const png5 = path.join(brainDir, 'screenshot_subfolder_user_desktop.png');

console.log('[TEST SUBFOLDERS] Avvio QEMU per testare la navigazione profonda di tutte le sottocartelle...');

const qemu = spawn(qemuBin, [
    '-cdrom', isoPath,
    '-m', '128M',
    '-vga', 'std',
    '-netdev', 'user,id=net0',
    '-device', 'e1000,netdev=net0',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4516,server,nowait',
    '-serial', `file:${path.join(ROOT, 'serial_subfolder_test.log')}`
], { stdio: 'ignore' });

function sleep(ms) {
    return new Promise(res => setTimeout(res, ms));
}

function convertPpmToBmp(ppmPath, bmpPath) {
    if (!fs.existsSync(ppmPath)) return;
    const data = fs.readFileSync(ppmPath);
    let offset = 0;
    while (data[offset] !== 0x0A && offset < data.length) offset++;
    offset++;
    while (data[offset] === 0x23) {
        while (data[offset] !== 0x0A && offset < data.length) offset++;
        offset++;
    }
    let dimStr = '';
    while (data[offset] !== 0x0A && offset < data.length) {
        dimStr += String.fromCharCode(data[offset++]);
    }
    offset++;
    const [w, h] = dimStr.trim().split(/\s+/).map(Number);
    while (data[offset] !== 0x0A && offset < data.length) offset++;
    offset++;

    const rowSize = Math.floor((24 * w + 31) / 32) * 4;
    const pixelArraySize = rowSize * h;
    const fileSize = 54 + pixelArraySize;
    const bmp = Buffer.alloc(fileSize, 0);

    bmp.write('BM', 0, 2, 'ascii');
    bmp.writeUInt32LE(fileSize, 2);
    bmp.writeUInt32LE(54, 10);
    bmp.writeUInt32LE(40, 14);
    bmp.writeInt32LE(w, 18);
    bmp.writeInt32LE(h, 22);
    bmp.writeUInt16LE(1, 26);
    bmp.writeUInt16LE(24, 28);
    bmp.writeUInt32LE(0, 30);
    bmp.writeUInt32LE(pixelArraySize, 34);

    for (let y = 0; y < h; y++) {
        const srcRow = (h - 1 - y) * w * 3 + offset;
        const dstRow = 54 + y * rowSize;
        for (let x = 0; x < w; x++) {
            const r = data[srcRow + x * 3];
            const g = data[srcRow + x * 3 + 1];
            const b = data[srcRow + x * 3 + 2];
            bmp[dstRow + x * 3] = b;
            bmp[dstRow + x * 3 + 1] = g;
            bmp[dstRow + x * 3 + 2] = r;
        }
    }
    fs.writeFileSync(bmpPath, bmp);
}

setTimeout(() => {
    const client = net.createConnection({ port: 4516 }, async () => {
        console.log('Connesso al monitor QEMU...');
        await sleep(1500);

        // 1. Boot option 4: Prompt dei Comandi
        console.log('Selezione boot option 4 (Prompt dei Comandi)...');
        client.write('sendkey 4\n');
        await sleep(2500);

        // 2. Apri explorer su C:\
        console.log('Digitazione "explorer"...');
        const cmd = 'explorer';
        for (const c of cmd) {
            client.write(`sendkey ${c}\n`);
            await sleep(60);
        }
        client.write('sendkey ret\n');
        await sleep(2000);

        // Apri Disco C: (primo elemento in Risorse PC)
        console.log('Apertura Disco locale (C:)...');
        client.write('sendkey ret\n');
        await sleep(1500);

        // In C:\, il primo elemento è SYSTEM. Entriamo in SYSTEM!
        console.log('Navigazione in SYSTEM...');
        client.write('sendkey ret\n');
        await sleep(1500);

        // Cattura Screenshot 1: SYSTEM
        console.log('Cattura Screenshot 1: C:\\SYSTEM...');
        client.write(`screendump "${ppm1.replace(/\\/g, '/')}"\n`);
        await sleep(1200);

        // In SYSTEM, il primo elemento è DRIVERS. Entriamo in DRIVERS!
        console.log('Navigazione in sottocartella DRIVERS...');
        client.write('sendkey ret\n');
        await sleep(1500);

        // Cattura Screenshot 2: SYSTEM\DRIVERS
        console.log('Cattura Screenshot 2: C:\\SYSTEM\\DRIVERS...');
        client.write(`screendump "${ppm2.replace(/\\/g, '/')}"\n`);
        await sleep(1200);

        // In DRIVERS, il primo elemento è NET. Entriamo in NET (sottocartella livello 3)!
        console.log('Navigazione in sottocartella di 3° livello NET...');
        client.write('sendkey ret\n');
        await sleep(1500);

        // Cattura Screenshot 3: SYSTEM\DRIVERS\NET
        console.log('Cattura Screenshot 3: C:\\SYSTEM\\DRIVERS\\NET...');
        client.write(`screendump "${ppm3.replace(/\\/g, '/')}"\n`);
        await sleep(1200);

        // Torniamo indietro con Backspace: NET -> DRIVERS -> SYSTEM
        console.log('Ritorno a DRIVERS...');
        client.write('sendkey backspace\n');
        await sleep(1000);

        console.log('Ritorno a SYSTEM...');
        client.write('sendkey backspace\n');
        await sleep(1000);

        // In SYSTEM: Tab per scorrere da DRIVERS a DEV, poi a WALLPAPERS
        console.log('Scorrimento con Tab fino a WALLPAPERS...');
        client.write('sendkey tab\n');
        await sleep(300);
        client.write('sendkey tab\n');
        await sleep(300);

        // Entriamo in WALLPAPERS!
        console.log('Navigazione in sottocartella WALLPAPERS...');
        client.write('sendkey ret\n');
        await sleep(1500);

        // Cattura Screenshot 4: SYSTEM\WALLPAPERS
        console.log('Cattura Screenshot 4: C:\\SYSTEM\\WALLPAPERS...');
        client.write(`screendump "${ppm4.replace(/\\/g, '/')}"\n`);
        await sleep(1200);

        // Torniamo a C:\ (Backspace due volte: WALLPAPERS -> SYSTEM -> C:\)
        console.log('Ritorno a SYSTEM e poi a C:\\...');
        client.write('sendkey backspace\n');
        await sleep(1000);
        client.write('sendkey backspace\n');
        await sleep(1000);

        // In C:\: Tab per scorrere: SYSTEM (0), DOCS (1), APPS (2), USER (3)
        console.log('Scorrimento su USER (Tab x 3)...');
        client.write('sendkey tab\n');
        await sleep(250);
        client.write('sendkey tab\n');
        await sleep(250);
        client.write('sendkey tab\n');
        await sleep(250);

        // Entriamo in USER!
        console.log('Navigazione in USER...');
        client.write('sendkey ret\n');
        await sleep(1200);

        // In USER, il primo elemento è MORGAN. Entriamo!
        console.log('Navigazione in MORGAN...');
        client.write('sendkey ret\n');
        await sleep(1200);

        // In MORGAN, il primo elemento è DESKTOP. Entriamo!
        console.log('Navigazione in DESKTOP...');
        client.write('sendkey ret\n');
        await sleep(1500);

        // Cattura Screenshot 5: USER\MORGAN\DESKTOP
        console.log('Cattura Screenshot 5: C:\\USER\\MORGAN\\DESKTOP...');
        client.write(`screendump "${ppm5.replace(/\\/g, '/')}"\n`);
        await sleep(1200);

        console.log('Test completato! Chiusura QEMU...');
        client.write('quit\n');
        await sleep(800);
        qemu.kill();

        // Converti screenshots
        try {
            const { execSync } = require('child_process');
            const items = [
                { ppm: ppm1, bmp: bmp1, png: png1 },
                { ppm: ppm2, bmp: bmp2, png: png2 },
                { ppm: ppm3, bmp: bmp3, png: png3 },
                { ppm: ppm4, bmp: bmp4, png: png4 },
                { ppm: ppm5, bmp: bmp5, png: png5 },
            ];

            for (const it of items) {
                if (fs.existsSync(it.ppm)) {
                    convertPpmToBmp(it.ppm, it.bmp);
                    execSync(`powershell -Command "Add-Type -AssemblyName System.Drawing; [System.Drawing.Bitmap]::FromFile('${it.bmp}').Save('${it.png}', [System.Drawing.Imaging.ImageFormat]::Png)"`);
                    console.log(`[PNG] Salvato: ${it.png}`);
                }
            }
        } catch(err) {
            console.error('Errore conversione:', err.message);
        }

        process.exit(0);
    });
}, 2000);
