const { spawn } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const isoPath = path.join(ROOT, 'build', 'myos.iso');
const ppmPath = path.join(ROOT, 'screenshot_iso_boot.ppm');

const qemu = spawn(qemuBin, [
    '-cdrom', isoPath,
    '-m', '256M',
    '-vga', 'std',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4490,server,nowait',
    '-serial', `file:${path.join(ROOT, 'serial_iso.log')}`
], { stdio: 'ignore' });

function sleep(ms) {
    return new Promise(res => setTimeout(res, ms));
}

setTimeout(() => {
    const client = net.createConnection({ port: 4490 }, async () => {
        console.log('Connesso al monitor QEMU...');
        await sleep(1500);

        // Cattura screenshot della schermata iniziale di boot da ISO
        client.write(`screendump "${ppmPath.replace(/\\/g, '/')}"\n`);
        await sleep(600);

        // Invia '1' per entrare in Live mode
        console.log('Invio 1 per Live mode...');
        client.write('sendkey 1\n');
        await sleep(2000);

        const ppmDesktop = path.join(ROOT, 'screenshot_iso_desktop.ppm');
        client.write(`screendump "${ppmDesktop.replace(/\\/g, '/')}"\n`);
        await sleep(600);

        client.end();
        qemu.kill();

        console.log('Screenshot catturati con successo!');
        process.exit(0);
    });
}, 1000);
