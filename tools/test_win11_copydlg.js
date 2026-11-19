/**
 * tools/test_win11_copydlg.js
 * Collaudo visivo in QEMU della Finestra Copia/Spostamento File stile Windows 11 Fluent
 */

const { spawn } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');
const zlib = require('zlib');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const origImg = path.join(ROOT, 'build', 'NewOS_V1.img');
const testImg = path.join(ROOT, 'build', 'test_win11_copy.img');
const serialLog = path.join(ROOT, 'serial_win11_copy.log');

function sleep(ms) {
    return new Promise(res => setTimeout(res, ms));
}

async function waitForLog(logFile, marker, timeoutMs = 15000) {
    const start = Date.now();
    while (Date.now() - start < timeoutMs) {
        if (fs.existsSync(logFile)) {
            const content = fs.readFileSync(logFile, 'utf8');
            if (content.includes(marker)) return true;
        }
        await sleep(200);
    }
    return false;
}

function convertPpmToPng(ppmFile, pngFile) {
    if (!fs.existsSync(ppmFile)) return false;
    const buf = fs.readFileSync(ppmFile);
    let offset = 0;
    while (buf[offset] !== 0x0A && buf[offset] !== 0x20) offset++;
    offset++;
    while (buf[offset] === 0x23) {
        while (buf[offset] !== 0x0A) offset++;
        offset++;
    }
    let wStr = '';
    while (buf[offset] !== 0x20 && buf[offset] !== 0x0A) wStr += String.fromCharCode(buf[offset++]);
    offset++;
    let hStr = '';
    while (buf[offset] !== 0x20 && buf[offset] !== 0x0A) hStr += String.fromCharCode(buf[offset++]);
    offset++;
    while (buf[offset] !== 0x0A) offset++;
    offset++;

    const width = parseInt(wStr, 10);
    const height = parseInt(hStr, 10);
    const rgbData = buf.subarray(offset);

    const rawScanlines = Buffer.alloc(height * (1 + width * 3));
    for (let y = 0; y < height; y++) {
        const rowStart = y * (1 + width * 3);
        rawScanlines[rowStart] = 0;
        const srcStart = y * width * 3;
        rgbData.copy(rawScanlines, rowStart + 1, srcStart, srcStart + width * 3);
    }

    const compressed = zlib.deflateSync(rawScanlines);

    function crc32(b) {
        let c = 0xFFFFFFFF;
        for (let i = 0; i < b.length; i++) {
            c ^= b[i];
            for (let k = 0; k < 8; k++) c = (c >>> 1) ^ ((c & 1) ? 0xEDB88320 : 0);
        }
        return (c ^ 0xFFFFFFFF) >>> 0;
    }

    function makeChunk(type, data) {
        const len = Buffer.alloc(4);
        len.writeUInt32BE(data.length, 0);
        const typeBuf = Buffer.from(type, 'ascii');
        const crcBuf = Buffer.alloc(4);
        crcBuf.writeUInt32BE(crc32(Buffer.concat([typeBuf, data])), 0);
        return Buffer.concat([len, typeBuf, data, crcBuf]);
    }

    const sig = Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]);
    const ihdr = Buffer.alloc(13);
    ihdr.writeUInt32BE(width, 0);
    ihdr.writeUInt32BE(height, 4);
    ihdr[8] = 8;
    ihdr[9] = 2;
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;

    const png = Buffer.concat([
        sig,
        makeChunk('IHDR', ihdr),
        makeChunk('IDAT', compressed),
        makeChunk('IEND', Buffer.alloc(0))
    ]);

    fs.writeFileSync(pngFile, png);
    try { fs.unlinkSync(ppmFile); } catch (e) {}
    return true;
}

async function sendText(client, text) {
    for (const ch of text) {
        client.write(`sendkey ${ch.toLowerCase()}\n`);
        await sleep(60);
    }
}

