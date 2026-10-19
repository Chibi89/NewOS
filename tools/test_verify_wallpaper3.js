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
    '-monitor', 'tcp:127.0.0.1:4478,server,nowait',
    '-serial', `file:${path.join(ROOT, 'serial_wp3.log')}`
], { stdio: 'ignore' });

function sleep(ms) {
    return new Promise(res => setTimeout(res, ms));
}

setTimeout(() => {
    console.log('Connessione monitor 4478...');
    const client = net.createConnection({ port: 4478 }, async () => {
        async function moveMouseBy(dx, dy) {
            const steps = Math.max(Math.ceil(Math.abs(dx) / 50), Math.ceil(Math.abs(dy) / 50), 1);
            const stepX = Math.round(dx / steps);
            const stepY = Math.round(dy / steps);
            for (let i = 0; i < steps; i++) {
                client.write(`mouse_move ${stepX} ${stepY}\n`);
                await sleep(40);
            }
            await sleep(100);
        }

        console.log('Invio 1 per Live mode...');
        client.write('sendkey 1\n');
        await sleep(2000);

        // 1. Sposta mouse su desktop vuoto a (850, 400)
        // dx = 850 - 512 = 338, dy = -(400 - 384) = -16
        console.log('Spostamento mouse su desktop vuoto (850, 400)...');
        await moveMouseBy(338, -16);

        // 2. Click tasto destro
        console.log('Tasto destro del mouse per aprire menu contestuale...');
        client.write('mouse_button 2\n');
        client.write('mouse_move 0 0\n');
        await sleep(80);
        client.write('mouse_button 0\n');
        client.write('mouse_move 0 0\n');
        await sleep(400);

        // Cattura menu contestuale aperto
        client.write(`screendump "${ppmMenu.replace(/\\/g, '/')}"\n`);
        await sleep(400);

        // 3. Sposta mouse su voce "Personalizza..." (a x=890, y=440)
        // dx = 40, dy = -(440 - 400) = -40
        console.log('Spostamento su "Personalizza..." e click...');
        await moveMouseBy(40, -40);
        client.write('mouse_button 1\n');
        client.write('mouse_move 0 0\n');
        await sleep(80);
        client.write('mouse_button 0\n');
        client.write('mouse_move 0 0\n');
        await sleep(1000);

        // Cattura finestra Proprietà Schermo aperta
        client.write(`screendump "${ppmDisplay.replace(/\\/g, '/')}"\n`);
        await sleep(400);

        // 4. Seleziona sfondo "Bliss XP" a x = 180, y = 302
        // Mouse attuale a (890, 440)
        // dx = 180 - 890 = -710
        // dy = -(302 - 440) = +138
        console.log('Spostamento su Bliss XP e click...');
        await moveMouseBy(-710, 138);
        client.write('mouse_button 1\n');
        client.write('mouse_move 0 0\n');
        await sleep(80);
        client.write('mouse_button 0\n');
        client.write('mouse_move 0 0\n');
        await sleep(600);

        // 5. Clicca su [ Applica ] a x = 482, y = 444
        // Mouse attuale a (180, 302)
        // dx = 482 - 180 = 302
        // dy = -(444 - 302) = -142
        console.log('Spostamento su [ Applica ] e click...');
        await moveMouseBy(302, -142);
        client.write('mouse_button 1\n');
        client.write('mouse_move 0 0\n');
        await sleep(80);
        client.write('mouse_button 0\n');
        client.write('mouse_move 0 0\n');
        await sleep(1000);

        // Cattura desktop con sfondo applicato!
        client.write(`screendump "${ppmAfter.replace(/\\/g, '/')}"\n`);
        await sleep(800);

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
