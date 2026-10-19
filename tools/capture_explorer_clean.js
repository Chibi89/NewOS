const { spawn, execSync } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const isoPath = path.join(ROOT, 'NewOS_V1.iso');
const brainDir = 'C:\\Users\\morga\\.gemini\\antigravity-ide\\brain\\327df2b4-88f2-409e-bc1c-ac357035f353';

function sleep(ms) { return new Promise(res => setTimeout(res, ms)); }

function convertPpmToPng(ppmFile, bmpFile, pngFile) {
    if (fs.existsSync(ppmFile)) {
        execSync(`node tools/ppm2bmp.js "${ppmFile}" "${bmpFile}"`, { stdio: 'ignore' });
        execSync(`powershell -ExecutionPolicy Bypass -File tools/bmp2png.ps1 "${bmpFile}" "${pngFile}"`, { stdio: 'ignore' });
        console.log(`[OK] PNG: ${pngFile} (${fs.statSync(pngFile).size} byte)`);
    }
}

async function run() {
    console.log('[CAPTURE] Avvio QEMU con boot_choice 6 (Explorer puro)...');
    const qemu = spawn(qemuBin, [
        '-cdrom', isoPath,
        '-m', '128M',
        '-vga', 'std',
        '-netdev', 'user,id=net0',
        '-device', 'e1000,netdev=net0',
        '-audiodev', 'none,id=snd0',
        '-device', 'AC97,audiodev=snd0',
        '-display', 'none',
        '-monitor', 'tcp:127.0.0.1:4629,server,nowait',
        '-serial', `file:${path.join(ROOT, 'serial_explorer_clean.log')}`
    ], { stdio: 'ignore' });

    setTimeout(() => {
        const client = net.createConnection({ port: 4629 }, async () => {
            console.log('[CAPTURE] Connesso al monitor QEMU...');
            await sleep(1500);

            // Invia scelta '6' al boot menu
            client.write('sendkey 6\n');
            console.log('[CAPTURE] Scelta 6 inviata. Attesa caricamento desktop ed Explorer...');
            await sleep(4000);

            const ppm = path.join(brainDir, 'screen_explorer_this_pc_clean.ppm');
            const bmp = path.join(brainDir, 'screen_explorer_this_pc_clean.bmp');
            const png = path.join(brainDir, 'screen_explorer_this_pc_clean.png');

            client.write(`screendump ${ppm.replace(/\\/g, '/')}\n`);
            await sleep(1500);

            client.write('quit\n');
            await sleep(500);
            qemu.kill();

            convertPpmToPng(ppm, bmp, png);
            try { fs.unlinkSync(ppm); fs.unlinkSync(bmp); } catch(e){}
            console.log('[CAPTURE] Screenshot Explorer completato con successo!');
            process.exit(0);
        });

        client.on('error', (err) => {
            console.error('[ERR]', err.message);
            qemu.kill();
            process.exit(1);
        });
    }, 1500);
}

run();
