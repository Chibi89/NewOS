const { spawn } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const imgPath = path.join(ROOT, 'myos.img');
const ppmPath = path.join(ROOT, 'screenshot_wallpaper.ppm');

if (fs.existsSync(ppmPath)) fs.unlinkSync(ppmPath);

const serialLog = path.join(ROOT, 'serial_wallpaper.log');
if (fs.existsSync(serialLog)) fs.unlinkSync(serialLog);

console.log('Avvio QEMU per test selezione Wallpaper Fotografico...');
const qemu = spawn(qemuBin, [
    '-drive', `format=raw,file=${imgPath}`,
    '-m', '256M',
    '-vga', 'std',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4446,server,nowait',
    '-serial', `file:${serialLog}`
], { stdio: 'ignore' });

function sleep(ms) {
    return new Promise(res => setTimeout(res, ms));
}

setTimeout(async () => {
    console.log('Connessione al monitor QEMU...');
    let sentQuit = false;
    const client = net.createConnection({ port: 4446 }, async () => {
        console.log('Avviata sequenza interazione GUI...');

        async function moveMouseBy(dx, dy) {
            const steps = Math.max(Math.ceil(Math.abs(dx) / 50), Math.ceil(Math.abs(dy) / 50), 1);
            const stepX = Math.round(dx / steps);
            const stepY = Math.round(dy / steps);
            for (let i = 0; i < steps; i++) {
                client.write(`mouse_move ${stepX} ${stepY}\n`);
                await sleep(50);
            }
            await sleep(100);
        }

        // Mouse parte da (512, 384)
        // 1. Chiudiamo la finestra "Suoni" cliccando sul tasto [X] in alto a destra a (566, 100)
        // dx = +54, dy = +284
        await moveMouseBy(54, 284);
        client.write('mouse_button 1\n');
        await sleep(60);
        client.write('mouse_button 0\n');
        await sleep(250);

        // Ora la finestra attiva e "Proprieta - Schermo"
        // 2. Clicchiamo su "Windows Hero" (BG_PHOTO_HERO) a (430, 299)
        // Da (566, 100) a (430, 299): dx = -136, dy = -199
        await moveMouseBy(-136, -199);
        client.write('mouse_button 1\n');
        await sleep(60);
        client.write('mouse_button 0\n');
        await sleep(250);

        // 3. Clicchiamo su "Applica" a (502, 434)
        // Da (430, 299) a (502, 434): dx = +72, dy = -135
        await moveMouseBy(72, -135);
        client.write('mouse_button 1\n');
        await sleep(60);
        client.write('mouse_button 0\n');
        await sleep(600); // tempo per caricamento ATA PIO dal disco

        // Cattura screendump
        console.log('Cattura screenshot con wallpaper applicato...');
        client.write(`screendump "${ppmPath.replace(/\\/g, '/')}"\n`);
        await sleep(1200);

        sentQuit = true;
        client.write('quit\n');
        await sleep(500);

        try { client.destroy(); } catch (e) {}
        try { qemu.kill(); } catch (e) {}

        if (fs.existsSync(ppmPath)) {
            const stats = fs.statSync(ppmPath);
            console.log(`[SUCCESSO] Screenshot wallpaper catturato! Dimensione: ${stats.size} byte`);
        } else {
            console.log('[INFO] Screenshot non trovato.');
        }
        process.exit(0);
    });

    client.on('error', (err) => {
        if (!sentQuit) {
            console.error('Errore connessione monitor:', err.message);
            try { qemu.kill(); } catch (e) {}
            process.exit(1);
        }
    });
}, 2500);
