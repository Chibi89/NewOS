const { spawn } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const imgPath = path.join(ROOT, 'myos.img');
const ppmPath = path.join(ROOT, 'screenshot_start_open.ppm');

if (fs.existsSync(ppmPath)) fs.unlinkSync(ppmPath);

const qemu = spawn(qemuBin, [
    '-drive', `format=raw,file=${imgPath}`,
    '-m', '256M',
    '-vga', 'std',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4468,server,nowait'
], { stdio: 'ignore' });

function sleep(ms) {
    return new Promise(res => setTimeout(res, ms));
}

setTimeout(async () => {
    let sentQuit = false;
    const client = net.createConnection({ port: 4468 }, async () => {
        // Spostiamo fino in fondo a sinistra e in basso
        for (let i = 0; i < 15; i++) {
            client.write('mouse_move -40 35\n');
            await sleep(40);
        }
        await sleep(100);

        // Ora spostiamo a destra di 30px per centrare il pulsante "Start" (x=2..62, y=742..766)
        client.write('mouse_move 30 -5\n');
        await sleep(150);

        // Click su Start
        client.write('mouse_button 1\n');
        client.write('mouse_move 0 0\n');
        await sleep(80);
        client.write('mouse_button 0\n');
        client.write('mouse_move 0 0\n');
        await sleep(500);

        client.write(`screendump "${ppmPath.replace(/\\/g, '/')}"\n`);
        await sleep(1000);

        sentQuit = true;
        client.write('quit\n');
        await sleep(400);

        try { client.destroy(); } catch (e) {}
        try { qemu.kill(); } catch (e) {}

        process.exit(0);
    });
}, 2500);
