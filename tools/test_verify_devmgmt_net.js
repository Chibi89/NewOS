const { spawn, execSync } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const isoPath = path.join(ROOT, 'NewOS_V1.iso');
const brainDir = 'C:\\Users\\morga\\.gemini\\antigravity-ide\\brain\\201a47cf-7636-4592-93f8-cb22ec54e1bd';

const ppmDev = path.join(ROOT, 'test_devmgmt_net.ppm');
const bmpDev = path.join(ROOT, 'test_devmgmt_net.bmp');
const pngDev = path.join(brainDir, 'screenshot_network_devmgmt.png');

console.log('[TEST DEVMGMT NET] Avvio QEMU con Gestione Periferiche...');

const qemu = spawn(qemuBin, [
    '-cdrom', isoPath,
    '-m', '128M',
    '-vga', 'std',
    '-netdev', 'user,id=net0',
    '-device', 'e1000,netdev=net0',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4501,server,nowait'
], { stdio: 'ignore' });

function sleep(ms) {
    return new Promise(res => setTimeout(res, ms));
}

setTimeout(() => {
    const client = net.createConnection({ port: 4501 }, async () => {
        console.log('Connesso a QEMU...');
        await sleep(1500);

        // Boot option 4: Prompt dei Comandi
        client.write('sendkey 4\n');
        await sleep(2500);

        // Digita "devmgmt" per aprire Gestione Periferiche
        console.log('Apertura devmgmt...');
        const cmd = 'devmgmt';
        for (const c of cmd) {
            client.write(`sendkey ${c}\n`);
            await sleep(60);
        }
        client.write('sendkey ret\n');
        await sleep(1500);

        // Naviga verso il basso per evidenziare il driver di rete E1000.SYS
        // 8 posizioni verso il basso
        for (let i = 0; i < 8; i++) {
            client.write('sendkey s\n');
            await sleep(80);
        }
        await sleep(800);

        // Cattura screenshot di Gestione Periferiche con scheda di rete!
        client.write(`screendump "${ppmDev.replace(/\\/g, '/')}"\n`);
        await sleep(1000);

        client.end();
        qemu.kill();

        if (fs.existsSync(ppmDev)) {
            execSync(`node tools/ppm2bmp.js "${ppmDev}" "${bmpDev}"`, { stdio: 'ignore' });
            execSync(`powershell -ExecutionPolicy Bypass -File tools/bmp2png.ps1 "${bmpDev}" "${pngDev}"`, { stdio: 'ignore' });
            console.log(`[OK] Generato screenshot: ${pngDev}`);
        }

        process.exit(0);
    });
}, 1500);