async function main() {
    console.log('[TEST] Preparazione immagine per test Finestra Copia Win11...');
    fs.copyFileSync(origImg, testImg);
    if (fs.existsSync(serialLog)) fs.unlinkSync(serialLog);

    const monitorPort = 4488;
    const qemuArgs = [
        '-m', '512M',
        '-smp', '2',
        '-drive', `file=${testImg},format=raw,index=0,media=disk`,
        '-vga', 'std',
        '-serial', `file:${serialLog}`,
        '-monitor', `tcp:127.0.0.1:${monitorPort},server,nowait`,
        '-display', 'none'
    ];

    console.log('[TEST] Avvio QEMU...');
    const qemu = spawn(qemuBin, qemuArgs, { cwd: ROOT });

    await sleep(2000);

    console.log('[TEST] Connessione al monitor QEMU...');
    const client = net.createConnection({ port: monitorPort, host: '127.0.0.1' }, async () => {
        console.log('[TEST] Connesso al monitor QEMU.');
        await sleep(1000);

        console.log('[TEST] Attesa avvio desktop...');
        const booted = await waitForLog(serialLog, '[LOGIN] Desktop avviato con successo', 20000);
        if (!booted) {
            console.log('[WARN] Log marker desktop non rilevato, attendo 5 secondi addizionali...');
            await sleep(5000);
        } else {
            console.log('[OK] Desktop NewOS avviato.');
        }

        await sleep(2500);

        // Esplora Risorse e' aperto su "Risorse del computer" con C:\ selezionato:
        // Invio -> C:\ (primo elemento BootSetup) -> Invio -> C:\BootSetup -> 2x Destra (SYSTEM.PAK) -> Ctrl+C
        console.log('[TEST] Navigazione da tastiera in C:\\BootSetup\\SYSTEM.PAK e avvio copia reale...');
        client.write('sendkey ret\n');
        await sleep(700);
        client.write('sendkey ret\n');
        await sleep(700);
        client.write('sendkey right\n');
        await sleep(250);
        client.write('sendkey right\n');
        await sleep(250);
        client.write('sendkey ctrl-c\n');
        await sleep(1200);

        // Cattura screenshot finestra copia Win11 con dettagli espansi
        {
            const ppm = path.join(ROOT, 'screenshot_win11_copydlg.ppm');
            const png = path.join(ROOT, 'screenshot_win11_copydlg.png');
            client.write(`screendump "${ppm.replace(/\\/g, '/')}"\n`);
            await sleep(1200);
            convertPpmToPng(ppm, png);
            console.log('[OK] Salvato screenshot_win11_copydlg.png');
        }

        // Clic su Fewer details per mostrare la finestra contratta
        // Centro schermo è 512, 384. La finestra è 460x260 centrata:
        // x: 282..742, y: 254..514.
        // Il pulsante Fewer details è a cx + 32, footer_y + 2 (footer_y = cy + 208 = 254 + 24 + 208 = 486).
        // Quindi circa x = 320, y = 490 -> rispetto al centro (512, 384) è dx = -192, dy = +106.
        console.log('[TEST] Clic su Fewer details...');
        client.write('mouse_move -192 106\n');
        await sleep(400);
        client.write('mouse_button 1\n');
        await sleep(150);
        client.write('mouse_button 0\n');
        await sleep(1500);

        {
            const ppm = path.join(ROOT, 'screenshot_win11_copydlg_fewer.ppm');
            const png = path.join(ROOT, 'screenshot_win11_copydlg_fewer.png');
            client.write(`screendump "${ppm.replace(/\\/g, '/')}"\n`);
            await sleep(1200);
            convertPpmToPng(ppm, png);
            console.log('[OK] Salvato screenshot_win11_copydlg_fewer.png');
        }

        // Clic di nuovo su More details per riaprire i dettagli
        console.log('[TEST] Clic su More details...');
        client.write('mouse_move -192 48\n'); // quando contratta, il footer è a y=150 anziché 208
        await sleep(400);
        client.write('mouse_button 1\n');
        await sleep(150);
        client.write('mouse_button 0\n');
        await sleep(1000);

        // Ora clicchiamo sul link Destinazione (Desktop -> NewOS -> ProgramFile -> FtpCache -> U:\Backup)
        // dx = +70, dy = -90 rispetto al centro
        console.log('[TEST] Cambio destinazione su USB (U:\\Backup)...');
        for (let cycle = 0; cycle < 4; cycle++) {
            client.write('mouse_move 70 -90\n');
            await sleep(200);
            client.write('mouse_button 1\n');
            await sleep(100);
            client.write('mouse_button 0\n');
            await sleep(600);
        }

        {
            const ppm = path.join(ROOT, 'screenshot_win11_copydlg_usb.ppm');
            const png = path.join(ROOT, 'screenshot_win11_copydlg_usb.png');
            client.write(`screendump "${ppm.replace(/\\/g, '/')}"\n`);
            await sleep(1200);
            convertPpmToPng(ppm, png);
            console.log('[OK] Salvato screenshot_win11_copydlg_usb.png');
        }

        client.write('quit\n');
        await sleep(500);
        try { client.destroy(); } catch (e) {}
        try { qemu.kill(); } catch (e) {}
        process.exit(0);
    });

    client.on('error', (err) => {
        console.error('[ERRORE MONITOR]', err);
        try { qemu.kill(); } catch (e) {}
        process.exit(1);
    });
}

main().catch(err => {
    console.error(err);
    process.exit(1);
});
