const { spawn, execSync } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const imgPath = path.join(ROOT, 'myos.img');
const serialLog = path.join(ROOT, 'serial_mouse_cpl.log');
const ppmScreenshot = path.join(ROOT, 'screenshot_mouse_cpl.ppm');
const pngScreenshot = path.join(ROOT, 'screenshot_mouse_cpl.png');

if (fs.existsSync(serialLog)) fs.unlinkSync(serialLog);
if (fs.existsSync(ppmScreenshot)) fs.unlinkSync(ppmScreenshot);
if (fs.existsSync(pngScreenshot)) fs.unlinkSync(pngScreenshot);

console.log('[TEST MOUSE CPL] Avvio QEMU con myos.img...');
const qemu = spawn(qemuBin, [
    '-drive', `format=raw,file=${imgPath}`,
    '-m', '256M',
    '-vga', 'std',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4499,server,nowait',
    '-serial', `file:${serialLog}`
], { stdio: 'ignore' });

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

    // BMP Header
    bmpBuf.write('BM', 0);
    bmpBuf.writeUInt32LE(54 + pixelDataSize, 2);
    bmpBuf.writeUInt32LE(54, 10); // Data offset
    bmpBuf.writeUInt32LE(40, 14); // DIB header size
    bmpBuf.writeInt32LE(width, 18);
    bmpBuf.writeInt32LE(-height, 22); // Top-down
    bmpBuf.writeUInt16LE(1, 26); // Planes
    bmpBuf.writeUInt16LE(24, 28); // 24 bpp
    bmpBuf.writeUInt32LE(0, 30); // BI_RGB

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
    console.log(`[OK] BMP salvato: ${bmpFile}`);

    try {
        const psCmd = `Add-Type -AssemblyName System.Drawing; $bmp = [System.Drawing.Image]::FromFile('${bmpFile.replace(/\\/g, '\\\\')}'); $bmp.Save('${pngFile.replace(/\\/g, '\\\\')}', [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose();`;
        execSync(`powershell -NoProfile -Command "${psCmd}"`, { stdio: 'ignore' });
        console.log(`[OK] PNG generato con successo: ${pngFile}`);
        return true;
    } catch (e) {
        return false;
    }
}

setTimeout(() => {
    const client = net.createConnection({ port: 4499 }, async () => {
        console.log('Connesso al monitor QEMU...');
        await sleep(5500);

        // 1. Invio tasto '0' e Invio per lanciare direttamente Proprietà Mouse (MAIN.CPL)
        console.log('Invio scelta 0 (MAIN.CPL - Proprieta Mouse)...');
        client.write('sendkey 0\n');
        await sleep(500);
        client.write('sendkey ret\n');

        // Attendi che il desktop e la finestra CPL si aprano e disegnino tutti i controlli
        await sleep(5000);

        // Posiziona il cursore su un punto carino
        console.log('Posizionamento mouse per screendump...');
        client.write('mouse_move 20 20\n');
        await sleep(1000);

        // Cattura screenshot
        console.log('Cattura screendump CPL Mouse...');
        client.write(`screendump "${ppmScreenshot.replace(/\\/g, '/')}"\n`);
        await sleep(1500);

        client.write('quit\n');
        await sleep(500);

        try { client.destroy(); } catch (e) {}
        try { qemu.kill(); } catch (e) {}

        console.log('[TEST] QEMU terminato. Conversione screenshot...');
        convertPpmToPng(ppmScreenshot, pngScreenshot);

        // Copia lo screenshot anche nella directory degli artefatti
        const artifactDir = 'C:\\Users\\morga\\.gemini\\antigravity-ide\\brain\\6d1cba90-12d0-41c5-9458-9cfe376fafc5';
        const artifactPng = path.join(artifactDir, 'screenshot_mouse_cpl.png');
        if (fs.existsSync(pngScreenshot)) {
            fs.copyFileSync(pngScreenshot, artifactPng);
            console.log(`[OK] Screenshot copiato negli artefatti: ${artifactPng}`);
        }

        // Analisi serial log
        if (fs.existsSync(serialLog)) {
            const logs = fs.readFileSync(serialLog, 'utf8');
            console.log('\n--- SERIAL LOG (Estratto CPL Mouse) ---');
            const lines = logs.split('\n').filter(l => l.includes('CURSOR') || l.includes('MOUSE') || l.includes('SCHEMA'));
            console.log(lines.join('\n'));
            console.log('---------------------------------------\n');
        }

        process.exit(0);
    });

    client.on('error', (err) => {
        console.log('QEMU socket event:', err.message);
    });
}, 1000);
