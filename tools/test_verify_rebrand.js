const { spawn, execSync } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const imgPath = path.join(ROOT, 'build', 'myos.img');
const ppmStart = path.join(ROOT, 'screenshot_start_open.ppm');
const bmpStart = path.join(ROOT, 'screenshot_start_open.bmp');
const pngStart = 'C:\\Users\\morga\\.gemini\\antigravity-ide\\brain\\201a47cf-7636-4592-93f8-cb22ec54e1bd\\screenshot_start_open.png';

if (fs.existsSync(ppmStart)) fs.unlinkSync(ppmStart);

console.log('Avvio QEMU per test Start Menu aperto...');
const qemu = spawn(qemuBin, [
    '-drive', `format=raw,file=${imgPath}`,
    '-m', '256M',
    '-vga', 'std',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4460,server,nowait'
], { stdio: 'ignore' });

function sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

setTimeout(async () => {
    console.log('Connessione monitor 4460...');
    const client = net.createConnection({ port: 4460 });

    client.on('error', (err) => console.error('Monitor err:', err.message));
    await sleep(300);

    // Boot menu -> 1 (Live Mode)
    console.log('Invio tasto 1 (Live Mode)...');
    client.write('sendkey 1\n');
    await sleep(1500);

    // Click su Start: sposta mouse all'angolo in basso a sinistra
    for (let i = 0; i < 15; i++) {
        client.write('mouse_move -40 35\n');
        await sleep(30);
    }
    await sleep(100);

    // Posiziona sopra il bottone Start (x=30, y=755)
    client.write('mouse_move 30 -5\n');
    await sleep(150);

    // Click
    console.log('Clic su pulsante Start...');
    client.write('mouse_button 1\n');
    await sleep(60);
    client.write('mouse_button 0\n');
    await sleep(500);

    // Screendump
    console.log('Cattura screenshot...');
    client.write(`screendump "${ppmStart.replace(/\\/g, '/')}"\n`);
    await sleep(1200);

    client.write('quit\n');
    await sleep(500);

    try { client.destroy(); } catch (e) {}
    try { qemu.kill(); } catch (e) {}

    if (fs.existsSync(ppmStart)) {
        execSync(`node "${path.join(ROOT, 'tools', 'ppm2bmp.js')}" "${ppmStart}" "${bmpStart}"`, { stdio: 'inherit' });
        execSync(`powershell -ExecutionPolicy Bypass -File "${path.join(ROOT, 'tools', 'bmp2png.ps1')}" "${bmpStart}" "${pngStart}"`, { stdio: 'inherit' });
        console.log('[OK] Start menu screenshot pronto!');
    }
    process.exit(0);
}, 2000);
