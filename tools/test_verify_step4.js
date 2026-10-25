const { spawn, execSync } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const imgPath = path.join(ROOT, 'build', 'myos.img');
const ppmPath = path.join(ROOT, 'screen_explorer_adv.ppm');
const bmpPath = path.join(ROOT, 'screen_explorer_adv.bmp');
const pngArtifact = 'C:\\Users\\morga\\.gemini\\antigravity-ide\\brain\\f19bbd57-0a55-4397-b3b0-d5bd988a51be\\screen_explorer_adv.png';

if (fs.existsSync(ppmPath)) fs.unlinkSync(ppmPath);

console.log('Avvio QEMU per collaudo Esplora Risorse Avanzato (Menu Modifica & Context Menu)...');
const qemu = spawn(qemuBin, [
    '-smp', '2',
    '-drive', `format=raw,file=${imgPath}`,
    '-m', '256M',
    '-vga', 'std',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4465,server,nowait',
    '-serial', `file:${path.join(ROOT, 'serial.log')}`,
    '-d', 'guest_errors'
], { stdio: 'ignore' });

function sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

setTimeout(async () => {
    try {
        console.log('Connessione monitor 4465...');
        const client = net.createConnection({ port: 4465 });

        client.on('error', (err) => console.error('Monitor err:', err.message));
        await sleep(300);

        // Seleziona Live Mode nel boot menu
        console.log('Invio tasto 1 (Live Mode)...');
        client.write('sendkey 1\n');
        await sleep(500);
        client.write('sendkey ret\n');
        await sleep(5500);

        // Explorer è aperto di default (x=60, y=40, w=600, h=420)
        // Clicchiamo sul menu a tendina "Modifica" in alto a sinistra della finestra di Explorer
        // Menu "Modifica" è a cx + 44 = 60 + 4 + 44 = 108, cy + 2 = 40 + 24 + 2 = 66
        console.log('Apertura menu Modifica in Explorer...');
        // Portiamo prima il mouse a (0,0), poi a (108, 66)
        client.write('mouse_move -1000 -1000\n');
        await sleep(100);
        client.write('mouse_move 108 66\n');
        await sleep(150);
        client.write('mouse_button 1\n');
        await sleep(50);
        client.write('mouse_button 0\n');
        await sleep(500);

        // Cattura screendump del menu Modifica aperto
        console.log('Cattura screendump...');
        client.write(`screendump ${ppmPath}\n`);
        await sleep(600);

        client.write('quit\n');
        client.end();
    } catch (e) {
        console.error('Errore:', e);
    }
}, 1000);

qemu.on('close', () => {
    console.log('QEMU terminato. Conversione screendump...');
    try {
        if (fs.existsSync(ppmPath)) {
            execSync(`node "${path.join(ROOT, 'tools', 'ppm2bmp.js')}" "${ppmPath}" "${bmpPath}"`, { stdio: 'inherit' });
            const psScript = `Add-Type -AssemblyName System.Drawing; $img = [System.Drawing.Image]::FromFile('${bmpPath}'); $img.Save('${pngArtifact}', [System.Drawing.Imaging.ImageFormat]::Png); $img.Dispose()`;
            execSync(`powershell -Command "${psScript}"`, { stdio: 'inherit' });
            console.log('[OK] Cattura completata: ' + pngArtifact);
        } else {
            console.error('[ERRORE] File PPM non trovato.');
        }
    } catch (e) {
        console.error('Errore conversione:', e.message);
    }
    process.exit(0);
});
