const { spawn, execSync } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const isoPath = path.join(ROOT, 'NewOS_V1.iso');
const brainDir = 'C:\\Users\\morga\\.gemini\\antigravity-ide\\brain\\201a47cf-7636-4592-93f8-cb22ec54e1bd';

const ppmPing = path.join(ROOT, 'test_ping.ppm');
const bmpPing = path.join(ROOT, 'test_ping.bmp');
const pngPing = path.join(brainDir, 'screenshot_network_ping.png');

console.log('[TEST PING] Avvio QEMU con e1000 e test comando PING...');

const qemu = spawn(qemuBin, [
    '-cdrom', isoPath,
    '-m', '128M',
    '-vga', 'std',
    '-netdev', 'user,id=net0',
    '-device', 'e1000,netdev=net0',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4500,server,nowait'
], { stdio: 'ignore' });

function sleep(ms) {
    return new Promise(res => setTimeout(res, ms));
}

setTimeout(() => {
    const client = net.createConnection({ port: 4500 }, async () => {
        console.log('Connesso a QEMU...');
        await sleep(1500);

        // Boot option 4: Prompt dei Comandi
        client.write('sendkey 4\n');
        await sleep(2500);

        // Digita "ping 10.0.2.2"
        console.log('Invio comando "ping 10.0.2.2"...');
        const cmd = 'ping 10.0.2.2';
        for (const c of cmd) {
            if (c === ' ') client.write('sendkey spc\n');
            else if (c === '.') client.write('sendkey dot\n');
            else client.write(`sendkey ${c}\n`);
            await sleep(60);
        }
        client.write('sendkey ret\n');
        await sleep(2000);

        // Cattura screenshot di PING!
        client.write(`screendump "${ppmPing.replace(/\\/g, '/')}"\n`);
        await sleep(1000);

        client.end();
        qemu.kill();

        if (fs.existsSync(ppmPing)) {
            execSync(`node tools/ppm2bmp.js "${ppmPing}" "${bmpPing}"`, { stdio: 'ignore' });
            execSync(`powershell -ExecutionPolicy Bypass -File tools/bmp2png.ps1 "${bmpPing}" "${pngPing}"`, { stdio: 'ignore' });
            console.log(`[OK] Generato screenshot: ${pngPing}`);
        }

        process.exit(0);
    });
}, 1500);
