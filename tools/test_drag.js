const { spawn } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const imgPath = path.join(ROOT, 'myos.img');
const ppmPath = path.join(ROOT, 'screenshot_drag.ppm');

if (fs.existsSync(ppmPath)) fs.unlinkSync(ppmPath);

const serialLog = path.join(ROOT, 'serial_drag.log');
if (fs.existsSync(serialLog)) fs.unlinkSync(serialLog);

console.log('Avvio QEMU per test drag & drop...');
const qemu = spawn(qemuBin, [
    '-drive', `format=raw,file=${imgPath}`,
    '-m', '256M',
    '-vga', 'std',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4445,server,nowait',
    '-serial', `file:${serialLog}`
], { stdio: 'ignore' });

function sleep(ms) {
    return new Promise(res => setTimeout(res, ms));
}

setTimeout(async () => {
    console.log('Connessione a QEMU monitor...');
    const client = net.createConnection({ port: 4445 }, async () => {
        console.log('Avviato test sequenza mouse...');

        // Mouse parte da (512, 384)
        // Chiudiamo prima le finestre aperte per avere il desktop completamente libero:
        // Spostiamo il mouse sul pulsante [X] di "Proprieta del sistema" o "Prompt comandi",
        // oppure andiamo direttamente sull'icona a sinistra:
        // A sinistra x = 20, y = 470 per "Note.txt".
        // Da 512 a 20: dx = -492 (circa 6 x -82).
        // Da 384 a 470: dy = -86 (poiché dy negativo sposta Y verso il basso).
        for (let i = 0; i < 6; i++) {
            client.write('mouse_move -82 -21\n');
            await sleep(60);
        }
        await sleep(150);

        // Ora siamo esattamente sopra l'icona "Note.txt" a (20, 468)
        // Premiamo il tasto sinistro e inviamo un pacchetto mouse
        client.write('mouse_button 1\n');
        client.write('mouse_move 0 0\n');
        await sleep(150);

        // Trasciniamo verso destra e verso il basso (+400 px a destra, +250 px in basso verso spazio libero)
        for (let i = 0; i < 10; i++) {
            client.write('mouse_move 40 25\n');
            await sleep(60);
        }
        await sleep(150);

        // Rilasciamo il tasto sinistro (drop)
        client.write('mouse_button 0\n');
        client.write('mouse_move 0 0\n');
        await sleep(250);

        // Screendump
        console.log('Cattura screenshot del drag...');
        client.write(`screendump "${ppmPath.replace(/\\/g, '/')}"\n`);
        await sleep(1000);

        client.write('quit\n');
        client.end();
        setTimeout(() => {
            qemu.kill();
            process.exit(0);
        }, 500);
    });

    client.on('error', (err) => {
        console.error('Errore connessione monitor:', err.message);
        qemu.kill();
        process.exit(1);
    });
}, 2000);
