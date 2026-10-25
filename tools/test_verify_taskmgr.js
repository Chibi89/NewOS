const { spawn, execSync } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const imgPath = path.join(ROOT, 'build', 'myos.img');
const ppmPath = path.join(ROOT, 'screen_taskmgr.ppm');
const bmpPath = path.join(ROOT, 'screen_taskmgr.bmp');
const pngArtifact = 'C:\\Users\\morga\\.gemini\\antigravity-ide\\brain\\f19bbd57-0a55-4397-b3b0-d5bd988a51be\\screen_taskmgr.png';

if (fs.existsSync(ppmPath)) fs.unlinkSync(ppmPath);

console.log('Avvio QEMU per collaudo Task Manager Grafico (SMP 2 Core)...');
const qemu = spawn(qemuBin, [
    '-smp', '2',
    '-drive', `format=raw,file=${imgPath}`,
    '-m', '256M',
    '-vga', 'std',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4460,server,nowait',
    '-serial', `file:${path.join(ROOT, 'serial.log')}`,
    '-d', 'guest_errors'
], { stdio: 'ignore' });

function sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

setTimeout(async () => {
    try {
        console.log('Connessione monitor 4460...');
        const client = net.createConnection({ port: 4460 });

        client.on('error', (err) => console.error('Monitor err:', err.message));
        await sleep(300);

        // Seleziona Live Mode nel boot menu
        console.log('Invio tasto 1 (Live Mode)...');
        client.write('sendkey 1\n');
        await sleep(500);
        client.write('sendkey ret\n');
        await sleep(5500);

        // Cattura screendump Tab 0 Processi
        console.log('Cattura screendump Task Manager Processi...');
        client.write(`screendump ${ppmPath}\n`);
        await sleep(500);

        // Ora clicchiamo sul Tab Prestazioni:
        // Window a (140, 70). Tab Prestazioni è a local_x = 4 + 96 = 100..194, local_y = 24 + 6 = 30..54.
        // Screen coords: x = 140 + 140 = 280, y = 70 + 40 = 110.
        // Spostiamo il mouse partendo dal centro (512, 384): dx = 280 - 512 = -232, dy = 110 - 384 = -274.
        console.log('Clic su Tab Prestazioni...');
        // Portiamo prima il mouse a (0,0) con dx=-1000, dy=-1000, poi a (280, 110)
        client.write('mouse_move -1000 -1000\n');
        await sleep(100);
        client.write('mouse_move 280 110\n');
        await sleep(150);
        client.write('mouse_button 1\n');
        await sleep(50);
        client.write('mouse_button 0\n');
        await sleep(500);

        const ppmPerf = path.join(ROOT, 'screen_taskmgr_perf.ppm');
        client.write(`screendump ${ppmPerf}\n`);
        await sleep(500);

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
            // Converte PPM in BMP con ppm2bmp.js
            execSync(`node "${path.join(ROOT, 'tools', 'ppm2bmp.js')}" "${ppmPath}" "${bmpPath}"`, { stdio: 'inherit' });
            // Converte BMP in PNG tramite PowerShell
            const psScript = `Add-Type -AssemblyName System.Drawing; $img = [System.Drawing.Image]::FromFile('${bmpPath}'); $img.Save('${pngArtifact}', [System.Drawing.Imaging.ImageFormat]::Png); $img.Dispose()`;
            execSync(`powershell -Command "${psScript}"`, { stdio: 'inherit' });
            console.log('[OK] Cattura completata: ' + pngArtifact);
        }

        const ppmPerf = path.join(ROOT, 'screen_taskmgr_perf.ppm');
        const bmpPerf = path.join(ROOT, 'screen_taskmgr_perf.bmp');
        const pngPerf = 'C:\\Users\\morga\\.gemini\\antigravity-ide\\brain\\f19bbd57-0a55-4397-b3b0-d5bd988a51be\\screen_taskmgr_perf.png';
        if (fs.existsSync(ppmPerf)) {
            execSync(`node "${path.join(ROOT, 'tools', 'ppm2bmp.js')}" "${ppmPerf}" "${bmpPerf}"`, { stdio: 'inherit' });
            const psScript = `Add-Type -AssemblyName System.Drawing; $img = [System.Drawing.Image]::FromFile('${bmpPerf}'); $img.Save('${pngPerf}', [System.Drawing.Imaging.ImageFormat]::Png); $img.Dispose()`;
            execSync(`powershell -Command "${psScript}"`, { stdio: 'inherit' });
            console.log('[OK] Cattura tab Prestazioni completata: ' + pngPerf);
        }
    } catch (e) {
        console.error('Errore conversione:', e.message);
    }
    process.exit(0);
});
