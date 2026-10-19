const { spawn, execSync } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const isoPath = path.join(ROOT, 'NewOS_V1.iso');
const brainDir = 'C:\\Users\\morga\\.gemini\\antigravity-ide\\brain\\327df2b4-88f2-409e-bc1c-ac357035f353';

function sleep(ms) { return new Promise(res => setTimeout(res, ms)); }

async function dump(client, name) {
    const ppm = path.join(brainDir, `${name}.ppm`);
    const bmp = path.join(brainDir, `${name}.bmp`);
    const png = path.join(brainDir, `${name}.png`);
    client.write(`screendump ${ppm.replace(/\\/g, '/')}\n`);
    await sleep(600);
    if (fs.existsSync(ppm)) {
        execSync(`node tools/ppm2bmp.js "${ppm}" "${bmp}"`, { stdio: 'ignore' });
        execSync(`powershell -ExecutionPolicy Bypass -File tools/bmp2png.ps1 "${bmp}" "${png}"`, { stdio: 'ignore' });
        try { fs.unlinkSync(ppm); fs.unlinkSync(bmp); } catch(e){}
        console.log(`[DUMP] ${png}`);
    }
}

async function run() {
    console.log('[TIMELINE] Avvio QEMU...');
    const qemu = spawn(qemuBin, [
        '-cdrom', isoPath,
        '-m', '128M',
        '-vga', 'std',
        '-device', 'piix3-usb-uhci',
        '-device', 'qemu-xhci',
        '-display', 'none',
        '-monitor', 'tcp:127.0.0.1:4590,server,nowait'
    ], { stdio: 'ignore' });

    setTimeout(() => {
        const client = net.createConnection({ port: 4590 }, async () => {
            console.log('[TIMELINE] Connesso monitor.');
            await sleep(1500);
            await dump(client, 'boot_t1_1500ms');

            console.log('[TIMELINE] Invio sendkey 4...');
            client.write('sendkey 4\n');
            await sleep(1500);
            await dump(client, 'boot_t2_3000ms');

            console.log('[TIMELINE] Invio explorer...');
            for (const c of 'explorer') {
                client.write(`sendkey ${c}\n`);
                await sleep(60);
            }
            client.write('sendkey ret\n');
            await sleep(2500);
            await dump(client, 'boot_t3_explorer_open');

            client.write('quit\n');
            await sleep(400);
            qemu.kill();
            process.exit(0);
        });
        client.on('error', (err) => { console.error(err); qemu.kill(); process.exit(1); });
    }, 1500);
}

run().catch(console.error);
