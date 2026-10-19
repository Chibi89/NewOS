const { spawn, execSync } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const imgPath = path.join(ROOT, 'build', 'myos.img');
const brainDir = 'C:\\Users\\morga\\.gemini\\antigravity-ide\\brain\\be12af1d-c59c-4c02-b28b-947aabf1f4b4';

const ppmBrowse = path.join(ROOT, 'test_wp_browse.ppm');
const ppmHero = path.join(ROOT, 'test_wp_hero.ppm');

const qemu = spawn(qemuBin, [
    '-drive', `format=raw,file=${imgPath}`,
    '-m', '256M',
    '-vga', 'std',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4480,server,nowait',
    '-serial', `file:${path.join(ROOT, 'serial_wp5.log')}`
], { stdio: 'ignore' });

function sleep(ms) {
    return new Promise(res => setTimeout(res, ms));
}

setTimeout(() => {
    console.log('Connessione monitor 4480...');
    const client = net.createConnection({ port: 4480 }, async () => {
        async function moveMouseBy(dx, dy) {
            const steps = Math.max(Math.ceil(Math.abs(dx) / 40), Math.ceil(Math.abs(dy) / 40), 1);
            const stepX = Math.round(dx / steps);
            const stepY = Math.round(dy / steps);
            for (let i = 0; i < steps; i++) {
                client.write(`mouse_move ${stepX} ${stepY}\n`);
                await sleep(35);
            }
            await sleep(100);
        }

        console.log('Invio 1 per Live mode...');
        client.write('sendkey 1\n');
        await sleep(2000);

        // 1. Sposta mouse su desktop vuoto a (850, 400)
        await moveMouseBy(850 - 512, 400 - 384);

        // 2. Click tasto destro
        client.write('mouse_button 2\n');
        client.write('mouse_move 0 0\n');
        await sleep(80);
        client.write('mouse_button 0\n');
        client.write('mouse_move 0 0\n');
        await sleep(400);

        // 3. Sposta mouse su "Personalizza..." e click
        await moveMouseBy(890 - 850, 438 - 400);
        client.write('mouse_button 1\n');
        client.write('mouse_move 0 0\n');
        await sleep(80);
        client.write('mouse_button 0\n');
        client.write('mouse_move 0 0\n');
        await sleep(1000);

        // 4. Click su [ Sfoglia... ] a x = 145, y = 444
        // Mouse attuale a (890, 438)
        console.log('Click su [ Sfoglia... ]...');
        await moveMouseBy(145 - 890, 444 - 438);
        client.write('mouse_button 1\n');
        client.write('mouse_move 0 0\n');
        await sleep(80);
        client.write('mouse_button 0\n');
        client.write('mouse_move 0 0\n');
        await sleep(1200);

        // Cattura Explorer aperto su SYSTEM/WALLPAPERS
        client.write(`screendump "${ppmBrowse.replace(/\\/g, '/')}"\n`);
        await sleep(500);

        // Finestra Explorer aperta su SYSTEM/WALLPAPERS:
        // x = 80, y = 45, w = 600, h = 430
        // Item 0 (HERO.BMP):
        // main_x = 80 + 4 + 160 + 4 = 248
        // body_y = 45 + 24 + 74 = 143
        // Col 0, Row 0: x = 248 + 8 + 40 = 296, y = 143 + 8 + 30 = 181
        // Attuale mouse: (145, 444)
        console.log('Doppio click su HERO.BMP in Explorer...');
        await moveMouseBy(296 - 145, 181 - 444);
        // Primo click
        client.write('mouse_button 1\n');
        client.write('mouse_move 0 0\n');
        await sleep(60);
        client.write('mouse_button 0\n');
        client.write('mouse_move 0 0\n');
        await sleep(80);
        // Secondo click (< 50 ticks = doppio click)
        client.write('mouse_button 1\n');
        client.write('mouse_move 0 0\n');
        await sleep(60);
        client.write('mouse_button 0\n');
        client.write('mouse_move 0 0\n');
        await sleep(1500);

        // Cattura desktop dopo applicazione di HERO.BMP
        client.write(`screendump "${ppmHero.replace(/\\/g, '/')}"\n`);
        await sleep(800);

        client.write('quit\n');
        await sleep(300);
        try { client.destroy(); } catch (e) {}
        try { qemu.kill(); } catch (e) {}

        if (fs.existsSync(ppmBrowse)) {
            execSync(`node tools/ppm2bmp.js "${ppmBrowse}" test_wp_browse.bmp`, { stdio: 'inherit' });
            execSync(`powershell -ExecutionPolicy Bypass -File tools/bmp2png.ps1 test_wp_browse.bmp "${brainDir}\\test_wp_browse.png"`, { stdio: 'inherit' });
        }
        if (fs.existsSync(ppmHero)) {
            execSync(`node tools/ppm2bmp.js "${ppmHero}" test_wp_hero.bmp`, { stdio: 'inherit' });
            execSync(`powershell -ExecutionPolicy Bypass -File tools/bmp2png.ps1 test_wp_hero.bmp "${brainDir}\\test_wp_hero.png"`, { stdio: 'inherit' });
        }

        console.log('=== TEST SFOGLIA & HERO COMPLETATO! ===');
        process.exit(0);
    });
}, 2000);
