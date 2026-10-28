const { spawn, execSync } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const imgPath = path.join(ROOT, 'myos.img');
const serialLog = path.join(ROOT, 'serial_cursor_test.log');
const ppmScreenshot = path.join(ROOT, 'screenshot_cursor.ppm');
const pngScreenshot = path.join(ROOT, 'screenshot_cursor.png');

if (fs.existsSync(serialLog)) fs.unlinkSync(serialLog);
if (fs.existsSync(ppmScreenshot)) fs.unlinkSync(ppmScreenshot);
if (fs.existsSync(pngScreenshot)) fs.unlinkSync(pngScreenshot);

console.log('[TEST CURSORS] Avvio QEMU con myos.img...');
const qemu = spawn(qemuBin, [
    '-drive', `format=raw,file=${imgPath}`,
    '-m', '256M',
    '-vga', 'std',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4488,server,nowait',
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

    // Crea BMP uncompressed per facilitare visualizzazione o conversione
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

    // Tentativo conversione PNG con powershell o tool
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
    const client = net.createConnection({ port: 4488 }, async () => {
        console.log('Connesso al monitor QEMU...');
        await sleep(5500);

        // 1. Invio tasto '1' e Invio per accedere al Desktop
        console.log('Invio scelta 1 (Desktop)...');
        client.write('sendkey 1\n');
        await sleep(500);
        client.write('sendkey ret\n');

        // Attendi che il desktop si avvii e carichi temi e cursori
        await sleep(5000);

        // Muovi il mouse per posizionare il cursore in un punto ben visibile sul desktop (es. centro schermo)
        console.log('Posizionamento mouse sul desktop...');
        client.write('mouse_move -100 -50\n');
        await sleep(1000);

        let quitting = false;
        // Cattura screenshot
        console.log('Cattura screendump...');
        client.write(`screendump "${ppmScreenshot.replace(/\\/g, '/')}"\n`);
        await sleep(1500);

        quitting = true;
        client.write('quit\n');
        await sleep(500);

        try { client.destroy(); } catch (e) {}
        try { qemu.kill(); } catch (e) {}

        console.log('[TEST] QEMU terminato. Conversione screenshot...');
        convertPpmToPng(ppmScreenshot, pngScreenshot);

        // Copia lo screenshot anche nella directory degli artefatti
        const artifactDir = 'C:\\Users\\morga\\.gemini\\antigravity-ide\\brain\\6d1cba90-12d0-41c5-9458-9cfe376fafc5';
        const artifactPng = path.join(artifactDir, 'screenshot_cursor.png');
        if (fs.existsSync(pngScreenshot)) {
            fs.copyFileSync(pngScreenshot, artifactPng);
            console.log(`[OK] Screenshot copiato negli artefatti: ${artifactPng}`);
        }

        // Analisi serial log
        if (fs.existsSync(serialLog)) {
            const logs = fs.readFileSync(serialLog, 'utf8');
            console.log('\n--- SERIAL LOG (Estratto Cursori) ---');
            const lines = logs.split('\n').filter(l => l.includes('CURSOR') || l.includes('SCHEMA') || l.includes('THEME'));
            console.log(lines.join('\n'));
            console.log('-------------------------------------\n');
        }

        process.exit(0);
    });

    client.on('error', (err) => {
        // Se stiamo uscendo, l'ECONNRESET e' fisiologico
        console.log('QEMU socket event:', err.message);
    });
}, 1000);
