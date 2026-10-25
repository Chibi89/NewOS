const { spawn, execSync } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const imgPath = path.join(ROOT, 'build', 'NewOS_V1.img');

const ppmPlayer = path.join(ROOT, 'screenshot_player.ppm');
const bmpPlayer = path.join(ROOT, 'screenshot_player.bmp');
const pngPlayer = path.join(ROOT, 'screenshot_player.png');
const artifactPng = 'C:\\Users\\morga\\.gemini\\antigravity-ide\\brain\\2a833b14-8f67-4c71-b5d5-281b1da0491a\\screenshot_player.png';
const serialLog = path.join(ROOT, 'serial_player.log');

for (const p of [ppmPlayer, bmpPlayer, pngPlayer, serialLog]) {
    try { if (fs.existsSync(p)) fs.unlinkSync(p); } catch (e) {}
}

console.log('[TEST] Avvio QEMU per collaudo visuale NewOS Media Player (PLAYER.EXE)...');
const qemu = spawn(qemuBin, [
    '-drive', `format=raw,file=${imgPath}`,
    '-m', '256M',
    '-vga', 'std',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4465,server,nowait',
    '-serial', `file:${serialLog}`,
    '-d', 'guest_errors',
    '-D', path.join(ROOT, 'qemu_debug.log')
], { stdio: 'ignore' });

function sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

async function waitForSerial(pattern, timeoutMs) {
    const start = Date.now();
    while (Date.now() - start < timeoutMs) {
        if (fs.existsSync(serialLog)) {
            try {
                const content = fs.readFileSync(serialLog, 'utf8');
                if (content.includes(pattern)) return true;
            } catch (e) {}
        }
        await sleep(100);
    }
    return false;
}

setTimeout(async () => {
    console.log('Connessione al monitor QEMU (porta 4465)...');
    const client = net.createConnection({ port: 4465 });
    client.on('error', (err) => console.error('Monitor err:', err.message));

    console.log('Attesa arrivo a boot_menu...');
    const atMenu = await waitForSerial('[BOOT_MENU] Pronto!', 10000);
    console.log(`Boot menu raggiunto: ${atMenu}`);
    await sleep(500);

    console.log('Selezione avvio Media Player (sendkey 7 con conferma seriale)...');
    let chosen = false;
    for (let attempt = 0; attempt < 12; attempt++) {
        client.write('sendkey 7\n');
        await sleep(400);
        if (fs.existsSync(serialLog)) {
            const content = fs.readFileSync(serialLog, 'utf8');
            if (content.includes('[BOOT_MENU] Scelto 7')) {
                chosen = true;
                console.log('Scelto 7 confermato via seriale!');
                break;
            }
        }
    }

    if (!chosen) {
        console.warn('Avviso: Scelto 7 non rilevato, invio fallback sendkey 7');
        client.write('sendkey 7\n');
    }

    console.log('Attesa caricamento desktop e disegno Media Player...');
    // Attende che il desktop entri nel main loop (emette '.') oppure attende 6 secondi
    const atLoop = await waitForSerial('.', 7000);
    console.log(`Desktop main loop attivo: ${atLoop}`);
    await sleep(2000);

    // Cattura screenshot dell'interfaccia grafica del riproduttore multimediale
    console.log('Cattura screenshot NewOS Media Player...');
    client.write(`screendump "${ppmPlayer.replace(/\\/g, '/')}"\n`);

    // Attende la scrittura effettiva del file PPM
    const startWait = Date.now();
    while (Date.now() - startWait < 5000) {
        if (fs.existsSync(ppmPlayer) && fs.statSync(ppmPlayer).size > 1000000) break;
        await sleep(200);
    }
    await sleep(500);

    client.write('quit\n');
    await sleep(400);
    try { client.destroy(); } catch (e) {}
    try { qemu.kill(); } catch (e) {}

    // Conversione PPM -> BMP -> PNG
    if (fs.existsSync(ppmPlayer) && fs.statSync(ppmPlayer).size > 1000000) {
        execSync(`node "${path.join(ROOT, 'tools', 'ppm2bmp.js')}" "${ppmPlayer}" "${bmpPlayer}"`, { stdio: 'inherit' });
        execSync(`powershell -ExecutionPolicy Bypass -File "${path.join(ROOT, 'tools', 'bmp2png.ps1')}" "${bmpPlayer}" "${pngPlayer}"`, { stdio: 'inherit' });
        try {
            fs.copyFileSync(pngPlayer, artifactPng);
        } catch (e) {}
        console.log(`[OK] Screenshot NewOS Media Player salvato in: ${pngPlayer}`);
    } else {
        console.error('[ERRORE] File screenshot non generato.');
    }

    process.exit(0);
}, 1200);
