const { spawn, execSync } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const isoPath = path.join(ROOT, 'NewOS_V1.iso');
const brainDir = 'C:\\Users\\morga\\.gemini\\antigravity-ide\\brain\\201a47cf-7636-4592-93f8-cb22ec54e1bd';

const ppmCmdRes = path.join(ROOT, 'test_cmd_res.ppm');
const bmpCmdRes = path.join(ROOT, 'test_cmd_res.bmp');
const pngCmdRes = path.join(brainDir, 'screenshot_cmd_res.png');

console.log('[TEST CMD RES] Avvio QEMU con NewOS_V1.iso...');

const qemu = spawn(qemuBin, [
    '-cdrom', isoPath,
    '-m', '128M',
    '-vga', 'std',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4498,server,nowait'
], { stdio: 'ignore' });

function sleep(ms) {
    return new Promise(res => setTimeout(res, ms));
}

setTimeout(() => {
    const client = net.createConnection({ port: 4498 }, async () => {
        console.log('Connesso al monitor QEMU...');
        await sleep(1500);

        // Seleziona boot option 4 (Prompt dei Comandi CLI)
        console.log('Avvio boot option 4 (Prompt dei Comandi)...');
        client.write('sendkey 4\n');
        await sleep(2500);

        // Digita "res" e premi invio nel terminale
        console.log('Digitazione comando "res"...');
        client.write('sendkey r\n');
        await sleep(100);
        client.write('sendkey e\n');
        await sleep(100);
        client.write('sendkey s\n');
        await sleep(100);
        client.write('sendkey ret\n');
        await sleep(1000);

        // Cattura screenshot del Prompt dei Comandi con output di res
        console.log('Cattura screenshot Prompt dei Comandi con comando res...');
        client.write(`screendump "${ppmCmdRes.replace(/\\/g, '/')}"\n`);
        await sleep(1000);

        client.end();
        qemu.kill();

        if (fs.existsSync(ppmCmdRes)) {
            execSync(`node tools/ppm2bmp.js "${ppmCmdRes}" "${bmpCmdRes}"`, { stdio: 'ignore' });
            execSync(`powershell -ExecutionPolicy Bypass -File tools/bmp2png.ps1 "${bmpCmdRes}" "${pngCmdRes}"`, { stdio: 'ignore' });
            console.log(`[OK] Generato screenshot: ${pngCmdRes}`);
        }

        process.exit(0);
    });
}, 1500);
