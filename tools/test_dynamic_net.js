const { spawn } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const isoPath = path.join(ROOT, 'NewOS_V1.iso');
const brainDir = 'C:\\Users\\morga\\.gemini\\antigravity-ide\\brain\\201a47cf-7636-4592-93f8-cb22ec54e1bd';

const serialLogPath = path.join(ROOT, 'serial_net.log');
if (fs.existsSync(serialLogPath)) fs.unlinkSync(serialLogPath);

const ppmIpconfig = path.join(ROOT, 'test_ipconfig.ppm');
const ppmPing = path.join(ROOT, 'test_ping.ppm');
const ppmNetstat = path.join(ROOT, 'test_netstat.ppm');

console.log('[TEST NET] Avvio QEMU con Intel E1000 e SLIRP DHCP...');

const qemu = spawn(qemuBin, [
    '-cdrom', isoPath,
    '-m', '128M',
    '-vga', 'std',
    '-netdev', 'user,id=net0',
    '-device', 'e1000,netdev=net0',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-serial', `file:${serialLogPath}`,
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4550,server,nowait'
], { stdio: 'ignore' });

function sleep(ms) {
    return new Promise(res => setTimeout(res, ms));
}

setTimeout(() => {
    const client = net.createConnection({ port: 4550 }, async () => {
        console.log('Connesso al monitor di QEMU...');
        await sleep(1500);

        // Seleziona opzione 4 nel boot menu: Prompt dei comandi
        client.write('sendkey 4\n');
        await sleep(2500);

        // 1. Esegui "ipconfig"
        console.log('Invio comando "ipconfig"...');
        for (const c of 'ipconfig') {
            client.write(`sendkey ${c}\n`);
            await sleep(50);
        }
        client.write('sendkey ret\n');
        await sleep(2000);

        client.write(`screendump "${ppmIpconfig.replace(/\\/g, '/')}"\n`);
        await sleep(1000);

        // 2. Esegui "ping 10.0.2.2"
        console.log('Invio comando "ping 10.0.2.2"...');
        const cmdPing = 'ping 10.0.2.2';
        for (const c of cmdPing) {
            if (c === ' ') client.write('sendkey spc\n');
            else if (c === '.') client.write('sendkey dot\n');
            else client.write(`sendkey ${c}\n`);
            await sleep(50);
        }
        client.write('sendkey ret\n');
        await sleep(4000);

        client.write(`screendump "${ppmPing.replace(/\\/g, '/')}"\n`);
        await sleep(1000);

        // 3. Esegui "netstat"
        console.log('Invio comando "netstat"...');
        for (const c of 'netstat') {
            client.write(`sendkey ${c}\n`);
            await sleep(50);
        }
        client.write('sendkey ret\n');
        await sleep(2000);

        client.write(`screendump "${ppmNetstat.replace(/\\/g, '/')}"\n`);
        await sleep(1000);

        client.end();
        qemu.kill();

        console.log('Test completato! Converto immagini...');
        try {
            // Conversione ppm in png usando node
            function ppmToBmp(ppmFile, bmpFile, pngFile) {
                if (!fs.existsSync(ppmFile)) return;
                const data = fs.readFileSync(ppmFile);
                let offset = 0;
                function readLine() {
                    let end = data.indexOf(10, offset);
                    if (end === -1) end = data.length;
                    const line = data.toString('ascii', offset, end).trim();
                    offset = end + 1;
                    return line;
                }
                const magic = readLine();
                let dims = readLine();
                while (dims.startsWith('#')) dims = readLine();
                const [w, h] = dims.split(/\s+/).map(Number);
                readLine(); // maxval
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
                        const r = pixelData[srcIdx];
                        const g = pixelData[srcIdx + 1];
                        const b = pixelData[srcIdx + 2];
                        bmp[dstIdx] = b;
                        bmp[dstIdx + 1] = g;
                        bmp[dstIdx + 2] = r;
                    }
                }
                fs.writeFileSync(bmpFile, bmp);
                const { execSync } = require('child_process');
                execSync(`powershell -Command "Add-Type -AssemblyName System.Drawing; [System.Drawing.Bitmap]::FromFile('${bmpFile}').Save('${pngFile}', [System.Drawing.Imaging.ImageFormat]::Png)"`);
                console.log(`Generato: ${pngFile}`);
            }

            ppmToBmp(ppmIpconfig, path.join(ROOT, 'test_ipconfig.bmp'), path.join(brainDir, 'screenshot_network_ipconfig.png'));
            ppmToBmp(ppmPing, path.join(ROOT, 'test_ping.bmp'), path.join(brainDir, 'screenshot_network_ping.png'));
            ppmToBmp(ppmNetstat, path.join(ROOT, 'test_netstat.bmp'), path.join(brainDir, 'screenshot_network_netstat.png'));
        } catch (e) {
            console.error('Errore conversione:', e.message);
        }

        if (fs.existsSync(serialLogPath)) {
            console.log('=== LOG SERIALE REGISTRATO DALLA MACCHINA VIRTUALE ===');
            console.log(fs.readFileSync(serialLogPath, 'utf8'));
        }
        process.exit(0);
    });

    client.on('error', (err) => {
        console.error('Connessione monitor fallita:', err.message);
        qemu.kill();
        process.exit(1);
    });
}, 1000);
