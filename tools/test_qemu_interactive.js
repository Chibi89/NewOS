const { spawn, execSync } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const isoPath = path.join(ROOT, 'NewOS_V1.iso');

function sleep(ms) {
    return new Promise(res => setTimeout(res, ms));
}

console.log('[DEBUG] Avvio QEMU per tracciamento dettagliato...');

const qemu = spawn(qemuBin, [
    '-cdrom', isoPath,
    '-m', '128M',
    '-vga', 'std',
    '-netdev', 'user,id=net0',
    '-device', 'e1000,netdev=net0',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4630,server,nowait'
], { stdio: 'ignore' });

setTimeout(() => {
    const client = net.createConnection({ port: 4630 }, async () => {
        client.on('data', (d) => {
            process.stdout.write(`[QEMU MON] ${d.toString()}`);
        });

        console.log('[DEBUG] Connesso. Attendo 4 secondi per arrivo a boot menu...');
        await sleep(4000);

        console.log('[DEBUG] Invio "sendkey 6" (Avvio diretto Desktop con Explorer)...');
        client.write('sendkey 6\n');
        await sleep(3000);

        const ppm = path.join(ROOT, 'debug_screen.ppm');
        const bmp = path.join(ROOT, 'debug_screen.bmp');
        const png = path.join(ROOT, 'debug_screen.png');

        console.log('[DEBUG] Cattura screenshot desktop...');
        client.write(`screendump "${ppm.replace(/\\/g, '/')}"\n`);
        await sleep(1500);

        client.write('quit\n');
        await sleep(500);
        qemu.kill();

        if (fs.existsSync(ppm)) {
            console.log('[DEBUG] PPM catturato con successo, dimensione:', fs.statSync(ppm).size);
            execSync(`node tools/ppm2bmp.js "${ppm}" "${bmp}"`, { stdio: 'inherit' });
            execSync(`powershell -ExecutionPolicy Bypass -File tools/bmp2png.ps1 "${bmp}" "${png}"`, { stdio: 'inherit' });
            console.log('[DEBUG] PNG generato con successo:', fs.statSync(png).size);
        } else {
            console.error('[DEBUG ERR] PPM non trovato!');
        }
        process.exit(0);
    });

    client.on('error', (err) => {
        console.error('[DEBUG ERR] Errore connessione:', err.message);
        qemu.kill();
        process.exit(1);
    });
}, 2000);
