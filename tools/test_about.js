const { spawn, execSync } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const imgPath = path.join(ROOT, 'build', 'myos.img');
const ppmAbout = path.join(ROOT, 'screenshot_about.ppm');
const bmpAbout = path.join(ROOT, 'screenshot_about.bmp');
const pngAbout = 'C:\\Users\\morga\\.gemini\\antigravity-ide\\brain\\201a47cf-7636-4592-93f8-cb22ec54e1bd\\screenshot_about.png';

if (fs.existsSync(ppmAbout)) fs.unlinkSync(ppmAbout);

console.log('Avvio QEMU per test Finestra About (Proprieta di Sistema)...');
const qemu = spawn(qemuBin, [
    '-drive', `format=raw,file=${imgPath}`,
    '-m', '256M',
    '-vga', 'std',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4455,server,nowait',
    '-serial', `file:${path.join(ROOT, 'serial.log')}`,
    '-d', 'guest_errors',
    '-D', path.join(ROOT, 'qemu_debug.log')
], { stdio: 'ignore' });

function sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

setTimeout(async () => {
    console.log('Connessione monitor 4455...');
    const client = net.createConnection({ port: 4455 });

    client.on('error', (err) => console.error('Monitor err:', err.message));
    await sleep(200);

    // Live mode (1)
    console.log('Invio tasto 1 (Live Mode)...');
    client.write('sendkey 1\n');
    await sleep(1500);

    // Chiudiamo Explorer premendo Esc per avere il desktop pulito
    client.write('sendkey esc\n');
    await sleep(500);

    // Muoviamo il mouse sull'icona "Info Sistema" (x=36, y=395+16=411)
    // Partendo dal centro (512, 384): dx = 36 - 512 = -476, dy = -(411 - 384) = -27
    console.log('Spostamento mouse su icona "Info Sistema"...');
    client.write('mouse_move -476 36\n');
    await sleep(400);

    // Doppio clic
    console.log('Doppio clic su "Info Sistema"...');
    client.write('mouse_button 1\n');
    await sleep(60);
    client.write('mouse_button 0\n');
    await sleep(80);
    client.write('mouse_button 1\n');
    await sleep(60);
    client.write('mouse_button 0\n');
    await sleep(1200);

    console.log('Cattura screenshot Proprieta del Sistema...');
    client.write(`screendump "${ppmAbout.replace(/\\/g, '/')}"\n`);
    await sleep(600);

    client.write('quit\n');
    await sleep(400);
    try { client.destroy(); } catch (e) {}
    try { qemu.kill(); } catch (e) {}

    execSync(`node "${path.join(ROOT, 'tools', 'ppm2bmp.js')}" "${ppmAbout}" "${bmpAbout}"`, { stdio: 'inherit' });
    execSync(`powershell -ExecutionPolicy Bypass -File "${path.join(ROOT, 'tools', 'bmp2png.ps1')}" "${bmpAbout}" "${pngAbout}"`, { stdio: 'inherit' });
    console.log('[OK] About screenshot completato!');
    process.exit(0);
}, 1500);
