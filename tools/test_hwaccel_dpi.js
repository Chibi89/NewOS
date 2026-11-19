const { spawn, execSync } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const imgPath = path.join(ROOT, 'myos.img');
const isoPath = path.join(ROOT, 'NewOS_V1.iso');
const brainDir = 'C:\\Users\\morga\\.gemini\\antigravity\\brain\\be9417d6-80de-4cf1-b4e0-c787fdbf2f97';

function sleep(ms) {
    return new Promise(res => setTimeout(res, ms));
}

async function moveMouse(client, dx, dy, steps = 20) {
    const sx = Math.trunc(dx / steps);
    const sy = Math.trunc(dy / steps);
    const rx = dx - sx * steps;
    const ry = dy - sy * steps;
    for (let i = 0; i < steps; i++) {
        client.write(`mouse_move ${sx} ${sy}\n`);
        await sleep(25);
    }
    if (rx !== 0 || ry !== 0) {
        client.write(`mouse_move ${rx} ${ry}\n`);
        await sleep(25);
    }
}

async function clickMouse(client) {
    await sleep(120);
    client.write('mouse_button 1\n');
    client.write('mouse_move 0 0\n');
    await sleep(80);
    client.write('mouse_button 0\n');
    client.write('mouse_move 0 0\n');
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

async function runSession(bootKey, port, cb) {
    const serialLog = path.join(ROOT, 'tools', `serial_hw_${bootKey}.log`);
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
        '-monitor', `tcp:127.0.0.1:${port},server,nowait`
    ], { stdio: 'ignore' });

    await sleep(1500);

    return new Promise((resolve, reject) => {
        const client = net.createConnection({ port }, async () => {
            client.on('error', () => { });
            try {
                for (let i = 0; i < 45; i++) {
                    await sleep(150);
                    if (fs.existsSync(serialLog) && fs.readFileSync(serialLog, 'utf8').includes('[BOOT_MENU] Pronto!')) break;
                }
                for (let k = 0; k < 4; k++) {
                    client.write(`sendkey ${bootKey}\n`);
                    await sleep(100);
                }
                await sleep(8000);
                await cb(client);
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
}

async function main() {
    console.log('=== TEST 1: Impostazioni > Sistema (HW Accel & Scaling DPI) ===');
    await runSession('9', 4501, async (client) => {
        // Va su "Sistema" nella sidebar (prima voce sotto profilo: -280, -190)
        await moveMouse(client, -280, -190, 10);
        await clickMouse(client);
        await sleep(800);
        await captureScreen(client, 'screenshot_hwaccel_system');

        // Clicca sul pulsante "125%" (x ~ 505, y ~ 495) -> dx=+215, dy=+145 (con steps=40)
        await moveMouse(client, 215, 145, 40);
        await clickMouse(client);
        await sleep(1000);
        await captureScreen(client, 'screenshot_hwaccel_scale125');

        // Ripristina "Auto" (-120 in X)
        await moveMouse(client, -120, 0, 20);
        await clickMouse(client);
        await sleep(600);
    });

    console.log('=== TEST 2: Proprieta Schermo > Risoluzione (HW Accel & DPI) ===');
    await runSession('3', 4502, async (client) => {
        // Tasto '3' in display.c apre la tab Risoluzione
        client.write('sendkey 3\n');
        await sleep(1200);
        await captureScreen(client, 'screenshot_hwaccel_display');
    });
}

main();

