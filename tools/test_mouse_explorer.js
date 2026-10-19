const { spawn, execSync } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const isoPath = path.join(ROOT, 'NewOS_V1.iso');
const brainDir = 'C:\\Users\\morga\\.gemini\\antigravity-ide\\brain\\201a47cf-7636-4592-93f8-cb22ec54e1bd';

const ppmMouse1 = path.join(ROOT, 'test_mouse1.ppm');
const bmpMouse1 = path.join(ROOT, 'test_mouse1.bmp');
const pngMouse1 = path.join(brainDir, 'screenshot_mouse_c_opened.png');

console.log('[TEST MOUSE EXPLORER] Test interazione col mouse e doppio clic...');

const qemu = spawn(qemuBin, [
    '-cdrom', isoPath,
    '-m', '128M',
    '-vga', 'std',
    '-netdev', 'user,id=net0',
    '-device', 'e1000,netdev=net0',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4514,server,nowait',
    '-serial', `file:${path.join(ROOT, 'serial_mouse_test.log')}`
], { stdio: 'ignore' });

function sleep(ms) {
    return new Promise(res => setTimeout(res, ms));
}

function convertPpmToBmp(ppmPath, bmpPath) {
    const data = fs.readFileSync(ppmPath);
    let offset = 0;
    while (data[offset] !== 0x0A && offset < data.length) offset++;
    offset++;
    while (data[offset] === 0x23) {
        while (data[offset] !== 0x0A && offset < data.length) offset++;
        offset++;
    }
    let dimStr = '';
    while (data[offset] !== 0x0A && offset < data.length) {
        dimStr += String.fromCharCode(data[offset++]);
    }
    offset++;
    const [w, h] = dimStr.trim().split(/\s+/).map(Number);
    while (data[offset] !== 0x0A && offset < data.length) offset++;
    offset++;

    const rowSize = Math.floor((24 * w + 31) / 32) * 4;
    const pixelArraySize = rowSize * h;
    const fileSize = 54 + pixelArraySize;
    const bmp = Buffer.alloc(fileSize, 0);

    bmp.write('BM', 0, 2, 'ascii');
    bmp.writeUInt32LE(fileSize, 2);
    bmp.writeUInt32LE(54, 10);
    bmp.writeUInt32LE(40, 14);
    bmp.writeInt32LE(w, 18);
    bmp.writeInt32LE(h, 22);
    bmp.writeUInt16LE(1, 26);
    bmp.writeUInt16LE(24, 28);
    bmp.writeUInt32LE(0, 30);
    bmp.writeUInt32LE(pixelArraySize, 34);

    for (let y = 0; y < h; y++) {
        const srcRow = (h - 1 - y) * w * 3 + offset;
        const dstRow = 54 + y * rowSize;
        for (let x = 0; x < w; x++) {
            const r = data[srcRow + x * 3];
            const g = data[srcRow + x * 3 + 1];
            const b = data[srcRow + x * 3 + 2];
            bmp[dstRow + x * 3] = b;
            bmp[dstRow + x * 3 + 1] = g;
            bmp[dstRow + x * 3 + 2] = r;
        }
    }
    fs.writeFileSync(bmpPath, bmp);
}

setTimeout(() => {
    const client = net.createConnection({ port: 4514 }, async () => {
        console.log('Connesso al monitor QEMU...');
        await sleep(1500);

        // 1. Boot option 1 (Desktop Live)
        console.log('Selezione boot option 1 (Desktop Live)...');
        client.write('sendkey 1\n');
        await sleep(3500);

        // 2. Apri Risorse PC dal desktop (clic a x=36, y=36)
        console.log('Clic su icona Desktop Risorse PC...');
        client.write('mouse_move 36 36\n');
        await sleep(100);
        client.write('mouse_button 1\n');
        await sleep(50);
        client.write('mouse_button 0\n');
        await sleep(1500);

        // 3. Ora la finestra Risorse del Computer è aperta a (80, 45)!
        // Facciamo clic sul pulsante [ Apri C:\ ] nella barra laterale a x=164, y=362
        console.log('Clic sul pulsante [ Apri C:\\ ] nella barra laterale...');
        client.write('mouse_move 164 362\n');
        await sleep(100);
        client.write('mouse_button 1\n');
        await sleep(50);
        client.write('mouse_button 0\n');
        await sleep(1500);

        // 4. Screenshot per confermare che C:\ si è aperta tramite clic del mouse
        console.log('Cattura Screenshot: C:\\ aperta con successo...');
        client.write(`screendump "${ppmMouse1.replace(/\\/g, '/')}"\n`);
        await sleep(1500);

        if (fs.existsSync(ppmMouse1)) {
            convertPpmToBmp(ppmMouse1, bmpMouse1);
            execSync(`powershell -Command "Add-Type -AssemblyName System.Drawing; [System.Drawing.Bitmap]::FromFile('${bmpMouse1}').Save('${pngMouse1}', [System.Drawing.Imaging.ImageFormat]::Png)"`);
            console.log(`[PNG] Salvato: ${pngMouse1}`);
        }

        console.log('Test completato! Chiusura QEMU...');
        client.end();
        qemu.kill();
        process.exit(0);
    });
    client.on('error', () => {});
}, 2000);
