const { spawn } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const isoPath = path.join(ROOT, 'NewOS_V1.iso');
const brainDir = 'C:\\Users\\morga\\.gemini\\antigravity-ide\\brain\\201a47cf-7636-4592-93f8-cb22ec54e1bd';

const ppmTimeout = path.join(ROOT, 'test_timeout.ppm');
const pngTimeout = path.join(brainDir, 'screenshot_network_ping_timeout.png');

console.log('[TEST TIMEOUT] Avvio QEMU per test ping IP inesistente (10.0.2.99)...');

const qemu = spawn(qemuBin, [
    '-cdrom', isoPath,
    '-m', '128M',
    '-vga', 'std',
    '-netdev', 'user,id=net0',
    '-device', 'e1000,netdev=net0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4555,server,nowait'
], { stdio: 'ignore' });

function sleep(ms) {
    return new Promise(res => setTimeout(res, ms));
}

setTimeout(() => {
    const client = net.createConnection({ port: 4555 }, async () => {
        await sleep(1500);

        // Opzione 4: CMD
        client.write('sendkey 4\n');
        await sleep(2500);

        // Ping IP inesistente: 10.0.2.99
        console.log('Invio comando "ping 10.0.2.99"...');
        const cmdPing = 'ping 10.0.2.99';
        for (const c of cmdPing) {
            if (c === ' ') client.write('sendkey spc\n');
            else if (c === '.') client.write('sendkey dot\n');
            else client.write(`sendkey ${c}\n`);
            await sleep(50);
        }
        client.write('sendkey ret\n');
        await sleep(6000); // Attendi timeout delle 4 richieste

        client.write(`screendump "${ppmTimeout.replace(/\\/g, '/')}"\n`);
        await sleep(1000);

        client.end();
        qemu.kill();

        console.log('Conversione screenshot timeout...');
        const bmpFile = path.join(ROOT, 'test_timeout.bmp');
        if (fs.existsSync(ppmTimeout)) {
            const data = fs.readFileSync(ppmTimeout);
            let offset = 0;
            function readLine() {
                let end = data.indexOf(10, offset);
                if (end === -1) end = data.length;
                const line = data.toString('ascii', offset, end).trim();
                offset = end + 1;
                return line;
            }
            readLine();
            let dims = readLine();
            while (dims.startsWith('#')) dims = readLine();
            const [w, h] = dims.split(/\s+/).map(Number);
            readLine();
            const pixelData = data.slice(offset);

            const rowSize = Math.floor((24 * w + 31) / 32) * 4;
            const imageSize = rowSize * h;
            const fileSize = 54 + imageSize;
            const bmp = Buffer.alloc(fileSize);

            bmp.write('BM', 0);
            bmp.writeUInt32LE(fileSize, 2);
            bmp.writeUInt32LE(54, 10);
            bmp.writeUInt32LE(40, 14);
            bmp.writeInt32LE(w, 18);
            bmp.writeInt32LE(h, 22);
            bmp.writeUInt16LE(1, 26);
            bmp.writeUInt16LE(24, 28);
            bmp.writeUInt32LE(0, 30);
            bmp.writeUInt32LE(imageSize, 34);

            for (let y = 0; y < h; y++) {
                const srcRow = y;
                const dstRow = h - 1 - y;
                for (let x = 0; x < w; x++) {
                    const srcIdx = (srcRow * w + x) * 3;
                    const dstIdx = 54 + dstRow * rowSize + x * 3;
                    bmp[dstIdx] = pixelData[srcIdx + 2];
                    bmp[dstIdx + 1] = pixelData[srcIdx + 1];
                    bmp[dstIdx + 2] = pixelData[srcIdx];
                }
            }
            fs.writeFileSync(bmpFile, bmp);
            const { execSync } = require('child_process');
            execSync(`powershell -Command "Add-Type -AssemblyName System.Drawing; [System.Drawing.Bitmap]::FromFile('${bmpFile}').Save('${pngTimeout}', [System.Drawing.Imaging.ImageFormat]::Png)"`);
            console.log(`Generato: ${pngTimeout}`);
        }
        process.exit(0);
    });

    client.on('error', (err) => {
        console.error('Errore:', err.message);
        qemu.kill();
        process.exit(1);
    });
}, 1000);
