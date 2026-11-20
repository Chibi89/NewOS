const { spawn, execSync } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const imgPath = path.join(ROOT, 'myos.img');
const isoPath = path.join(ROOT, 'NewOS_V1.iso');
const brainDir = 'C:\\Users\\morga\\.gemini\\antigravity\\brain\\be9417d6-80de-4cf1-b4e0-c787fdbf2f97';

function sleep(ms) { return new Promise(res => setTimeout(res, ms)); }

async function moveMouse(client, dx, dy, steps = 25) {
    const sx = Math.trunc(dx / steps), sy = Math.trunc(dy / steps);
    const rx = dx - sx * steps, ry = dy - sy * steps;
    for (let i = 0; i < steps; i++) { client.write(`mouse_move ${sx} ${sy}\n`); await sleep(20); }
    if (rx !== 0 || ry !== 0) { client.write(`mouse_move ${rx} ${ry}\n`); await sleep(20); }
}

async function click(client) {
    await sleep(80);
    client.write('mouse_button 1\n');
    await sleep(60);
    client.write('mouse_button 0\n');
    await sleep(600);
}

async function captureScreen(client, name) {
    const ppm = path.join(ROOT, name + '.ppm');
    const bmp = path.join(ROOT, name + '.bmp');
    const png = path.join(brainDir, name + '.png');
    if (fs.existsSync(ppm)) fs.unlinkSync(ppm);
    await sleep(300);
    client.write(`screendump "${ppm.replace(/\\/g, '/')}"\n`);
    for (let w = 0; w < 15; w++) {
        await sleep(250);
        if (fs.existsSync(ppm) && fs.statSync(ppm).size > 10000) break;
    }
    if (fs.existsSync(ppm)) {
        execSync(`node "${path.join(ROOT, 'tools', 'ppm2bmp.js')}" "${ppm}" "${bmp}"`, { stdio: 'ignore' });
        execSync(`powershell -ExecutionPolicy Bypass -File "${path.join(ROOT, 'tools', 'bmp2png.ps1')}" "${bmp}" "${png}"`, { stdio: 'ignore' });
        console.log(`[OK] Screenshot salvato: ${png}`);
    }
}

async function main() {
    const serialLog = path.join(ROOT, 'tools', 'serial_logout.log');
    if (fs.existsSync(serialLog)) fs.unlinkSync(serialLog);

    const qemu = spawn(qemuBin, [
        '-drive', `format=raw,file=${imgPath}`,
        '-cdrom', isoPath,
        '-m', '256M',
        '-vga', 'std',
        '-audiodev', 'none,id=snd0',
        '-device', 'AC97,audiodev=snd0',
        '-serial', `file:${serialLog}`,
        '-display', 'none',
        '-monitor', 'tcp:127.0.0.1:4514,server,nowait'
    ], { stdio: 'ignore' });

    await sleep(1500);

    await new Promise((resolve, reject) => {
        const client = net.createConnection({ port: 4514 }, async () => {
            client.on('error', () => { });
            try {
                for (let i = 0; i < 45; i++) {
                    await sleep(150);
                    if (fs.existsSync(serialLog) && fs.readFileSync(serialLog, 'utf8').includes('[BOOT_MENU] Pronto!')) break;
                }
                for (let k = 0; k < 3; k++) { client.write('sendkey 1\n'); await sleep(120); }
                await sleep(7500);

                // Click su Start (32, 754) -> da (512, 384): dx=-480, dy=+370
                await moveMouse(client, -480, 370, 30);
                await click(client);
                await sleep(400);

                // Click su "Arresta il sistema..." (ultima voce Start Menu ~80, 720 -> dx=+48, dy=-34)
                await moveMouse(client, 48, -34, 15);
                await click(client);
                await sleep(1200);

                await captureScreen(client, 'screenshot_logout_connected');
                client.write('quit\n');
                await sleep(400);
                client.destroy();
                qemu.kill();
                resolve();
            } catch (e) {
                try { client.destroy(); } catch (_) { }
                try { qemu.kill(); } catch (_) { }
                reject(e);
            }
        });
    });

    if (fs.existsSync(serialLog)) {
        const log = fs.readFileSync(serialLog, 'utf8');
        console.log('--- SERIAL LOG (tail) ---');
        console.log(log.slice(-1000));
    }
}

main().catch(e => { console.error(e); process.exit(1); });
