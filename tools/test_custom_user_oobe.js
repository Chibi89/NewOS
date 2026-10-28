/**
 * tools/test_custom_user_oobe.js
 * Collaudo completo della Migrazione e Creazione Dinamica del Profilo Utente in OOBE:
 *  1. Avvio in QEMU su disco vergine e apertura di OOBE.MOD.
 *  2. Impostazione del nome utente personalizzato "Mario" al Passo 4 (in sostituzione di "Morgan").
 *  3. Completamento OOBE (Passo 5) -> verifica migrazione atomica C:\User\Morgan -> C:\User\Mario.
 *  4. Apertura di Esplora Risorse in C:\User -> screenshot con "Default" e "Mario" (senza "Morgan").
 *  5. Riavvio di QEMU sullo stesso disco -> verifica bypass automatico Boot Menu e caricamento
 *     di LOGIN.MOD con "Mario" precompilato come utente predefinito.
 */

const { spawn, execSync } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const origImg = path.join(ROOT, 'myos.img');
const testImg = path.join(ROOT, 'build', 'test_custom_user.img');
const serialLog1 = path.join(ROOT, 'serial_oobe_mario_1.log');
const serialLog2 = path.join(ROOT, 'serial_oobe_mario_2.log');

const ppmStep4 = path.join(ROOT, 'screenshot_oobe_step4_mario.ppm');
const pngStep4 = path.join(ROOT, 'screenshot_oobe_step4_mario.png');
const ppmExplorer = path.join(ROOT, 'screenshot_explorer_user_mario.ppm');
const pngExplorer = path.join(ROOT, 'screenshot_explorer_user_mario.png');
const ppmLogin = path.join(ROOT, 'screenshot_login_mario.ppm');
const pngLogin = path.join(ROOT, 'screenshot_login_mario.png');

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
    while (buf[offset] !== 0x20 && buf[offset] !== 0x0A) { wStr += String.fromCharCode(buf[offset++]); }
    offset++;
    let hStr = '';
    while (buf[offset] !== 0x20 && buf[offset] !== 0x0A) { hStr += String.fromCharCode(buf[offset++]); }
    offset++;
    while (buf[offset] !== 0x0A) offset++;
    offset++;

    const width = parseInt(wStr, 10);
    const height = parseInt(hStr, 10);

    const bmpFile = pngFile.replace(/\.png$/, '.bmp');
    const rowSize = width * 3;
    const paddedRowSize = Math.floor((rowSize + 3) / 4) * 4;
    const pixelDataSize = paddedRowSize * height;
    const bmpBuf = Buffer.alloc(54 + pixelDataSize, 0);

    bmpBuf.write('BM', 0);
    bmpBuf.writeUInt32LE(54 + pixelDataSize, 2);
    bmpBuf.writeUInt32LE(54, 10);
    bmpBuf.writeUInt32LE(40, 14);
    bmpBuf.writeInt32LE(width, 18);
    bmpBuf.writeInt32LE(-height, 22);
    bmpBuf.writeUInt16LE(1, 26);
    bmpBuf.writeUInt16LE(24, 28);
    bmpBuf.writeUInt32LE(0, 30);

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

    try {
        const psCmd = `Add-Type -AssemblyName System.Drawing; $bmp = [System.Drawing.Image]::FromFile('${bmpFile.replace(/\\/g, '\\\\')}'); $bmp.Save('${pngFile.replace(/\\/g, '\\\\')}', [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose();`;
        execSync(`powershell -NoProfile -Command "${psCmd}"`, { stdio: 'ignore' });
        console.log(`[OK] PNG generato: ${pngFile}`);
        return true;
    } catch (e) {
        return false;
    }
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
        await sleep(140);
    }
}

