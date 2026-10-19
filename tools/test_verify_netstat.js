const { spawn, execSync } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const isoPath = path.join(ROOT, 'NewOS_V1.iso');
const brainDir = 'C:\\Users\\morga\\.gemini\\antigravity-ide\\brain\\201a47cf-7636-4592-93f8-cb22ec54e1bd';

const ppmStat = path.join(ROOT, 'test_netstat.ppm');
const bmpStat = path.join(ROOT, 'test_netstat.bmp');
const pngStat = path.join(brainDir, 'screenshot_network_netstat.png');

console.log('[TEST NETSTAT] Avvio QEMU con test comandi NETSTAT e DHCP...');

const qemu = spawn(qemuBin, [
    '-cdrom', isoPath,
    '-m', '128M',
    '-vga', 'std',
    '-netdev', 'user,id=net0',
    '-device', 'e1000,netdev=net0',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4502,server,nowait'
], { stdio: 'ignore' });

function sleep(ms) {
    return new Promise(res => setTimeout(res, ms));
}

setTimeout(() => {
    const client = net.createConnection({ port: 4502 }, async () => {
        console.log('Connesso a QEMU...');
        await sleep(1500);

        // Boot option 4: Prompt dei Comandi
        client.write('sendkey 4\n');
        await sleep(2500);

        // Digita "netstat"
        console.log('Invio comando "netstat"...');
        const cmd1 = 'netstat';
        for (const c of cmd1) {
            client.write(`sendkey ${c}\n`);
            await sleep(60);
        }
        client.write('sendkey ret\n');
        await sleep(1200);

        // Digita "dhcp"
        console.log('Invio comando "dhcp"...');
        const cmd2 = 'dhcp';
        for (const c of cmd2) {
            client.write(`sendkey ${c}\n`);
            await sleep(60);
        }
        client.write('sendkey ret\n');
        await sleep(1200);

        // Cattura screenshot di NETSTAT e DHCP!
        client.write(`screendump "${ppmStat.replace(/\\/g, '/')}"\n`);
        await sleep(1000);

        client.end();
        qemu.kill();

        if (fs.existsSync(ppmStat)) {
            execSync(`node tools/ppm2bmp.js "${ppmStat}" "${bmpStat}"`, { stdio: 'ignore' });
            execSync(`powershell -ExecutionPolicy Bypass -File tools/bmp2png.ps1 "${bmpStat}" "${pngStat}"`, { stdio: 'ignore' });
            console.log(`[OK] Generato screenshot: ${pngStat}`);
        }

        process.exit(0);
    });
}, 1500);
