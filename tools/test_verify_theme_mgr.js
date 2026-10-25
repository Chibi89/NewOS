const { spawn, execSync } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const imgPath = path.join(ROOT, 'myos.img');
const brainDir = 'C:\\Users\\morga\\.gemini\\antigravity-ide\\brain\\b87b31dd-8d16-4d98-8cff-28574ce5e3ae';

const ppmThemeTab = path.join(ROOT, 'test_theme_tab.ppm');
const bmpThemeTab = path.join(ROOT, 'test_theme_tab.bmp');
const pngThemeTab = path.join(brainDir, 'screenshot_theme_tab.png');

if (fs.existsSync(ppmThemeTab)) fs.unlinkSync(ppmThemeTab);
if (fs.existsSync(bmpThemeTab)) fs.unlinkSync(bmpThemeTab);

console.log('[TEST THEMES] Avvio QEMU con myos.img (C: exFAT)...');

const qemu = spawn(qemuBin, [
    '-drive', `format=raw,file=${imgPath}`,
    '-m', '256M',
    '-vga', 'std',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4496,server,nowait',
    '-serial', `file:${path.join(ROOT, 'serial_theme_test.log')}`
], { stdio: 'ignore' });

function sleep(ms) {
    return new Promise(res => setTimeout(res, ms));
}

setTimeout(() => {
    const client = net.createConnection({ port: 4496 }, async () => {
        console.log('Connesso al monitor QEMU...');
        // Attendi che il BootScreen termini e compaia il Boot Menu
        await sleep(6500);

        // 1. Invio '3' per avviare direttamente Proprietà Schermo dal Boot Menu
        console.log('Invio scelta 3 (Proprieta Schermo)...');
        client.write('sendkey 3\n');
        await sleep(4500);

        // 2. Invio '2' per passare alla scheda "Tema"
        console.log('Invio tasto 2 per attivare la scheda Tema...');
        client.write('sendkey 2\n');
        await sleep(2000);

        // 3. Invio tasto 'w' tre volte per scorrere verso l'alto con la scrollbar
        console.log('Invio tasti scroll in alto (w)...');
        client.write('sendkey w\n');
        await sleep(400);
        client.write('sendkey w\n');
        await sleep(400);
        client.write('sendkey w\n');
        await sleep(600);

        // 4. Invio tasto 'd' tre volte per selezionare il tema Windows 11 Fluent Glass Light
        console.log('Invio tasti d per selezionare tema Windows 11 Fluent Glass Light...');
        client.write('sendkey d\n');
        await sleep(400);
        client.write('sendkey d\n');
        await sleep(400);
        client.write('sendkey d\n');
        await sleep(1500);

        // 5. Cattura screenshot per verificare l'anteprima aggiornata con tema chiaro
        console.log('Cattura screenshot anteprima aggiornata...');
        client.write(`screendump "${ppmThemeTab.replace(/\\/g, '/')}"\n`);
        await sleep(2500);

        client.end();
        qemu.kill();

        // Converti il file PPM in PNG
        if (fs.existsSync(ppmThemeTab)) {
            try {
                execSync(`node tools/ppm2bmp.js "${ppmThemeTab}" "${bmpThemeTab}"`, { stdio: 'inherit' });
                execSync(`powershell -ExecutionPolicy Bypass -File tools/bmp2png.ps1 "${bmpThemeTab}" "${pngThemeTab}"`, { stdio: 'inherit' });
                console.log(`[OK] Generato screenshot: ${pngThemeTab}`);
            } catch (err) {
                console.error('[ERRORE] Conversione immagine fallita:', err);
            }
        } else {
            console.error('[ERRORE] File PPM non trovato:', ppmThemeTab);
        }

        process.exit(0);
    });
    client.on('error', (err) => {
        console.error('Errore connessione monitor:', err);
    });
}, 2000);
