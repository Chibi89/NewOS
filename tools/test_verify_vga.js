const { spawn, execSync } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const isoPath = path.join(ROOT, 'NewOS_V1.iso');
const brainDir = 'C:\\Users\\morga\\.gemini\\antigravity-ide\\brain\\201a47cf-7636-4592-93f8-cb22ec54e1bd';

const ppmResTab = path.join(ROOT, 'test_res_tab.ppm');
const bmpResTab = path.join(ROOT, 'test_res_tab.bmp');
const pngResTab = path.join(brainDir, 'screenshot_vga_tab.png');

const ppmWide = path.join(ROOT, 'test_wide.ppm');
const bmpWide = path.join(ROOT, 'test_wide.bmp');
const pngWide = path.join(brainDir, 'screenshot_vga_1280x720.png');

console.log('[TEST VGA] Avvio QEMU con NewOS_V1.iso...');

const qemu = spawn(qemuBin, [
    '-cdrom', isoPath,
    '-m', '128M',
    '-vga', 'std',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4498,server,nowait',
    '-serial', `file:${path.join(ROOT, 'serial_vga_test.log')}`
], { stdio: 'ignore' });

function sleep(ms) {
    return new Promise(res => setTimeout(res, ms));
}

setTimeout(() => {
    const client = net.createConnection({ port: 4498 }, async () => {
        console.log('Connesso al monitor QEMU...');
        await sleep(1500);

        // 1. Seleziona 1 (Live desktop)
        console.log('Avvio Live Desktop (tasto 1)...');
        client.write('sendkey 1\n');
        await sleep(2500);

        // 2. Apri Prompt Comandi e lancia display per aprire Proprietà Schermo
        // Apri menu Start (Ctrl+Esc)
        console.log('Apertura menu Start...');
        client.write('sendkey ctrl-esc\n');
        await sleep(600);

        // Chiudi Start Menu
        client.write('sendkey esc\n');
        await sleep(500);

        // Clic tasto destro sul desktop a x=500, y=300 per aprire il menu contestuale
        console.log('Apertura menu contestuale Desktop...');
        // sendkey per muovere mouse e cliccare
        // In alternativa usiamo il comando del terminale
        // Apriamo il prompt dei comandi con doppio clic su icona Prompt Comandi (x=20+16=36, y=20+2*75+16=186)
        // Oppure inviamo tasti mouse
        client.write('mouse_move 36 186\n');
        await sleep(300);
        client.write('mouse_button 1\n');
        await sleep(100);
        client.write('mouse_button 0\n');
        await sleep(100);
        client.write('mouse_button 1\n');
        await sleep(100);
        client.write('mouse_button 0\n');
        await sleep(1000);

        // Nel prompt digitiamo "display" e premiamo invio
        console.log('Digitazione comando display nel terminale...');
        client.write('sendkey d\n'); await sleep(50);
        client.write('sendkey i\n'); await sleep(50);
        client.write('sendkey s\n'); await sleep(50);
        client.write('sendkey p\n'); await sleep(50);
        client.write('sendkey l\n'); await sleep(50);
        client.write('sendkey a\n'); await sleep(50);
        client.write('sendkey y\n'); await sleep(50);
        client.write('sendkey ret\n');
        await sleep(1200);

        // La finestra Proprietà Schermo è a x=90, y=50, w=440, h=420
        // Clicchiamo sul 3° tab "Risoluzione"
        // Tab 0: x=90+4+8 = 102
        // Tab 1: x=102 + 92 = 194
        // Tab 2: x=194 + 92 = 286, y=50+24+4+10 = 88
        console.log('Clic sul tab Risoluzione...');
        client.write('mouse_move 300 88\n');
        await sleep(300);
        client.write('mouse_button 1\n');
        await sleep(100);
        client.write('mouse_button 0\n');
        await sleep(800);

        // Cattura screenshot della scheda Risoluzione Schermo
        console.log('Cattura screenshot scheda Risoluzione...');
        client.write(`screendump "${ppmResTab.replace(/\\/g, '/')}"\n`);
        await sleep(1000);

        // Clic sul pulsante Risoluzione 1280x720 (colonna 0, riga 2)
        // In draw_res_tab:
        // box_x = 90 + 4 + 14 = 108
        // box_y = 50 + 24 + 138 = 212
        // col 0: bx = 108 + 10 = 118
        // row 2 (1280x720): by = 212 + 30 + 2*44 = 330
        console.log('Selezione modalita 1280x720...');
        client.write('mouse_move 160 350\n');
        await sleep(300);
        client.write('mouse_button 1\n');
        await sleep(100);
        client.write('mouse_button 0\n');
        await sleep(500);

        // Clic sul pulsante [ Applica ] a cx+cw-78 = 90+4+432-78 = 448, y = 50+24+392-34 = 432
        console.log('Clic su [ Applica ]...');
        client.write('mouse_move 480 444\n');
        await sleep(300);
        client.write('mouse_button 1\n');
        await sleep(100);
        client.write('mouse_button 0\n');
        await sleep(1500);

        // Cattura screenshot della nuova risoluzione 1280x720!
        console.log('Cattura screenshot nuova risoluzione 1280x720...');
        client.write(`screendump "${ppmWide.replace(/\\/g, '/')}"\n`);
        await sleep(1000);

        client.end();
        qemu.kill();

        // Converti i file PPM in BMP e PNG
        if (fs.existsSync(ppmResTab)) {
            execSync(`node tools/ppm2bmp.js "${ppmResTab}" "${bmpResTab}"`, { stdio: 'ignore' });
            execSync(`powershell -ExecutionPolicy Bypass -File tools/bmp2png.ps1 "${bmpResTab}" "${pngResTab}"`, { stdio: 'ignore' });
            console.log(`[OK] Generato screenshot: ${pngResTab}`);
        }
        if (fs.existsSync(ppmWide)) {
            execSync(`node tools/ppm2bmp.js "${ppmWide}" "${bmpWide}"`, { stdio: 'ignore' });
            execSync(`powershell -ExecutionPolicy Bypass -File tools/bmp2png.ps1 "${bmpWide}" "${pngWide}"`, { stdio: 'ignore' });
            console.log(`[OK] Generato screenshot: ${pngWide}`);
        }

        process.exit(0);
    });
}, 1500);
