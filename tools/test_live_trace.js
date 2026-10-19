const { spawn, execSync } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const isoPath = path.join(ROOT, 'NewOS_V1.iso');
const logFile = path.join(ROOT, 'serial_live_trace.log');

if (fs.existsSync(logFile)) fs.unlinkSync(logFile);

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
    '-monitor', 'tcp:127.0.0.1:4588,server,nowait',
    '-serial', `file:${logFile}`
], { stdio: 'ignore' });

setTimeout(() => {
    const client = net.createConnection({ port: 4588 }, async () => {
        client.on('error', (e) => console.log('Err:', e.message));
        console.log('[TRACE] Monitor connesso.');

        let keySent = false;

        for (let i = 0; i < 80; i++) {
            await new Promise(r => setTimeout(r, 200));
            if (fs.existsSync(logFile)) {
                const content = fs.readFileSync(logFile, 'utf8');
                if (!keySent && content.includes('[BOOT_MENU] Pronto!')) {
                    keySent = true;
                    console.log(`[TRACE] Boot Menu pronto! Invio sendkey 6...`);
                    client.write('sendkey 6\n');
                }
                if (content.includes('[BOOT_MENU] Scelto 6') || content.includes('5S7')) {
                    console.log('[TRACE] Scelta 6 accettata e Desktop raggiunto!');
                    break;
                }
            }
        }

        console.log('[TRACE] Attesa rendering desktop ed Explorer (3.5s)...');
        await new Promise(r => setTimeout(r, 3500));

        const ppm = path.join(ROOT, 'desktop_exp_success.ppm');
        const bmp = path.join(ROOT, 'desktop_exp_success.bmp');
        const png = path.join(ROOT, 'desktop_exp_success.png');
        client.write(`screendump ${ppm.replace(/\\/g, '/')}\n`);
        await new Promise(r => setTimeout(r, 1200));

        client.write('quit\n');
        await new Promise(r => setTimeout(r, 500));
        qemu.kill();

        if (fs.existsSync(ppm)) {
            execSync(`node tools/ppm2bmp.js "${ppm}" "${bmp}"`, { stdio: 'ignore' });
            execSync(`powershell -ExecutionPolicy Bypass -File tools/bmp2png.ps1 "${bmp}" "${png}"`, { stdio: 'ignore' });
            console.log('SUCCESS! Screenshot desktop_exp_success.png size:', fs.statSync(png).size);
        } else {
            console.log('PPM not created');
        }
        process.exit(0);
    });
}, 2000);
