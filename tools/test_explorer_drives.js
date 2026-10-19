const { spawn } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const isoPath = path.join(ROOT, 'NewOS_V1.iso');
const brainDir = 'C:\\Users\\morga\\.gemini\\antigravity-ide\\brain\\201a47cf-7636-4592-93f8-cb22ec54e1bd';

const ppmExp1 = path.join(ROOT, 'test_exp1.ppm');
const bmpExp1 = path.join(ROOT, 'test_exp1.bmp');
const pngExp1 = path.join(brainDir, 'screenshot_explorer_this_pc.png');

const ppmExp2 = path.join(ROOT, 'test_exp2.ppm');
const bmpExp2 = path.join(ROOT, 'test_exp2.bmp');
const pngExp2 = path.join(brainDir, 'screenshot_explorer_drive_c.png');

const ppmExp3 = path.join(ROOT, 'test_exp3.ppm');
const bmpExp3 = path.join(ROOT, 'test_exp3.bmp');
const pngExp3 = path.join(brainDir, 'screenshot_explorer_system_folder.png');

const ppmExp4 = path.join(ROOT, 'test_exp4.ppm');
const bmpExp4 = path.join(ROOT, 'test_exp4.bmp');
const pngExp4 = path.join(brainDir, 'screenshot_explorer_drive_d.png');

console.log('[TEST EXPLORER] Avvio QEMU per testare la navigazione delle memorie e unita disco...');

const qemu = spawn(qemuBin, [
    '-cdrom', isoPath,
    '-m', '128M',
    '-vga', 'std',
    '-netdev', 'user,id=net0',
    '-device', 'e1000,netdev=net0',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4513,server,nowait',
    '-serial', `file:${path.join(ROOT, 'serial_explorer_test.log')}`
], { stdio: 'ignore' });

function sleep(ms) {
    return new Promise(res => setTimeout(res, ms));
}

function convertPpmToBmp(ppmPath, bmpPath) {
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
    console.log(`[CONVERT] Convertito PPM -> BMP: ${bmpPath} (${w}x${h})`);
}

setTimeout(() => {
    const client = net.createConnection({ port: 4513 }, async () => {
        console.log('Connesso al monitor QEMU...');
        await sleep(1500);

        // 1. Boot option 4: Prompt dei Comandi
        console.log('Selezione boot option 4 (Prompt dei Comandi)...');
        client.write('sendkey 4\n');
        await sleep(2500);

        // 2. Digita comando "explorer"
        console.log('Digitazione "explorer"...');
        const cmd = 'explorer';
        for (const c of cmd) {
            client.write(`sendkey ${c}\n`);
            await sleep(80);
        }
        client.write('sendkey ret\n');
        await sleep(2000);

        // 3. Screenshot 1: Risorse del computer (This PC) con C:, A:, D:, Pannello
        console.log('Cattura Screenshot 1: Risorse del computer (This PC)...');
        client.write(`screendump "${ppmExp1.replace(/\\/g, '/')}"\n`);
        await sleep(1200);

        // 4. Premiamo Enter (sendkey ret) sull'elemento selezionato (Disco locale C:)!
        console.log('Navigazione in Disco locale (C:) tramite Enter...');
        client.write('sendkey ret\n');
        await sleep(1500);

        // 5. Screenshot 2: Navigazione dentro C:\ (SYSTEM, DOCS, APPS, USER, SOURCES)
        console.log('Cattura Screenshot 2: Cartella radice C:\\...');
        client.write(`screendump "${ppmExp2.replace(/\\/g, '/')}"\n`);
        await sleep(1200);

        // 6. Premiamo Enter per entrare nella cartella selezionata (SYSTEM)
        console.log('Navigazione dentro la prima sottocartella (C:\\SYSTEM) tramite Enter...');
        client.write('sendkey ret\n');
        await sleep(1500);

        // 7. Screenshot 3: Contenuto di C:\SYSTEM
        console.log('Cattura Screenshot 3: Cartella C:\\SYSTEM...');
        client.write(`screendump "${ppmExp3.replace(/\\/g, '/')}"\n`);
        await sleep(1200);

        // 8. Premiamo Backspace per tornare a C:\, poi Backspace per tornare a Risorse del computer
        console.log('Ritorno a C:\\ tramite Backspace...');
        client.write('sendkey backspace\n');
        await sleep(1000);
        console.log('Ritorno a Risorse del computer tramite Backspace...');
        client.write('sendkey backspace\n');
        await sleep(1000);

        // 9. Selezioniamo Unità CD-ROM (D:) premendo Tab due volte e poi Enter
        console.log('Spostamento su Unita CD-ROM (D:) tramite Tab...');
        client.write('sendkey tab\n');
        await sleep(300);
        client.write('sendkey tab\n');
        await sleep(300);
        console.log('Apertura Unita CD-ROM (D:) tramite Enter...');
        client.write('sendkey ret\n');
        await sleep(1500);

        // 10. Screenshot 4: Contenuto di D:\ (CD-ROM)
        console.log('Cattura Screenshot 4: Unita CD-ROM D:\\...');
        client.write(`screendump "${ppmExp4.replace(/\\/g, '/')}"\n`);
        await sleep(1500);

        console.log('Test completato! Chiusura QEMU...');
        client.write('quit\n');
        await sleep(800);
        qemu.kill();

        // Converti screenshots
        try {
            if (fs.existsSync(ppmExp1)) {
                convertPpmToBmp(ppmExp1, bmpExp1);
                const { execSync } = require('child_process');
                execSync(`powershell -Command "Add-Type -AssemblyName System.Drawing; [System.Drawing.Bitmap]::FromFile('${bmpExp1}').Save('${pngExp1}', [System.Drawing.Imaging.ImageFormat]::Png)"`);
                console.log(`[PNG] Salvato: ${pngExp1}`);
            }
            if (fs.existsSync(ppmExp2)) {
                convertPpmToBmp(ppmExp2, bmpExp2);
                const { execSync } = require('child_process');
                execSync(`powershell -Command "Add-Type -AssemblyName System.Drawing; [System.Drawing.Bitmap]::FromFile('${bmpExp2}').Save('${pngExp2}', [System.Drawing.Imaging.ImageFormat]::Png)"`);
                console.log(`[PNG] Salvato: ${pngExp2}`);
            }
            if (fs.existsSync(ppmExp3)) {
                convertPpmToBmp(ppmExp3, bmpExp3);
                const { execSync } = require('child_process');
                execSync(`powershell -Command "Add-Type -AssemblyName System.Drawing; [System.Drawing.Bitmap]::FromFile('${bmpExp3}').Save('${pngExp3}', [System.Drawing.Imaging.ImageFormat]::Png)"`);
                console.log(`[PNG] Salvato: ${pngExp3}`);
            }
            if (fs.existsSync(ppmExp4)) {
                convertPpmToBmp(ppmExp4, bmpExp4);
                const { execSync } = require('child_process');
                execSync(`powershell -Command "Add-Type -AssemblyName System.Drawing; [System.Drawing.Bitmap]::FromFile('${bmpExp4}').Save('${pngExp4}', [System.Drawing.Imaging.ImageFormat]::Png)"`);
                console.log(`[PNG] Salvato: ${pngExp4}`);
            }
        } catch(err) {
            console.error('Errore conversione:', err.message);
        }

        process.exit(0);
    });
}, 2000);
