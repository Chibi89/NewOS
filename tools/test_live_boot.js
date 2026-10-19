const { spawn } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const imgPath = path.join(ROOT, 'build', 'myos.img');
const ppmLive = path.join(ROOT, 'screenshot_live_desktop.ppm');
const bmpLive = path.join(ROOT, 'screenshot_live_desktop.bmp');
const pngLive = 'C:\\Users\\morga\\.gemini\\antigravity-ide\\brain\\201a47cf-7636-4592-93f8-cb22ec54e1bd\\screenshot_live_desktop.png';

if (fs.existsSync(ppmLive)) fs.unlinkSync(ppmLive);

console.log('Avvio QEMU per test Live Boot...');
const qemu = spawn(qemuBin, [
    '-drive', `format=raw,file=${imgPath}`,
    '-m', '256M',
    '-vga', 'std',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4450,server,nowait',
    '-serial', `file:${path.join(ROOT, 'serial.log')}`,
    '-d', 'guest_errors',
    '-D', path.join(ROOT, 'qemu_debug.log')
], { stdio: 'ignore' });

function sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

setTimeout(async () => {
    console.log('Connessione monitor 4450...');
    const client = net.createConnection({ port: 4450 });

    client.on('error', (err) => console.error('Monitor err:', err.message));
    await sleep(200);

    // Premiamo '1' per avvio Live immediato
    console.log('Invio tasto 1 (Live Mode)...');
    client.write('sendkey 1\n');
    await sleep(1500);

    console.log('Cattura screenshot Live Desktop...');
    client.write(`screendump "${ppmLive.replace(/\\/g, '/')}"\n`);
    await sleep(600);

    client.write('quit\n');
    await sleep(400);
    try { client.destroy(); } catch (e) {}
    try { qemu.kill(); } catch (e) {}

    const { execSync } = require('child_process');
    execSync(`node "${path.join(ROOT, 'tools', 'ppm2bmp.js')}" "${ppmLive}" "${bmpLive}"`, { stdio: 'inherit' });
    execSync(`powershell -ExecutionPolicy Bypass -File "${path.join(ROOT, 'tools', 'bmp2png.ps1')}" "${bmpLive}" "${pngLive}"`, { stdio: 'inherit' });
    console.log('[OK] Live Desktop screenshot completato!');
    process.exit(0);
}, 1500);
