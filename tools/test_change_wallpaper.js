const { spawn } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const imgPath = path.join(ROOT, 'build', 'myos.img');
const ppmBefore = path.join(ROOT, 'screenshot_wp_before.ppm');
const ppmDisplay = path.join(ROOT, 'screenshot_wp_display.ppm');
const ppmAfter = path.join(ROOT, 'screenshot_wp_after.ppm');

console.log('Avvio QEMU per test cambio wallpaper...');
const qemu = spawn(qemuBin, [
    '-drive', `format=raw,file=${imgPath}`,
    '-m', '256M',
    '-vga', 'std',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4451,server,nowait',
    '-serial', `file:${path.join(ROOT, 'serial.log')}`,
    '-d', 'guest_errors',
    '-D', path.join(ROOT, 'qemu_debug.log')
], { stdio: 'ignore' });

function sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

setTimeout(async () => {
    console.log('Connessione monitor 4451...');
    const client = net.createConnection({ port: 4451 });
    client.on('error', (err) => console.error('Monitor err:', err.message));
    await sleep(200);

    // Boot menu -> 1 (Live)
    client.write('sendkey 1\n');
    await sleep(1500);

    // Cattura desktop iniziale
    client.write(`screendump "${ppmBefore.replace(/\\/g, '/')}"\n`);
    await sleep(500);

    // Apri Proprietà Schermo tramite clic destro sul desktop (a x=500, y=200)
    // Tasto destro del mouse in QEMU:
    // mouse_button 2 -> tasto destro premuto
    // mouse_button 0 -> rilasciato
    console.log('Tasto destro sul desktop...');
    client.write('mouse_move -1200 1200\n'); // reset a (0,0)
    await sleep(100);
    client.write('mouse_move 500 -200\n'); // a (500, 200)
    await sleep(150);
    client.write('mouse_button 2\n');
    await sleep(100);
    client.write('mouse_button 0\n');
    await sleep(500);

    // Nel menu contestuale, "Proprieta" e' a (ctx_x + 20, ctx_y + 40) = (520, 240)
    console.log('Click su Proprieta nel menu contestuale...');
    client.write('mouse_move 20 -40\n'); // da 500,200 a 520,240
    await sleep(150);
    client.write('mouse_button 1\n');
    await sleep(100);
    client.write('mouse_button 0\n');
    await sleep(800);

    // Cattura finestra Proprietà Schermo
    client.write(`screendump "${ppmDisplay.replace(/\\/g, '/')}"\n`);
    await sleep(500);

    // Clicchiamo su uno sfondo, ad esempio "Bliss XP" (indice 2, colonna 0, riga 2)
    // Finestra Schermo: x=90, y=50, width=440, height=420
    // box_x = 90 + 4 + 14 = 108
    // box_y = 50 + 24 + 138 = 212
    // col 0, row 2: bx = 108 + 10 = 118, by = 212 + 14 + 2*38 = 302
    // Clicchiamo a x=150, y=315
    console.log('Selezione sfondo Bliss XP...');
    client.write('mouse_move -1200 1200\n');
    await sleep(100);
    client.write('mouse_move 150 -315\n');
    await sleep(150);
    client.write('mouse_button 1\n');
    await sleep(100);
    client.write('mouse_button 0\n');
    await sleep(400);

    // Clicchiamo sul pulsante "OK" (x = 90 + 4 + 432 - 200 = 326, y = 50 + 24 + 392 - 34 + 10 = 442)
    console.log('Click su OK...');
    client.write('mouse_move -1200 1200\n');
    await sleep(100);
    client.write('mouse_move 326 -442\n');
    await sleep(150);
    client.write('mouse_button 1\n');
    await sleep(100);
    client.write('mouse_button 0\n');
    await sleep(800);

    // Cattura desktop finale
    client.write(`screendump "${ppmAfter.replace(/\\/g, '/')}"\n`);
    await sleep(500);

    client.write('quit\n');
    await sleep(400);
    try { client.destroy(); } catch (e) {}
    try { qemu.kill(); } catch (e) {}

    const { execSync } = require('child_process');
    const brainDir = 'C:\\Users\\morga\\.gemini\\antigravity-ide\\brain\\be12af1d-c59c-4c02-b28b-947aabf1f4b4';
    execSync(`node tools/ppm2bmp.js screenshot_wp_display.ppm screenshot_wp_display.bmp`, { stdio: 'inherit' });
    execSync(`powershell -ExecutionPolicy Bypass -File tools/bmp2png.ps1 screenshot_wp_display.bmp "${brainDir}\\screenshot_wp_display.png"`, { stdio: 'inherit' });
    execSync(`node tools/ppm2bmp.js screenshot_wp_after.ppm screenshot_wp_after.bmp`, { stdio: 'inherit' });
    execSync(`powershell -ExecutionPolicy Bypass -File tools/bmp2png.ps1 screenshot_wp_after.bmp "${brainDir}\\screenshot_wp_after.png"`, { stdio: 'inherit' });

    console.log('Test cambio wallpaper completato!');
    process.exit(0);
}, 1500);
