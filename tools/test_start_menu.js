const { spawn } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const imgPath = path.join(ROOT, 'myos.img');
const ppmPath = path.join(ROOT, 'screenshot_start.ppm');

if (fs.existsSync(ppmPath)) fs.unlinkSync(ppmPath);

console.log('Avvio QEMU per test Start Menu con Icone...');
const qemu = spawn(qemuBin, [
    '-drive', `format=raw,file=${imgPath}`,
    '-m', '256M',
    '-vga', 'std',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4447,server,nowait'
], { stdio: 'ignore' });

function sleep(ms) {
    return new Promise(res => setTimeout(res, ms));
}

setTimeout(async () => {
    console.log('Connessione al monitor QEMU...');
    let sentQuit = false;
    const client = net.createConnection({ port: 4447 }, async () => {
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
        // Spostiamo il mouse sul pulsante Start a (25, 755)
        // dx = 25 - 512 = -487
        // dy = -(755 - 384) = -371 (negativo verso il basso)
        await moveMouseBy(-487, -371);
        client.write('mouse_button 1\n');
        client.write('mouse_move 0 0\n');
        await sleep(60);
        client.write('mouse_button 0\n');
        client.write('mouse_move 0 0\n');
        await sleep(300);

        // Cattura screendump
        console.log('Cattura screenshot dello Start Menu...');
        client.write(`screendump "${ppmPath.replace(/\\/g, '/')}"\n`);
        await sleep(1200);

        sentQuit = true;
        client.write('quit\n');
        await sleep(500);

        try { client.destroy(); } catch (e) {}
        try { qemu.kill(); } catch (e) {}

        if (fs.existsSync(ppmPath)) {
            const stats = fs.statSync(ppmPath);
            console.log(`[SUCCESSO] Screenshot Start Menu catturato! (${stats.size} byte)`);
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
