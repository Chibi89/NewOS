const { spawn } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const imgPath = path.join(ROOT, 'myos.img');
const ppmPath = path.join(ROOT, 'screenshot_power_props.ppm');

if (fs.existsSync(ppmPath)) fs.unlinkSync(ppmPath);

const qemu = spawn(qemuBin, [
    '-drive', `format=raw,file=${imgPath}`,
    '-m', '256M',
    '-vga', 'std',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4470,server,nowait'
], { stdio: 'ignore' });

function sleep(ms) {
    return new Promise(res => setTimeout(res, ms));
}

setTimeout(async () => {
    let sentQuit = false;
    const client = net.createConnection({ port: 4470 }, async () => {
        // Spostiamo il mouse all'angolo inferiore destro (System Tray a x=950, y=755)
        // Mouse parte da (512, 384)
        // dx = 950 - 512 = +438
        // dy = 755 - 384 = +371 (in basso)
        console.log('Spostamento su System Tray...');
        for (let i = 0; i < 12; i++) {
            client.write('mouse_move 40 32\n');
            await sleep(40);
        }
        await sleep(200);

        // Click sulla tray (icona batteria / orologio)
        console.log('Click su System Tray per aprire Opzioni Risparmio Energia...');
        client.write('mouse_button 1\n');
        client.write('mouse_move 0 0\n');
        await sleep(80);
        client.write('mouse_button 0\n');
        client.write('mouse_move 0 0\n');
        await sleep(600);

        // Cattura screenshot della finestra Proprietà - Opzioni risparmio energia
        console.log('Cattura screenshot Opzioni Risparmio Energia...');
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
