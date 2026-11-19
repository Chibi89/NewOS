/**
 * tools/test_recycle_dll.js
 * Collaudo visivo in QEMU di RECYCLE.DLL:
 *   1. Vista Cestino in Esplora Risorse con nuova Quota e pulsante [Opzioni Cestino]
 *   2. Finestra Proprieta e Gestione Cestino (RECYCLE.DLL - recyclecfg)
 *   3. Finestra Avviso Superamento Limite Spazio -> Suggerimento Eliminazione Diretta (recyclequota)
 */

const { spawn } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');
const zlib = require('zlib');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const origImg = path.join(ROOT, 'build', 'NewOS_V1.img');
const testImg = path.join(ROOT, 'build', 'test_recycle.img');
const serialLog = path.join(ROOT, 'serial_recycle.log');

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

async function sendText(client, str) {
    for (const ch of str) {
        if (ch === ' ') {
            client.write('sendkey spc\n');
        } else if (ch >= 'A' && ch <= 'Z') {
            client.write(`sendkey shift-${ch.toLowerCase()}\n`);
        } else {
            client.write(`sendkey ${ch}\n`);
        }
        await sleep(120);
    }
}

async function main() {
    fs.copyFileSync(origImg, testImg);
    if (fs.existsSync(serialLog)) fs.unlinkSync(serialLog);

    const qemu = spawn(qemuBin, [
        '-drive', `format=raw,file=${testImg}`,
        '-m', '256M',
        '-vga', 'std',
        '-netdev', 'user,id=net0',
        '-device', 'e1000,netdev=net0',
        '-display', 'none',
        '-monitor', 'tcp:127.0.0.1:4570,server,nowait',
        '-serial', `file:${serialLog}`
    ], { stdio: 'ignore' });

    await sleep(1800);

    return new Promise((resolve, reject) => {
        let sentQuit = false;
        const client = net.createConnection({ host: '127.0.0.1', port: 4570 }, async () => {
            await waitForLog(serialLog, '[BOOT_MENU] Pronto!', 15000);
            await sleep(400);
            client.write('sendkey 4\n');

            await waitForLog(serialLog, '[CURSOR_LOADER] Inizializzazione completata', 15000);
            await sleep(1200);

            // 1. Apri Cestino in Esplora Risorse (recycle)
            console.log('[TEST] Apertura Esplora Risorse (Cestino)...');
            await sendText(client, 'recycle');
            await sleep(200);
            client.write('sendkey ret\n');
            await sleep(2200);
            {
                const ppm = path.join(ROOT, 'screenshot_recycle_bin_updated.ppm');
                const png = path.join(ROOT, 'screenshot_recycle_bin_updated.png');
                client.write(`screendump "${ppm.replace(/\\/g, '/')}"\n`);
                await sleep(1200);
                convertPpmToPng(ppm, png);
                console.log('[OK] Salvato screenshot_recycle_bin_updated.png');
            }
            client.write('sendkey alt-f4\n');
            await sleep(800);

            // 2. Apri Proprietà e Gestione Cestino (RECYCLE.DLL - recyclecfg)
            console.log('[TEST] Apertura Finestra Proprieta e Quota Cestino (RECYCLE.DLL)...');
            await sendText(client, 'recyclecfg');
            await sleep(200);
            client.write('sendkey ret\n');
            await sleep(2200);
            {
                const ppm = path.join(ROOT, 'screenshot_recycle_settings.ppm');
                const png = path.join(ROOT, 'screenshot_recycle_settings.png');
                client.write(`screendump "${ppm.replace(/\\/g, '/')}"\n`);
                await sleep(1200);
                convertPpmToPng(ppm, png);
                console.log('[OK] Salvato screenshot_recycle_settings.png');
            }
            client.write('sendkey esc\n');
            await sleep(800);

            // 3. Test Avviso Superamento Limite Spazio -> Suggerimento Eliminazione Diretta (recyclequota)
            console.log('[TEST] Apertura Dialogo Quota Superata -> Suggerimento Eliminazione Diretta...');
            await sendText(client, 'recyclequota');
            await sleep(200);
            client.write('sendkey ret\n');
            await sleep(2200);
            {
                const ppm = path.join(ROOT, 'screenshot_recycle_quota_exceeded.ppm');
                const png = path.join(ROOT, 'screenshot_recycle_quota_exceeded.png');
                client.write(`screendump "${ppm.replace(/\\/g, '/')}"\n`);
                await sleep(1200);
                convertPpmToPng(ppm, png);
                console.log('[OK] Salvato screenshot_recycle_quota_exceeded.png');
            }
            client.write('sendkey esc\n');
            await sleep(800);

            sentQuit = true;
            client.write('quit\n');
            await sleep(500);
            try { client.destroy(); } catch (e) {}
            try { qemu.kill(); } catch (e) {}
            resolve();
        });

        client.on('error', (err) => {
            if (!sentQuit) reject(err);
        });
    });
}

main().catch(err => {
    console.error('[ERRORE]', err);
    process.exit(1);
});

