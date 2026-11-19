/**
 * tools/test_block2_comdlg.js
 * Collaudo visivo in QEMU del Blocco 2:
 *   1. COMDLG.DLL - Dialogo Universale Apri/Salva File (comdlg)
 *   2. COMDLG.DLL - Gestore Dual-Pane Stack di File & Rete FTP (ftpstack)
 *   3. Cestino Reale VFS (C:\Recycle.Bin) con Ripristino e Svuotamento (recycle)
 */

const { spawn } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');
const zlib = require('zlib');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const origImg = path.join(ROOT, 'build', 'NewOS_V1.img');
const testImg = path.join(ROOT, 'build', 'test_block2.img');
const serialLog = path.join(ROOT, 'serial_block2.log');

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
        '-monitor', 'tcp:127.0.0.1:4569,server,nowait',
        '-serial', `file:${serialLog}`
    ], { stdio: 'ignore' });

    await sleep(1800);

    return new Promise((resolve, reject) => {
        let sentQuit = false;
        const client = net.createConnection({ host: '127.0.0.1', port: 4569 }, async () => {
            await waitForLog(serialLog, '[BOOT_MENU] Pronto!', 15000);
            await sleep(400);
            client.write('sendkey 4\n');

            await waitForLog(serialLog, '[CURSOR_LOADER] Inizializzazione completata', 15000);
            await sleep(1200);

            const tests = [
                { cmd: 'comdlg',   name: 'screenshot_comdlg_open',      closeKey: 'esc' },
                { cmd: 'ftpstack', name: 'screenshot_comdlg_ftp_stack', closeKey: 'esc' },
                { cmd: 'recycle',  name: 'screenshot_recycle_bin',      closeKey: 'alt-f4' }
            ];

            for (const t of tests) {
                console.log(`[TEST] Lancio ${t.cmd}...`);
                await sendText(client, t.cmd);
                await sleep(200);
                client.write('sendkey ret\n');
                await sleep(2200);

                const ppm = path.join(ROOT, `${t.name}.ppm`);
                const png = path.join(ROOT, `${t.name}.png`);
                client.write(`screendump "${ppm.replace(/\\/g, '/')}"\n`);
                await sleep(1200);
                convertPpmToPng(ppm, png);
                console.log(`[OK] Salvato ${t.name}.png`);

                client.write(`sendkey ${t.closeKey}\n`);
                await sleep(700);
            }

            // 4. Test Blocco Note con nuova barra strumenti + Ctrl+S (Salva con nome COMDLG.DLL)
            console.log('[TEST] Lancio notepad...');
            await sendText(client, 'notepad');
            await sleep(200);
            client.write('sendkey ret\n');
            await sleep(2000);
            {
                const ppm = path.join(ROOT, 'screenshot_notepad_comdlg.ppm');
                const png = path.join(ROOT, 'screenshot_notepad_comdlg.png');
                client.write(`screendump "${ppm.replace(/\\/g, '/')}"\n`);
                await sleep(1200);
                convertPpmToPng(ppm, png);
                console.log('[OK] Salvato screenshot_notepad_comdlg.png');
            }

            console.log('[TEST] Invio Ctrl+S a Blocco Note per aprire Salva con nome (COMDLG.DLL)...');
            client.write('sendkey ctrl-s\n');
            await sleep(2000);
            {
                const ppm = path.join(ROOT, 'screenshot_comdlg_save.ppm');
                const png = path.join(ROOT, 'screenshot_comdlg_save.png');
                client.write(`screendump "${ppm.replace(/\\/g, '/')}"\n`);
                await sleep(1200);
                convertPpmToPng(ppm, png);
                console.log('[OK] Salvato screenshot_comdlg_save.png');
            }
            client.write('sendkey esc\n');
            await sleep(600);
            client.write('sendkey alt-f4\n');
            await sleep(800);

            // 5. Test Esplora Risorse: Menu Contestuale Tasto Destro (Taglia / Copia / Sposta / Incolla) + Ctrl+C + Sposta + Ctrl+V
            console.log('[TEST] Lancio explorer...');
            await sendText(client, 'explorer');
            await sleep(300);
            client.write('sendkey ret\n');
            await sleep(2200);

            // Disco Locale (C:) è già selezionato (indice 0): premi Invio per entrare in C:\
            client.write('sendkey ret\n');
            await sleep(1600);

            // Seleziona elemento con Tab
            client.write('sendkey tab\n');
            await sleep(400);

            console.log('[TEST] Pressione Ctrl+C -> Apertura Finestra Copia (COMDLG.DLL)...');
            client.write('sendkey ctrl-c\n');
            await sleep(2000);
            {
                const ppm = path.join(ROOT, 'screenshot_comdlg_copy_ctrlc.ppm');
                const png = path.join(ROOT, 'screenshot_comdlg_copy_ctrlc.png');
                client.write(`screendump "${ppm.replace(/\\/g, '/')}"\n`);
                await sleep(1200);
                convertPpmToPng(ppm, png);
                console.log('[OK] Salvato screenshot_comdlg_copy_ctrlc.png');
            }
            client.write('sendkey esc\n');
            await sleep(800);

            console.log('[TEST] Pressione Ctrl+X -> Apertura Finestra Spostamento (COMDLG.DLL)...');
            client.write('sendkey ctrl-x\n');
            await sleep(2000);
            {
                const ppm = path.join(ROOT, 'screenshot_comdlg_move.ppm');
                const png = path.join(ROOT, 'screenshot_comdlg_move.png');
                client.write(`screendump "${ppm.replace(/\\/g, '/')}"\n`);
                await sleep(1200);
                convertPpmToPng(ppm, png);
                console.log('[OK] Salvato screenshot_comdlg_move.png');
            }
            client.write('sendkey esc\n');
            await sleep(800);

            console.log('[TEST] Pressione Ctrl+V -> Apertura Finestra Incolla (COMDLG.DLL)...');
            client.write('sendkey ctrl-v\n');
            await sleep(2000);
            {
                const ppm = path.join(ROOT, 'screenshot_comdlg_paste_ctrlv.ppm');
                const png = path.join(ROOT, 'screenshot_comdlg_paste_ctrlv.png');
                client.write(`screendump "${ppm.replace(/\\/g, '/')}"\n`);
                await sleep(1200);
                convertPpmToPng(ppm, png);
                console.log('[OK] Salvato screenshot_comdlg_paste_ctrlv.png');
            }
            client.write('sendkey esc\n');
            await sleep(800);

            // Clic destro sull'elemento in Esplora Risorse (da centro 512,384 -> -60,-140 = 452,244 sopra cartella EFI)
            console.log('[TEST] Clic destro in Esplora Risorse (Menu Contestuale con Copia / Sposta / Incolla)...');
            client.write('mouse_move -60 -140\n');
            await sleep(400);
            client.write('mouse_button 2\n');
            await sleep(200);
            client.write('mouse_button 0\n');
            await sleep(1200);
            {
                const ppm = path.join(ROOT, 'screenshot_context_menu_move.ppm');
                const png = path.join(ROOT, 'screenshot_context_menu_move.png');
                client.write(`screendump "${ppm.replace(/\\/g, '/')}"\n`);
                await sleep(1200);
                convertPpmToPng(ppm, png);
                console.log('[OK] Salvato screenshot_context_menu_move.png');
            }

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
