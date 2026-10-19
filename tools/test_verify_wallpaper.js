const { spawn, execSync } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const imgPath = path.join(ROOT, 'build', 'myos.img');
const brainDir = 'C:\\Users\\morga\\.gemini\\antigravity-ide\\brain\\be12af1d-c59c-4c02-b28b-947aabf1f4b4';

const ppm1 = path.join(ROOT, 'test_wp_1_boot.ppm');
const ppm2 = path.join(ROOT, 'test_wp_2_props.ppm');
const ppm3 = path.join(ROOT, 'test_wp_3_changed.ppm');

const qemu = spawn(qemuBin, [
    '-drive', `format=raw,file=${imgPath}`,
    '-m', '256M',
    '-vga', 'std',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4466,server,nowait',
    '-serial', `file:${path.join(ROOT, 'serial_wp.log')}`
], { stdio: 'ignore' });

function sleep(ms) {
    return new Promise(res => setTimeout(res, ms));
}

let curX = 512, curY = 384;

async function moveMouseTo(client, tx, ty) {
    while (curX !== tx || curY !== ty) {
        let dx = tx - curX;
        let dy = ty - curY;
        if (dx > 40) dx = 40;
        if (dx < -40) dx = -40;
        if (dy > 40) dy = 40;
        if (dy < -40) dy = -40;
        // In QEMU, negative dy moves down on screen
        client.write(`mouse_move ${dx} ${-dy}\n`);
        curX += dx;
        curY += dy;
        await sleep(25);
    }
    await sleep(80);
}

async function mouseClick(client, button = 1) {
    client.write(`mouse_button ${button}\n`);
    client.write(`mouse_move 0 0\n`);
    await sleep(100);
    client.write(`mouse_button 0\n`);
    client.write(`mouse_move 0 0\n`);
    await sleep(150);
}

async function mouseDoubleClick(client) {
    await mouseClick(client, 1);
    await sleep(60);
    await mouseClick(client, 1);
    await sleep(300);
}

setTimeout(async () => {
    console.log('Connessione a QEMU...');
    const client = net.createConnection({ port: 4466 }, async () => {
        console.log('Boot menu -> selezione 1 (Live)...');
        client.write('sendkey 1\n');
        await sleep(2000);

        // Cattura boot iniziale
        client.write(`screendump "${ppm1.replace(/\\/g, '/')}"\n`);
        await sleep(500);

        // Chiudi la finestra "Risorse del computer" (a x=80..680, y=45..475)
        // Pulsante di chiusura [X] a x = 80 + 600 - 14 = 666, y = 45 + 10 = 55
        console.log('Chiusura finestra Risorse del computer...');
        await moveMouseTo(client, 666, 55);
        await mouseClick(client, 1);
        await sleep(500);

        // Ora apriamo Proprietà Schermo tramite clic destro sul desktop libero a (x=500, y=300)
        console.log('Tasto destro sul desktop...');
        await moveMouseTo(client, 500, 300);
        await mouseClick(client, 2); // Tasto destro
        await sleep(400);

        // Nel menu contestuale a (500, 300):
        // "Proprieta" è a y = 300 + 40 = 340, x = 500 + 40 = 540
        console.log('Click su Proprieta...');
        await moveMouseTo(client, 540, 340);
        await mouseClick(client, 1);
        await sleep(800);

        // Cattura Proprietà Schermo aperta
        client.write(`screendump "${ppm2.replace(/\\/g, '/')}"\n`);
        await sleep(500);

        // Finestra "Proprietà - Schermo" è a x=90, y=50, w=440, h=420
        // Sfondo desktop box:
        // box_x = 90 + 4 + 14 = 108
        // box_y = 50 + 24 + 138 = 212
        // Elementi (col 0..1, row 0..4):
        // Per riga 2 ("Bliss XP"):
        // col = 2 / 5 = 0, row = 2 % 5 = 2
        // bx = 108 + 10 = 118, by = 212 + 12 + 2*32 = 288
        // Clicchiamo su Bliss XP a x = 180, y = 300
        console.log('Selezione Bliss XP...');
        await moveMouseTo(client, 180, 300);
        await mouseClick(client, 1);
        await sleep(300);

        // Clicchiamo su [ Applica ] a x = 90 + 4 + 432 - 78 + 34 = 482, y = 50 + 24 + 392 - 34 + 12 = 444
        console.log('Click su Applica...');
        await moveMouseTo(client, 482, 444);
        await mouseClick(client, 1);
        await sleep(800);

        // Cattura desktop dopo applicazione Bliss XP
        client.write(`screendump "${ppm3.replace(/\\/g, '/')}"\n`);
        await sleep(500);

        client.write('quit\n');
        await sleep(300);
        try { client.destroy(); } catch (e) {}
        try { qemu.kill(); } catch (e) {}

        // Converti in PNG
        execSync(`node tools/ppm2bmp.js "${ppm1}" test_wp_1_boot.bmp`, { stdio: 'inherit' });
        execSync(`powershell -ExecutionPolicy Bypass -File tools/bmp2png.ps1 test_wp_1_boot.bmp "${brainDir}\\test_wp_1_boot.png"`, { stdio: 'inherit' });

        execSync(`node tools/ppm2bmp.js "${ppm2}" test_wp_2_props.ppm`, { stdio: 'inherit' });
        execSync(`powershell -ExecutionPolicy Bypass -File tools/bmp2png.ps1 test_wp_2_props.ppm "${brainDir}\\test_wp_2_props.png"`, { stdio: 'inherit' });

        execSync(`node tools/ppm2bmp.js "${ppm3}" test_wp_3_changed.ppm`, { stdio: 'inherit' });
        execSync(`powershell -ExecutionPolicy Bypass -File tools/bmp2png.ps1 test_wp_3_changed.ppm "${brainDir}\\test_wp_3_changed.png"`, { stdio: 'inherit' });

        console.log('Test wallpaper completato con successo!');
        process.exit(0);
    });

    client.on('error', (err) => {
        console.error('Client err:', err);
        try { qemu.kill(); } catch (e) {}
        process.exit(1);
    });
}, 2000);
