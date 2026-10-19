const { spawn, execSync } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const isoPath = path.join(ROOT, 'NewOS_V1.iso');
const brainDir = 'C:\\Users\\morga\\.gemini\\antigravity-ide\\brain\\201a47cf-7636-4592-93f8-cb22ec54e1bd';

const ppmNet = path.join(ROOT, 'test_net.ppm');
const bmpNet = path.join(ROOT, 'test_net.bmp');
const pngNet = path.join(brainDir, 'screenshot_network_ipconfig.png');

console.log('[TEST NET] Avvio QEMU con supporto di rete e1000...');

const qemu = spawn(qemuBin, [
    '-cdrom', isoPath,
    '-m', '128M',
    '-vga', 'std',
    '-netdev', 'user,id=net0',
    '-device', 'e1000,netdev=net0',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4499,server,nowait',
    '-serial', `file:${path.join(ROOT, 'serial_net_test.log')}`
], { stdio: 'ignore' });

function sleep(ms) {
    return new Promise(res => setTimeout(res, ms));
}

setTimeout(() => {
    const client = net.createConnection({ port: 4499 }, async () => {
        console.log('Connesso al monitor QEMU...');
        await sleep(1500);

        // 1. Boot option 4: Prompt dei Comandi
        console.log('Selezione boot option 4 (Prompt dei Comandi)...');
        client.write('sendkey 4\n');
        await sleep(2500);

        // 2. Digita comando "ipconfig"
        console.log('Digitazione "ipconfig"...');
        client.write('sendkey i\n'); await sleep(80);
        client.write('sendkey p\n'); await sleep(80);
        client.write('sendkey c\n'); await sleep(80);
        client.write('sendkey o\n'); await sleep(80);
        client.write('sendkey n\n'); await sleep(80);
        client.write('sendkey f\n'); await sleep(80);
        client.write('sendkey i\n'); await sleep(80);
        client.write('sendkey g\n'); await sleep(80);
        client.write('sendkey ret\n');
        await sleep(1500);

        // 3. Cattura screenshot di ipconfig!
        console.log('Cattura screenshot IPCONFIG...');
        client.write(`screendump "${ppmNet.replace(/\\/g, '/')}"\n`);
        await sleep(1000);

        client.end();
        qemu.kill();

        if (fs.existsSync(ppmNet)) {
            execSync(`node tools/ppm2bmp.js "${ppmNet}" "${bmpNet}"`, { stdio: 'ignore' });
            execSync(`powershell -ExecutionPolicy Bypass -File tools/bmp2png.ps1 "${bmpNet}" "${pngNet}"`, { stdio: 'ignore' });
            console.log(`[OK] Generato screenshot: ${pngNet}`);
        }

        process.exit(0);
    });
}, 1500);
