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
    await sleep(100);
    client.write('mouse_button 1\n');
    client.write('mouse_move 0 0\n');
    await sleep(70);
    client.write('mouse_button 0\n');
    client.write('mouse_move 0 0\n');
    await sleep(300);
}

const KEYMAP = {
    '.': 'dot', ':': 'shift-semicolon', '/': 'slash', '-': 'minus',
    '?': 'shift-slash', '=': 'equal', '\n': 'ret'
};

async function sendText(client, str) {
    for (const ch of str) {
        const key = KEYMAP[ch] || ch;
        client.write(`sendkey ${key}\n`);
        await sleep(65);
    }
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
    const serialLog = path.join(ROOT, 'tools', 'serial_web_https.log');
    if (fs.existsSync(serialLog)) fs.unlinkSync(serialLog);

    const pcapPath = path.join(ROOT, 'netdump.pcap');
    if (fs.existsSync(pcapPath)) fs.unlinkSync(pcapPath);

    const qemu = spawn(qemuBin, [
        '-drive', `format=raw,file=${imgPath}`,
        '-cdrom', isoPath,
        '-m', '256M',
        '-vga', 'std',
        '-netdev', 'user,id=net0',
        '-device', 'e1000,netdev=net0',
        '-object', `filter-dump,id=f0,netdev=net0,file=${pcapPath}`,
        '-audiodev', 'none,id=snd0',
        '-device', 'AC97,audiodev=snd0',
        '-serial', `file:${serialLog}`,
        '-display', 'none',
        '-monitor', 'tcp:127.0.0.1:4505,server,nowait'
    ], { stdio: 'ignore' });

    await sleep(1500);

    const client = net.createConnection({ port: 4505 }, async () => {
        client.on('error', () => {});
        try {
            for (let i = 0; i < 45; i++) {
                await sleep(150);
                if (fs.existsSync(serialLog) && fs.readFileSync(serialLog, 'utf8').includes('[BOOT_MENU] Pronto!')) break;
            }
            for (let k = 0; k < 4; k++) {
                client.write('sendkey 1\n');
                await sleep(100);
            }
            await sleep(13500);

            // Click singolo su icona Web Browser (colonna sinistra, 2a icona: 42, 144)
            // Da centro (512, 384) -> dx=-470, dy=-240
            await moveMouse(client, -470, -240, 40);
            await clickMouse(client);
            for (let w = 0; w < 20; w++) {
                await sleep(300);
                if (fs.existsSync(serialLog) && fs.readFileSync(serialLog, 'utf8').includes('[POST_LOAD]')) break;
            }
            await sleep(800);

            // Click nella barra indirizzi della 1a finestra Web Browser (x=350, y=190 -> da 42,144 è +308,+46)
            await moveMouse(client, 308, 46, 30);
            await clickMouse(client);
            await sleep(400);

            // Cancella eventuale URL con backspace ripetuto
            for (let b = 0; b < 30; b++) {
                client.write('sendkey backspace\n');
                await sleep(20);
            }

            // 1. Ricerca Omnibox diretta (digitando "newos" senza URL completo!)
            console.log('[TEST WEB] Ricerca Omnibox "newos"...');
            await sendText(client, 'newos');
            await moveMouse(client, 495, 0, 25);
            await clickMouse(client);
            await sleep(10000);
            await captureScreen(client, 'screenshot_web_ddg');

            // 2. Test connessione reale a YouTube (yt:newos)
            await moveMouse(client, -495, 0, 25);
            await clickMouse(client);
            await sleep(200);
            for (let b = 0; b < 45; b++) {
                client.write('sendkey backspace\n');
                await sleep(35);
            }
            await sleep(300);
            console.log('[TEST WEB] Ricerca reale YouTube yt:newos...');
            await sendText(client, 'yt:newos');
            await sleep(200);
            await moveMouse(client, 495, 0, 25);
            await clickMouse(client);
            for (let w = 0; w < 150; w++) {
                await sleep(500);
                if (fs.existsSync(serialLog)) {
                    const logTxt = fs.readFileSync(serialLog, 'utf8');
                    if ((logTxt.match(/\[DRAW_IMGS_OK\]/g) || []).length >= 2) {
                        await sleep(800);
                        break;
                    }
                }
            }
            await moveMouse(client, 10, 10, 5);
            await sleep(1500);
            await captureScreen(client, 'screenshot_web_youtube');

            client.write('quit\n');
            await sleep(400);
            client.destroy();
            qemu.kill();
            process.exit(0);
        } catch (err) {
            console.error(err);
            try { client.destroy(); } catch (_) {}
            try { qemu.kill(); } catch (_) {}
            process.exit(1);
        }
    });
}

main();

