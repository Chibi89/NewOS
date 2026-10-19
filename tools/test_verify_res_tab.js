const { spawn, execSync } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const isoPath = path.join(ROOT, 'NewOS_V1.iso');
const brainDir = 'C:\\Users\\morga\\.gemini\\antigravity-ide\\brain\\201a47cf-7636-4592-93f8-cb22ec54e1bd';

const ppmResTab = path.join(ROOT, 'test_res_tab_final.ppm');
const bmpResTab = path.join(ROOT, 'test_res_tab_final.bmp');
const pngResTab = path.join(brainDir, 'screenshot_vga_tab.png');

const ppmWide = path.join(ROOT, 'test_wide_final.ppm');
const bmpWide = path.join(ROOT, 'test_wide_final.bmp');
const pngWide = path.join(brainDir, 'screenshot_vga_1280x720.png');

console.log('[TEST VGA] Avvio QEMU con NewOS_V1.iso...');

const qemu = spawn(qemuBin, [
    '-cdrom', isoPath,
    '-m', '128M',
    '-vga', 'std',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4495,server,nowait',
    '-serial', `file:${path.join(ROOT, 'serial_vga_test4.log')}`
], { stdio: 'ignore' });

function sleep(ms) {
    return new Promise(res => setTimeout(res, ms));
}

setTimeout(() => {
    const client = net.createConnection({ port: 4495 }, async () => {
        console.log('Connesso al monitor QEMU...');
        await sleep(1500);

        // 1. Invio '3' per avviare direttamente Proprietà Schermo & Risoluzioni
        console.log('Avvio boot option 3 (Proprieta Schermo)...');
        client.write('sendkey 3\n');
        await sleep(2500);

        // 2. Invio '3' per passare al Tab 2 "Risoluzione"
        console.log('Invio tasto 3 per attivare la scheda Risoluzione...');
        client.write('sendkey 3\n');
        await sleep(1000);

        // 3. Cattura screenshot della scheda Risoluzione Schermo!
        console.log('Cattura screenshot scheda Risoluzione...');
        client.write(`screendump "${ppmResTab.replace(/\\/g, '/')}"\n`);
        await sleep(1000);

        // 4. Invio '2' per selezionare la modalità 2: [ 1280x720 (16:9 HD 720p) ]
        console.log('Selezione modalita 2 (1280x720 HD)...');
        client.write('sendkey 2\n');
        await sleep(600);

        // 5. Invio 'ret' (Invio) per applicare la nuova risoluzione widescreen!
        console.log('Invio Invio per applicare risoluzione 1280x720...');
        client.write('sendkey ret\n');
        await sleep(2000);

        // 6. Cattura screenshot dello schermo espanso a 1280x720!
        console.log('Cattura screenshot nuova risoluzione 1280x720...');
        client.write(`screendump "${ppmWide.replace(/\\/g, '/')}"\n`);
        await sleep(1000);

        client.end();
        qemu.kill();

        // Converti i file PPM in PNG
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
