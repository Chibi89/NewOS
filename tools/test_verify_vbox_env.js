const { spawn } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const isoPath = path.join(ROOT, 'NewOS_V1.iso');
const blankDiskPath = path.join(ROOT, 'build', 'blank_disk.raw');
const ppmInstallerPath = path.join(ROOT, 'screenshot_vbox_test.ppm');

// Crea disco da 2GB vuoto (all zeroes, come un disco nuovo di VirtualBox)
if (!fs.existsSync(blankDiskPath)) {
    const fd = fs.openSync(blankDiskPath, 'w');
    // Tronca a 2 GB (2 * 1024 * 1024 * 1024 = 2147483648 byte)
    fs.ftruncateSync(fd, 2147483648);
    fs.closeSync(fd);
    console.log('[TEST] Creato disco vuoto da 2GB per simulare VirtualBox');
}

console.log('[TEST] Avvio simulazione VirtualBox: 64MB RAM, Disco Primario 2GB Vuoto, Boot da NewOS_V1.iso...');

const qemu = spawn(qemuBin, [
    '-drive', `file=${blankDiskPath},format=raw,index=0,media=disk`,
    '-cdrom', isoPath,
    '-boot', 'd',
    '-m', '64M',
    '-vga', 'std',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4499,server,nowait',
    '-serial', `file:${path.join(ROOT, 'serial_vbox_test.log')}`
], { stdio: 'ignore' });

function sleep(ms) {
    return new Promise(res => setTimeout(res, ms));
}

setTimeout(() => {
    const client = net.createConnection({ port: 4499 }, async () => {
        console.log('Connesso al monitor QEMU...');
        await sleep(2000);

        // Seleziona opzione 2: "Installa NewOS V1"
        console.log('Invio tasto 2 per avviare Installazione NewOS V1...');
        client.write('sendkey 2\n');
        await sleep(3000);

        // Cattura screenshot della schermata installer con disco vuoto
        client.write(`screendump "${ppmInstallerPath.replace(/\\/g, '/')}"\n`);
        await sleep(1000);

        client.end();
        qemu.kill();

        console.log('[TEST] Screenshot catturato con successo!');

        // Converti PPM in PNG
        const { ppmToPng } = require('./ppm_to_png.js');
        if (ppmToPng && fs.existsSync(ppmInstallerPath)) {
            ppmToPng(ppmInstallerPath, path.join(ROOT, 'screenshot_vbox_test.png'));
            console.log('[TEST] Convertito screenshot in PNG: screenshot_vbox_test.png');
        }
        process.exit(0);
    });
}, 1500);
