const { spawn } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const imgPath = path.join(ROOT, 'myos.img');
const ppmPath = path.join(ROOT, 'screenshot_power_tab2.ppm');

if (fs.existsSync(ppmPath)) fs.unlinkSync(ppmPath);

const qemu = spawn(qemuBin, [
    '-drive', `format=raw,file=${imgPath}`,
    '-m', '256M',
    '-vga', 'std',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4471,server,nowait'
], { stdio: 'ignore' });

function sleep(ms) {
    return new Promise(res => setTimeout(res, ms));
}

setTimeout(async () => {
    let sentQuit = false;
    const client = net.createConnection({ port: 4471 }, async () => {
        // 1. Spostamento su System Tray
        for (let i = 0; i < 12; i++) {
            client.write('mouse_move 40 32\n');
            await sleep(40);
        }
        await sleep(150);

        // Click sulla tray per aprire la finestra
        client.write('mouse_button 1\n');
        client.write('mouse_move 0 0\n');
        await sleep(60);
        client.write('mouse_button 0\n');
        client.write('mouse_move 0 0\n');
        await sleep(500);

        // 2. Finestra si apre a (160, 80).
        // Tab "Alimentazione" è a x = 160 + 190 = 350, y = 80 + 34 = 114.
        // Attuale posizione mouse: x = 992, y = 767.
        // dx = 350 - 992 = -642 (circa 16 passi da -40)
        // dy = 114 - 767 = -653 (verso l'alto: circa 16 passi da -41)
        for (let i = 0; i < 16; i++) {
            client.write('mouse_move -40 -41\n');
            await sleep(30);
        }
        await sleep(200);

        // Click sulla scheda "Alimentazione"
        client.write('mouse_button 1\n');
        client.write('mouse_move 0 0\n');
        await sleep(60);
        client.write('mouse_button 0\n');
        client.write('mouse_move 0 0\n');
        await sleep(500);

        // Cattura screenshot della scheda Alimentazione
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