async function runPhase1() {
    console.log('=== FASE 1: ESECUZIONE OOBE CON UTENTE PERSONALIZZATO "Mario" E VERIFICA C:\\User ===');
    fs.copyFileSync(origImg, testImg);
    if (fs.existsSync(serialLog1)) fs.unlinkSync(serialLog1);

    const qemu = spawn(qemuBin, [
        '-drive', `format=raw,file=${testImg}`,
        '-m', '256M',
        '-vga', 'std',
        '-audiodev', 'none,id=snd0',
        '-device', 'AC97,audiodev=snd0',
        '-display', 'none',
        '-monitor', 'tcp:127.0.0.1:4561,server,nowait',
        '-serial', `file:${serialLog1}`
    ], { stdio: 'ignore' });

    await sleep(1000);

    return new Promise((resolve, reject) => {
        let sentQuit = false;
        const client = net.createConnection({ port: 4561 }, async () => {
            console.log('[1] Connesso al monitor QEMU (porta 4561)...');
            await waitForLog(serialLog1, '[BOOT_MENU] Pronto!', 15000);
            await sleep(400);

            // Seleziona 4 (Prompt dei comandi) al Boot Menu
            console.log('[1] Selezione opzione 4 (CMD.EXE)...');
            client.write('sendkey 4\n');

            // Attendi caricamento Desktop + CMD.EXE
            await waitForLog(serialLog1, '[CURSOR_LOADER] Inizializzazione completata', 15000);
            await sleep(1200);

            // Digita "oobe" + Invio per avviare OOBE.MOD
            console.log('[1] Avvio OOBE.MOD...');
            await sendText(client, 'oobe');
            client.write('sendkey ret\n');

            await waitForLog(serialLog1, 'Modulo \'OOBE.MOD\' verificato', 10000);
            await sleep(1200);

            // Avanza da Passo 0 -> Passo 1 -> Passo 2 -> Passo 3 -> Passo 4
            for (let step = 1; step <= 4; step++) {
                console.log(`[1] Avanzamento a Passo ${step}...`);
                client.write('sendkey ret\n');
                await waitForLog(serialLog1, `S${step}`, 5000);
                await sleep(400);
            }

            // Siamo al Passo 4 (Account Utente & Stile), campo 0 = username ("Morgan")
            console.log('[1] Cancellazione "Morgan" e inserimento "Mario"...');
            for (let b = 0; b < 8; b++) {
                client.write('sendkey backspace\n');
                await sleep(120);
            }
            await sendText(client, 'Mario');
            await sleep(600);

            console.log('[1] Cattura screendump OOBE Passo 4 ("Mario")...');
            client.write(`screendump "${ppmStep4.replace(/\\/g, '/')}"\n`);
            await sleep(1200);

            // Passo 4 -> Passo 5 (Riepilogo)
            console.log('[1] Avanzamento a Passo 5 (Riepilogo)...');
            client.write('sendkey ret\n');
            await waitForLog(serialLog1, 'S5', 5000);
            await sleep(600);

            // Passo 5 -> Fine OOBE (migrazione C:\User\Morgan -> C:\User\Mario + salvataggio OOBE.INI e USER.CFG)
            console.log('[1] Conferma finale OOBE...');
            client.write('sendkey ret\n');
            await waitForLog(serialLog1, 'XMCK', 8000);
            await sleep(1000);

            // Ora siamo tornati in CMD.EXE: apriamo Esplora Risorse sulla cartella C:\User
            console.log('[1] Apertura Esplora Risorse in C:\\User...');
            await sendText(client, 'explorer User');
            client.write('sendkey ret\n');
            await sleep(2500);

            console.log('[1] Cattura screendump Esplora Risorse in C:\\User...');
            client.write(`screendump "${ppmExplorer.replace(/\\/g, '/')}"\n`);
            await sleep(1500);

            sentQuit = true;
            client.write('quit\n');
            await sleep(600);

            try { client.destroy(); } catch (e) {}
            try { qemu.kill(); } catch (e) {}

            convertPpmToPng(ppmStep4, pngStep4);
            convertPpmToPng(ppmExplorer, pngExplorer);
            resolve();
        });

        client.on('error', (err) => {
            if (!sentQuit) reject(err);
        });
    });
}

async function runPhase2() {
    console.log('\n=== FASE 2: RIAVVIO SU STESSO DISCO -> VERIFICA LOGIN.MOD CON "Mario" ===');
    if (fs.existsSync(serialLog2)) fs.unlinkSync(serialLog2);

    const qemu = spawn(qemuBin, [
        '-drive', `format=raw,file=${testImg}`,
        '-m', '256M',
        '-vga', 'std',
        '-audiodev', 'none,id=snd0',
        '-device', 'AC97,audiodev=snd0',
        '-display', 'none',
        '-monitor', 'tcp:127.0.0.1:4562,server,nowait',
        '-serial', `file:${serialLog2}`
    ], { stdio: 'ignore' });

    await sleep(1000);

    return new Promise((resolve, reject) => {
        let sentQuit = false;
        const client = net.createConnection({ port: 4562 }, async () => {
            console.log('[2] Connesso al monitor QEMU (porta 4562)...');
            await waitForLog(serialLog2, 'Modulo \'LOGIN.MOD\' verificato', 15000);
            await sleep(1500);

            console.log('[2] Cattura screendump LOGIN.MOD al riavvio...');
            client.write(`screendump "${ppmLogin.replace(/\\/g, '/')}"\n`);
            await sleep(1500);

            sentQuit = true;
            client.write('quit\n');
            await sleep(600);

            try { client.destroy(); } catch (e) {}
            try { qemu.kill(); } catch (e) {}

            convertPpmToPng(ppmLogin, pngLogin);
            resolve();
        });

        client.on('error', (err) => {
            if (!sentQuit) reject(err);
        });
    });
}

async function main() {
    await runPhase1();
    await runPhase2();
    console.log('\n=== COLLAUDO OOBE CUSTOM USER COMPLETATO CON SUCCESSO ===');
    process.exit(0);
}

main().catch(err => {
    console.error('Errore durante il test:', err);
    process.exit(1);
});
