const { spawn, execSync } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const imgPath = path.join(ROOT, 'build', 'NewOS_V1.img');
const ppmBrowser = path.join(ROOT, 'screenshot_browser.ppm');
const bmpBrowser = path.join(ROOT, 'screenshot_browser.bmp');
const pngBrowser = 'C:\\Users\\morga\\.gemini\\antigravity-ide\\brain\\cce9850e-402e-48dd-9a6f-1743e743feab\\screenshot_webbrowser.png';

const ppmYouTube = path.join(ROOT, 'screenshot_youtube.ppm');
const bmpYouTube = path.join(ROOT, 'screenshot_youtube.bmp');
const pngYouTube = 'C:\\Users\\morga\\.gemini\\antigravity-ide\\brain\\cce9850e-402e-48dd-9a6f-1743e743feab\\screenshot_youtube.png';

if (fs.existsSync(ppmBrowser)) fs.unlinkSync(ppmBrowser);
if (fs.existsSync(ppmYouTube)) fs.unlinkSync(ppmYouTube);

console.log('[TEST] Avvio QEMU per collaudo visuale YouTube HTML5...');
const qemu = spawn(qemuBin, [
    '-drive', `format=raw,file=${imgPath}`,
    '-m', '256M',
    '-vga', 'std',
    '-netdev', 'user,id=net0',
    '-device', 'e1000,netdev=net0',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4460,server,nowait',
    '-serial', `file:${path.join(ROOT, 'serial_browser.log')}`,
    '-d', 'guest_errors',
    '-D', path.join(ROOT, 'qemu_debug.log')
], { stdio: 'ignore' });

function sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

setTimeout(async () => {
    console.log('Connessione al monitor QEMU (porta 4460)...');
    const client = net.createConnection({ port: 4460 });

    client.on('error', (err) => console.error('Monitor err:', err.message));
    await sleep(500);

    // Invio tasto 1 per avviare subito la modalita' Live senza attendere il timeout
    console.log('Selezione avvio Live immediato (sendkey 1)...');
    await sleep(800);
    client.write('sendkey 1\n');
    await sleep(4000);

    // Cattura screenshot della pagina YouTube HTML5 renderizzata
    console.log('Cattura screenshot YouTube HTML5...');
    client.write(`screendump "${ppmYouTube.replace(/\\/g, '/')}"\n`);
    await sleep(1200);

    client.write('quit\n');
    await sleep(400);
    try { client.destroy(); } catch (e) {}
    try { qemu.kill(); } catch (e) {}

    // Conversione PPM -> BMP -> PNG
    if (fs.existsSync(ppmYouTube)) {
        execSync(`node "${path.join(ROOT, 'tools', 'ppm2bmp.js')}" "${ppmYouTube}" "${bmpYouTube}"`, { stdio: 'inherit' });
        execSync(`powershell -ExecutionPolicy Bypass -File "${path.join(ROOT, 'tools', 'bmp2png.ps1')}" "${bmpYouTube}" "${pngYouTube}"`, { stdio: 'inherit' });
        console.log(`[OK] Screenshot YouTube HTML5 salvato in: ${pngYouTube}`);
    }

    process.exit(0);
}, 1500);
