const { spawn } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const imgPath = path.join(ROOT, 'build', 'myos.img');
const ppmPath = path.join(ROOT, 'screenshot_boot_menu.ppm');
const bmpPath = path.join(ROOT, 'screenshot_boot_menu.bmp');

if (fs.existsSync(ppmPath)) fs.unlinkSync(ppmPath);

console.log('Avvio QEMU per test del Boot Menu...');
const qemu = spawn(qemuBin, [
    '-drive', `format=raw,file=${imgPath}`,
    '-m', '256M',
    '-vga', 'std',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4445,server,nowait',
    '-serial', `file:${path.join(ROOT, 'serial.log')}`,
    '-d', 'guest_errors',
    '-D', path.join(ROOT, 'qemu_debug.log')
], { stdio: 'ignore' });

setTimeout(() => {
    console.log('Connessione al QEMU monitor su porta 4445...');
    let sentQuit = false;
    const client = net.createConnection({ port: 4445 }, () => {
        console.log('Cattura screenshot Boot Menu...');
        client.write(`screendump "${ppmPath.replace(/\\/g, '/')}"\n`);
        setTimeout(() => {
            sentQuit = true;
            client.write('quit\n');
            setTimeout(() => {
                try { client.destroy(); } catch (e) {}
                try { qemu.kill(); } catch (e) {}
                if (fs.existsSync(ppmPath)) {
                    console.log(`[OK] Screenshot catturato (${fs.statSync(ppmPath).size} bytes)`);
                } else {
                    console.error('[ERRORE] Screenshot non presente');
                }
                process.exit(0);
            }, 600);
        }, 1200);
    });

    client.on('error', (err) => {
        if (!sentQuit) {
            console.error('Errore monitor:', err.message);
            try { qemu.kill(); } catch (e) {}
            process.exit(1);
        }
    });
}, 2200);
