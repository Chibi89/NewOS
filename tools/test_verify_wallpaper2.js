const { spawn, execSync } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const imgPath = path.join(ROOT, 'build', 'myos.img');
const brainDir = 'C:\\Users\\morga\\.gemini\\antigravity-ide\\brain\\be12af1d-c59c-4c02-b28b-947aabf1f4b4';

const ppmMenu = path.join(ROOT, 'test_ctx_menu.ppm');
const ppmDisplay = path.join(ROOT, 'test_display_open.ppm');
const ppmAfter = path.join(ROOT, 'test_wallpaper_after.ppm');

const qemu = spawn(qemuBin, [
    '-drive', `format=raw,file=${imgPath}`,
    '-m', '256M',
    '-vga', 'std',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4477,server,nowait',
    '-serial', `file:${path.join(ROOT, 'serial_wp2.log')}`
], { stdio: 'ignore' });

function sleep(ms) {
    return new Promise(res => setTimeout(res, ms));
}

setTimeout(() => {
    console.log('Connessione monitor 4477...');
    const client = net.createConnection({ port: 4477 }, async () => {
        console.log('Invio 1 per Live mode...');
        client.write('sendkey 1\n');
        await sleep(2000);

        // 1. Sposta mouse su desktop vuoto a (800, 300)
        // Partenza da (512, 384): dx = 800 - 512 = 288, dy = -(300 - 384) = +84
        console.log('Spostamento mouse su desktop vuoto (800, 300)...');
        client.write('mouse_move 288 84\n');
        await sleep(300);

        // 2. Click tasto destro (mouse_button 2)
        console.log('Tasto destro del mouse per aprire menu contestuale...');
        client.write('mouse_button 2\n');
        await sleep(100);
        client.write('mouse_button 0\n');
        await sleep(400);

        // Cattura menu contestuale aperto
        client.write(`screendump "${ppmMenu.replace(/\\/g, '/')}"\n`);
        await sleep(400);

        // 3. Sposta mouse su voce "Proprieta" (a x=850, y=340)
        // dx = 850 - 800 = 50, dy = -(340 - 300) = -40
        console.log('Spostamento su "Proprieta" e click...');
        client.write('mouse_move 50 -40\n');
        await sleep(200);
        client.write('mouse_button 1\n');
        await sleep(100);
        client.write('mouse_button 0\n');
        await sleep(800);

        // Cattura finestra Proprietà Schermo aperta
        client.write(`screendump "${ppmDisplay.replace(/\\/g, '/')}"\n`);
        await sleep(400);

        // 4. Seleziona sfondo "Bliss XP" dentro la finestra Proprietà Schermo
        // Finestra è a x=90, y=50, w=440, h=420.
        // Box sfondi: box_x = 90 + 4 + 14 = 108, box_y = 50 + 24 + 138 = 212
        // Bliss XP (indice 2, col 0, row 2): bx = 118, by = 212 + 12 + 2*32 = 288
        // Centro pulsante Bliss XP: x = 180, y = 302
        // Attuale mouse: (850, 340).
        // dx = 180 - 850 = -670, dy = -(302 - 340) = +38
        console.log('Spostamento su Bliss XP e click...');
        client.write('mouse_move -670 38\n');
        await sleep(200);
        client.write('mouse_button 1\n');
        await sleep(100);
        client.write('mouse_button 0\n');
        await sleep(400);

        // 5. Clicca su [ Applica ]
        // Applica è a x = 90 + 4 + 432 - 78 + 34 = 482, y = 50 + 24 + 392 - 34 + 12 = 444
        // Attuale mouse: (180, 302).
        // dx = 482 - 180 = 302, dy = -(444 - 302) = -142
        console.log('Spostamento su [ Applica ] e click...');
        client.write('mouse_move 302 -142\n');
        await sleep(200);
        client.write('mouse_button 1\n');
        await sleep(100);
        client.write('mouse_button 0\n');
        await sleep(800);

        // Cattura desktop con sfondo applicato!
        client.write(`screendump "${ppmAfter.replace(/\\/g, '/')}"\n`);
        await sleep(600);

        client.write('quit\n');
        await sleep(300);
        try { client.destroy(); } catch (e) {}
        try { qemu.kill(); } catch (e) {}

        if (fs.existsSync(ppmMenu)) {
            execSync(`node tools/ppm2bmp.js "${ppmMenu}" test_ctx_menu.bmp`, { stdio: 'inherit' });
            execSync(`powershell -ExecutionPolicy Bypass -File tools/bmp2png.ps1 test_ctx_menu.bmp "${brainDir}\\test_ctx_menu.png"`, { stdio: 'inherit' });
        }
        if (fs.existsSync(ppmDisplay)) {
            execSync(`node tools/ppm2bmp.js "${ppmDisplay}" test_display_open.bmp`, { stdio: 'inherit' });
            execSync(`powershell -ExecutionPolicy Bypass -File tools/bmp2png.ps1 test_display_open.bmp "${brainDir}\\test_display_open.png"`, { stdio: 'inherit' });
        }
        if (fs.existsSync(ppmAfter)) {
            execSync(`node tools/ppm2bmp.js "${ppmAfter}" test_wallpaper_after.bmp`, { stdio: 'inherit' });
            execSync(`powershell -ExecutionPolicy Bypass -File tools/bmp2png.ps1 test_wallpaper_after.bmp "${brainDir}\\test_wallpaper_after.png"`, { stdio: 'inherit' });
        }

        console.log('=== TEST PROPRIETA SCHERMO COMPLETATO! ===');
        process.exit(0);
    });
}, 2000);
