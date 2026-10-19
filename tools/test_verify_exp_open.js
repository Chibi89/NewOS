const { spawn, execSync } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const isoPath = path.join(ROOT, 'NewOS_V1.iso');

if (fs.existsSync('serial_exp_verify.log')) fs.unlinkSync('serial_exp_verify.log');

const qemu = spawn(qemuBin, [
    '-cdrom', isoPath,
    '-m', '128M',
    '-vga', 'std',
    '-device', 'piix3-usb-uhci',
    '-device', 'qemu-xhci',
    '-netdev', 'user,id=net0',
    '-device', 'e1000,netdev=net0',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4560,server,nowait',
    '-serial', `file:${path.join(ROOT, 'serial_exp_verify.log')}`
], { stdio: 'ignore' });

setTimeout(() => {
    const client = net.createConnection({ port: 4560 }, async () => {
        client.on('error', (e) => console.log('Err:', e.message));
        console.log('Connected to monitor');
        await new Promise(r => setTimeout(r, 2000));
        console.log('Sending sendkey 4 (Prompt CLI)...');
        client.write('sendkey 4\n');
        await new Promise(r => setTimeout(r, 3500));

        console.log('Typing explorer...');
        for (const c of 'explorer') {
            client.write(`sendkey ${c}\n`);
            await new Promise(r => setTimeout(r, 80));
        }
        client.write('sendkey ret\n');
        await new Promise(r => setTimeout(r, 2500));

        const ppm = path.join(ROOT, 'verify_exp_open.ppm');
        const bmp = path.join(ROOT, 'verify_exp_open.bmp');
        const png = path.join(ROOT, 'verify_exp_open.png');
        client.write(`screendump ${ppm.replace(/\\/g, '/')}\n`);
        await new Promise(r => setTimeout(r, 1200));

        client.write('quit\n');
        await new Promise(r => setTimeout(r, 500));
        qemu.kill();

        if (fs.existsSync(ppm)) {
            execSync(`node tools/ppm2bmp.js "${ppm}" "${bmp}"`, { stdio: 'ignore' });
            execSync(`powershell -ExecutionPolicy Bypass -File tools/bmp2png.ps1 "${bmp}" "${png}"`, { stdio: 'ignore' });
            console.log('SUCCESS! Screenshot verify_exp_open.png size:', fs.statSync(png).size);
        } else {
            console.log('PPM not created');
        }
        process.exit(0);
    });
}, 2000);
