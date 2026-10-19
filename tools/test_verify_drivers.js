const { spawn, execSync } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const imgPath = path.join(ROOT, 'build', 'myos.img');
const ppmCmd = path.join(ROOT, 'screenshot_driverquery.ppm');
const bmpCmd = path.join(ROOT, 'screenshot_driverquery.bmp');
const pngCmd = 'C:\\Users\\morga\\.gemini\\antigravity-ide\\brain\\201a47cf-7636-4592-93f8-cb22ec54e1bd\\screenshot_driverquery.png';

const ppmDev = path.join(ROOT, 'screenshot_devmgmt.ppm');
const bmpDev = path.join(ROOT, 'screenshot_devmgmt.bmp');
const pngDev = 'C:\\Users\\morga\\.gemini\\antigravity-ide\\brain\\201a47cf-7636-4592-93f8-cb22ec54e1bd\\screenshot_devmgmt.png';

if (fs.existsSync(ppmCmd)) fs.unlinkSync(ppmCmd);
if (fs.existsSync(ppmDev)) fs.unlinkSync(ppmDev);

console.log('Avvio QEMU per test Gestione Periferiche e Driver Modulari...');
const qemu = spawn(qemuBin, [
    '-drive', `format=raw,file=${imgPath}`,
    '-m', '256M',
    '-vga', 'std',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4478,server,nowait',
    '-serial', `file:${path.join(ROOT, 'serial.log')}`
], { stdio: 'ignore' });

function sleep(ms) {
    return new Promise(res => setTimeout(res, ms));
}

setTimeout(async () => {
    console.log('Connessione monitor 4478...');
    const client = net.createConnection({ port: 4478 });

    client.on('error', (err) => console.error('Monitor err:', err.message));
    await sleep(300);

    // Boot menu -> 1 (Live Mode)
    console.log('Invio tasto 1 (Live Mode)...');
    client.write('sendkey 1\n');
    await sleep(1500);

    // Chiudi explorer con Esc per avere il desktop pulito
    client.write('sendkey esc\n');
    await sleep(400);

    // Clicca sull'icona "Prompt Comandi" (3a icona a sinistra)
    console.log('Apertura Prompt dei Comandi...');
    client.write('mouse_move -476 -198\n');
    await sleep(300);
    client.write('mouse_button 1\n');
    await sleep(60);
    client.write('mouse_button 0\n');
    await sleep(80);
    client.write('mouse_button 1\n');
    await sleep(60);
    client.write('mouse_button 0\n');
    await sleep(1000);

    // Digita "driverquery" e premi Invio
    console.log('Invio comando driverquery...');
    const cmd = "driverquery";
    for (let i = 0; i < cmd.length; i++) {
        client.write(`sendkey ${cmd[i]}\n`);
        await sleep(50);
    }
    client.write('sendkey ret\n');
    await sleep(1000);

    // Cattura screenshot 1: Terminale con tabella driverquery completa
    console.log('Cattura screenshot driverquery...');
    client.write(`screendump "${ppmCmd.replace(/\\/g, '/')}"\n`);
    await sleep(800);

    // Ora lancia "devmgmt" per aprire la finestra di Gestione Periferiche
    console.log('Invio comando devmgmt...');
    const cmd2 = "devmgmt";
    for (let i = 0; i < cmd2.length; i++) {
        client.write(`sendkey ${cmd2[i]}\n`);
        await sleep(50);
    }
    client.write('sendkey ret\n');
    await sleep(1200);

    // Cattura screenshot 2: Gestione Periferiche
    console.log('Cattura screenshot devmgmt...');
    client.write(`screendump "${ppmDev.replace(/\\/g, '/')}"\n`);
    await sleep(800);

    client.write('quit\n');
    await sleep(400);

    try { client.destroy(); } catch (e) {}
    try { qemu.kill(); } catch (e) {}

    if (fs.existsSync(ppmCmd)) {
        execSync(`node "${path.join(ROOT, 'tools', 'ppm2bmp.js')}" "${ppmCmd}" "${bmpCmd}"`, { stdio: 'inherit' });
        execSync(`powershell -ExecutionPolicy Bypass -File "${path.join(ROOT, 'tools', 'bmp2png.ps1')}" "${bmpCmd}" "${pngCmd}"`, { stdio: 'inherit' });
        console.log('[OK] driverquery screenshot completato!');
    }

    if (fs.existsSync(ppmDev)) {
        execSync(`node "${path.join(ROOT, 'tools', 'ppm2bmp.js')}" "${ppmDev}" "${bmpDev}"`, { stdio: 'inherit' });
        execSync(`powershell -ExecutionPolicy Bypass -File "${path.join(ROOT, 'tools', 'bmp2png.ps1')}" "${bmpDev}" "${pngDev}"`, { stdio: 'inherit' });
        console.log('[OK] devmgmt screenshot completato!');
    }
    process.exit(0);
}, 2000);
