const { spawn } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const imgPath = path.join(ROOT, 'myos.img');
const ppmPath = path.join(ROOT, 'screenshot_shutdown_dialog.ppm');

if (fs.existsSync(ppmPath)) fs.unlinkSync(ppmPath);

const qemu = spawn(qemuBin, [
    '-drive', `format=raw,file=${imgPath}`,
    '-m', '256M',
    '-vga', 'std',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4469,server,nowait'
], { stdio: 'ignore' });

function sleep(ms) {
    return new Promise(res => setTimeout(res, ms));
}

setTimeout(async () => {
    let sentQuit = false;
    const client = net.createConnection({ port: 4469 }, async () => {
        // 1. Spostiamo a Start (x=30, y=755)
        for (let i = 0; i < 15; i++) {
            client.write('mouse_move -40 35\n');
            await sleep(40);
        }
        await sleep(100);
        client.write('mouse_move 30 -5\n');
        await sleep(150);

        // Click su Start
        console.log('Click su Start...');
        client.write('mouse_button 1\n');
        client.write('mouse_move 0 0\n');
        await sleep(80);
        client.write('mouse_button 0\n');
        client.write('mouse_move 0 0\n');
        await sleep(400);

        // 2. Spostiamo su "Arresta il sistema..." (iy = 700, x = 70)
        // Dal punto attuale (30, 755): dx = +40, dy = -55 (verso l'alto)
        console.log('Spostamento su "Arresta il sistema..."...');
        client.write('mouse_move 40 -55\n');
        await sleep(300);

        // Click su Arresta il sistema...
        console.log('Click su "Arresta il sistema..."...');
        client.write('mouse_button 1\n');
        client.write('mouse_move 0 0\n');
        await sleep(80);
        client.write('mouse_button 0\n');
        client.write('mouse_move 0 0\n');
        await sleep(600);

        // Cattura screenshot della finestra di dialogo Arresta il sistema
        console.log('Cattura screenshot dialogo arresto...');
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
