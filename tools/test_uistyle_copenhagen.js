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

async function moveMouse(client, dx, dy, steps = 10) {
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
    } else {
        console.error(`[ERRORE] PPM non generato: ${ppm}`);
    }
}

async function run() {
    const serialLog = path.join(ROOT, 'tools', 'serial_uistyle.log');
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
        '-monitor', 'tcp:127.0.0.1:4499,server,nowait'
    ], { stdio: 'ignore' });

    await sleep(1500);

    const client = net.createConnection({ port: 4499 }, async () => {
        try {
            console.log('[TEST UISTYLE] Attesa Boot Menu...');
            for (let i = 0; i < 45; i++) {
                await sleep(150);
                if (fs.existsSync(serialLog)) {
                    const content = fs.readFileSync(serialLog, 'utf8');
                    if (content.includes('[BOOT_MENU] Pronto!')) break;
                }
            }

            console.log('[TEST UISTYLE] Avvio Impostazioni (tasto 9)...');
            for (let k = 0; k < 4; k++) {
                client.write('sendkey 9\n');
                await sleep(100);
            }
            await sleep(8000);

            // Sposta su Personalizzazione nella sidebar (Rete = -280,-120; Dispositivi = -280,-85 -> Personalizzazione = -280,-102 con 10 step)
            await moveMouse(client, -280, -102, 10);
            await clickMouse(client);
            await sleep(800);
            await captureScreen(client, 'screenshot_uistyle_settings');

            // Sposta sul pulsante "Applica" della riga 2 (Copenhagen Glass UI)
            // Da (232,375) a (585, 545) => dx=+353, dy=+133 (con fattore PS/2 ~1.27)
            await moveMouse(client, 353, 133, 40);
            await clickMouse(client);
            await sleep(1500);
            await captureScreen(client, 'screenshot_uistyle_applied');

            // Chiude la finestra Impostazioni cliccando X in alto a destra (652, 72)
            // Da (585,545) -> dx=+53, dy=-370 (steps=40)
            await moveMouse(client, 53, -370, 40);
            await clickMouse(client);
            await sleep(1000);
            await captureScreen(client, 'screenshot_uistyle_desktop');

            // Clicca sul pulsante "Morgan's Stuff" nella Top Bar (~470, 18)
            // Da (652,72) -> dx=-182, dy=-54 (steps=30)
            await moveMouse(client, -182, -54, 30);
            await clickMouse(client);
            await sleep(800);
            await captureScreen(client, 'screenshot_uistyle_menu');

            // Clicca sull'icona "Risorse del PC" sul desktop (~42, 64)
            // Da (470,18) -> dx=-428, dy=+46 (steps=40)
            await moveMouse(client, -428, 46, 40);
            await clickMouse(client);
            await sleep(250);
            await clickMouse(client);
            await sleep(1200);
            await captureScreen(client, 'screenshot_uistyle_risorse');

            client.write('quit\n');
            await sleep(500);
            client.destroy();
            qemu.kill();
            process.exit(0);
        } catch (err) {
            console.error(err);
            try { client.destroy(); } catch (e) {}
            try { qemu.kill(); } catch (e) {}
            process.exit(1);
        }
    });
}

run();

