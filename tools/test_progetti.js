const { spawn } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const isoPath = path.join(ROOT, 'NewOS_V1.iso');
const brainDir = 'C:\\Users\\morga\\.gemini\\antigravity-ide\\brain\\201a47cf-7636-4592-93f8-cb22ec54e1bd';

const ppm6 = path.join(ROOT, 'subnav_6.ppm');
const bmp6 = path.join(ROOT, 'subnav_6.bmp');
const png6 = path.join(brainDir, 'screenshot_subfolder_progetti.png');

const qemu = spawn(qemuBin, [
    '-cdrom', isoPath,
    '-m', '128M',
    '-vga', 'std',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4517,server,nowait'
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
    const client = net.createConnection({ port: 4517 }, async () => {
        await sleep(1500);
        client.write('sendkey 4\n');
        await sleep(2500);

        for (const c of 'explorer') { client.write(`sendkey ${c}\n`); await sleep(60); }
        client.write('sendkey ret\n');
        await sleep(2000);

        // Apri C:\
        client.write('sendkey ret\n');
        await sleep(1200);

        // Tab x 3 fino a USER
        client.write('sendkey tab\n'); await sleep(200);
        client.write('sendkey tab\n'); await sleep(200);
        client.write('sendkey tab\n'); await sleep(200);
        client.write('sendkey ret\n'); // USER
        await sleep(1200);

        client.write('sendkey ret\n'); // MORGAN
        await sleep(1200);

        client.write('sendkey ret\n'); // DESKTOP
        await sleep(1200);

        client.write('sendkey ret\n'); // PROGETTI (4° livello!)
        await sleep(1500);

        // Cattura screenshot
        client.write(`screendump "${ppm6.replace(/\\/g, '/')}"\n`);
        await sleep(1200);

        client.write('quit\n');
        await sleep(800);
        qemu.kill();

        if (fs.existsSync(ppm6)) {
            convertPpmToBmp(ppm6, bmp6);
            const { execSync } = require('child_process');
            execSync(`powershell -Command "Add-Type -AssemblyName System.Drawing; [System.Drawing.Bitmap]::FromFile('${bmp6}').Save('${png6}', [System.Drawing.Imaging.ImageFormat]::Png)"`);
            console.log(`[PNG] Salvato: ${png6}`);
        }
        process.exit(0);
    });
}, 2000);
