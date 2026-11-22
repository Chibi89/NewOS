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

async function sendText(client, text) {
    for (const ch of text) {
        let key = ch === ' ' ? 'spc' : ch;
        client.write(`sendkey ${key}\n`);
        await sleep(60);
    }
    client.write('sendkey ret\n');
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
    const serialLog = path.join(ROOT, 'tools', 'serial_seccpl.log');
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
        '-monitor', 'tcp:127.0.0.1:4516,server,nowait'
    ], { stdio: 'ignore' });

    await sleep(2500);

    await new Promise((resolve, reject) => {
        const client = net.createConnection({ host: '127.0.0.1', port: 4516 }, async () => {
            client.on('error', () => { });
            try {
                for (let i = 0; i < 45; i++) {
                    await sleep(150);
                    if (fs.existsSync(serialLog) && fs.readFileSync(serialLog, 'utf8').includes('[BOOT_MENU] Pronto!')) break;
                }
                for (let k = 0; k < 3; k++) { client.write('sendkey 1\n'); await sleep(120); }
                await sleep(7500);

                // Apri CMD via doppio click su icona Prompt Comandi (72, 308)
                await moveMouse(client, -440, -76, 30);
                client.write('mouse_button 1\n'); client.write('mouse_move 0 0\n'); await sleep(80);
                client.write('mouse_button 0\n'); client.write('mouse_move 0 0\n'); await sleep(120);
                client.write('mouse_button 1\n'); client.write('mouse_move 0 0\n'); await sleep(80);
                client.write('mouse_button 0\n'); client.write('mouse_move 0 0\n');
                await sleep(900);

                // Focus CMD
                await moveMouse(client, 220, -60, 15);
                client.write('mouse_button 1\n'); client.write('mouse_move 0 0\n'); await sleep(80);
                client.write('mouse_button 0\n'); client.write('mouse_move 0 0\n');
                await sleep(500);

                // Lancia SECCPL.CPL via LAUNCHER.DLL ("start seccpl")
                await sendText(client, 'start seccpl');
                await sleep(1000);
                await captureScreen(client, 'screenshot_seccpl_tab0');

                // Clicca su "Applica" (x~740, y~502 -> da 292,248: dx=+448, dy=+254) per testare Kernel Policy Gate!
                await moveMouse(client, 448, 254, 25);
                client.write('mouse_button 1\n'); client.write('mouse_move 0 0\n'); await sleep(80);
                client.write('mouse_button 0\n'); client.write('mouse_move 0 0\n');
                await sleep(800);
                await captureScreen(client, 'screenshot_seccpl_applied');

                // Clicca sulla Tab 1 "File System" (x~432, y~179 -> da 740,502: dx=-308, dy=-323)
                await moveMouse(client, -308, -323, 25);
                client.write('mouse_button 1\n'); client.write('mouse_move 0 0\n'); await sleep(80);
                client.write('mouse_button 0\n'); client.write('mouse_move 0 0\n');
                await sleep(800);
                await captureScreen(client, 'screenshot_seccpl_tab1');

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
        console.log('--- SERIAL LOG (SECCPL) ---');
        for (const line of log.split(/\r?\n/)) {
            if (line.includes('SECCPL') || line.includes('LAUNCHER') || line.includes('KernelSecurityPolicy') || line.includes('SECURITY')) {
                console.log(line);
            }
        }
    }
}

main().catch(e => { console.error(e); process.exit(1); });
