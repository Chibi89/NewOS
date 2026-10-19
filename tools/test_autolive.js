const { spawn, execSync } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const isoPath = path.join(ROOT, 'NewOS_V1.iso');

function sleep(ms) {
    return new Promise(res => setTimeout(res, ms));
}

console.log('[AUTOLIVE] Avvio QEMU per verificare boot automatico Live al desktop...');

const qemu = spawn(qemuBin, [
    '-cdrom', isoPath,
    '-m', '128M',
    '-vga', 'std',
    '-netdev', 'user,id=net0',
    '-device', 'e1000,netdev=net0',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4635,server,nowait',
    '-serial', `file:${path.join(ROOT, 'serial_autolive.log')}`
], { stdio: 'ignore' });

setTimeout(() => {
    const client = net.createConnection({ port: 4635 }, async () => {
        console.log('[AUTOLIVE] Connesso al monitor QEMU...');
        console.log('[AUTOLIVE] Attesa 7.5 secondi (timeout boot menu 2s + rendering desktop)...');
        await sleep(7500);

        const ppm = path.join(ROOT, 'autolive_screen.ppm');
        const bmp = path.join(ROOT, 'autolive_screen.bmp');
        const png = path.join(ROOT, 'autolive_screen.png');

        console.log('[AUTOLIVE] Cattura screenshot desktop...');
        client.write(`screendump "${ppm.replace(/\\/g, '/')}"\n`);
        await sleep(1500);

        client.write('quit\n');
        await sleep(500);
        qemu.kill();

        if (fs.existsSync(ppm)) {
            console.log('[AUTOLIVE] PPM catturato con successo, dimensione:', fs.statSync(ppm).size);
            execSync(`node tools/ppm2bmp.js "${ppm}" "${bmp}"`, { stdio: 'inherit' });
            execSync(`powershell -ExecutionPolicy Bypass -File tools/bmp2png.ps1 "${bmp}" "${png}"`, { stdio: 'inherit' });
            console.log('[AUTOLIVE] PNG generato con successo:', fs.statSync(png).size);
        } else {
            console.error('[AUTOLIVE ERR] PPM non trovato!');
        }
        process.exit(0);
    });

    client.on('error', (err) => {
        console.error('[AUTOLIVE ERR] Errore connessione:', err.message);
        qemu.kill();
        process.exit(1);
    });
}, 1500);
