const { spawn, execSync } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const isoPath = path.join(ROOT, 'NewOS_V1.iso');

if (fs.existsSync('serial_quick.log')) fs.unlinkSync('serial_quick.log');

const qemu = spawn(qemuBin, [
    '-cdrom', isoPath,
    '-m', '128M',
    '-vga', 'std',
    '-device', 'piix3-usb-uhci',
    '-device', 'qemu-xhci',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4556,server,nowait',
    '-serial', `file:${path.join(ROOT, 'serial_quick.log')}`
], { stdio: 'ignore' });

setTimeout(() => {
    const client = net.createConnection({ port: 4556 }, async () => {
        client.on('error', (e) => console.log('Client err:', e.message));
        console.log('Connected to monitor');
        // Bootscreen takes until ~4.5s. Wait until 5.5s
        await new Promise(r => setTimeout(r, 4000));
        console.log('Sending sendkey 6 (Direct Explorer)...');
        client.write('sendkey 6\n');
        await new Promise(r => setTimeout(r, 4000));

        const ppm = path.join(ROOT, 'quick_exp.ppm');
        const bmp = path.join(ROOT, 'quick_exp.bmp');
        const png = path.join(ROOT, 'quick_exp.png');
        client.write(`screendump ${ppm.replace(/\\/g, '/')}\n`);
        await new Promise(r => setTimeout(r, 1200));

        client.write('quit\n');
        await new Promise(r => setTimeout(r, 500));
        qemu.kill();

        if (fs.existsSync(ppm)) {
            execSync(`node tools/ppm2bmp.js "${ppm}" "${bmp}"`, { stdio: 'ignore' });
            execSync(`powershell -ExecutionPolicy Bypass -File tools/bmp2png.ps1 "${bmp}" "${png}"`, { stdio: 'ignore' });
            console.log('SUCCESS! Saved quick_exp.png, size:', fs.statSync(png).size);
        } else {
            console.log('PPM not created');
        }
        process.exit(0);
    });
}, 2000);
