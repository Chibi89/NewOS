const { spawn, execSync } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const isoPath = path.join(ROOT, 'NewOS_V1.iso');

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
    '-monitor', 'tcp:127.0.0.1:4570,server,nowait',
    '-serial', `file:${path.join(ROOT, 'serial_test_key.log')}`
], { stdio: 'ignore' });

setTimeout(() => {
    const client = net.createConnection({ port: 4570 }, async () => {
        client.on('error', (e) => console.log('Err:', e.message));
        console.log('Connected');
        await new Promise(r => setTimeout(r, 2000));

        // Invia sendkey 6 a intervalli per essere certi di colpire boot_menu_show
        for (let i = 0; i < 6; i++) {
            console.log(`Sending sendkey 6 attempt ${i + 1}...`);
            client.write('sendkey 6\n');
            await new Promise(r => setTimeout(r, 600));
        }

        console.log('Waiting for desktop and Explorer...');
        await new Promise(r => setTimeout(r, 3000));

        const ppm = path.join(ROOT, 'test_desktop_exp.ppm');
        const bmp = path.join(ROOT, 'test_desktop_exp.bmp');
        const png = path.join(ROOT, 'test_desktop_exp.png');
        client.write(`screendump ${ppm.replace(/\\/g, '/')}\n`);
        await new Promise(r => setTimeout(r, 1200));

        client.write('quit\n');
        await new Promise(r => setTimeout(r, 500));
        qemu.kill();

        if (fs.existsSync(ppm)) {
            execSync(`node tools/ppm2bmp.js "${ppm}" "${bmp}"`, { stdio: 'ignore' });
            execSync(`powershell -ExecutionPolicy Bypass -File tools/bmp2png.ps1 "${bmp}" "${png}"`, { stdio: 'ignore' });
            console.log('SUCCESS! Screenshot test_desktop_exp.png size:', fs.statSync(png).size);
        } else {
            console.log('PPM not created');
        }
        process.exit(0);
    });
}, 2000);
