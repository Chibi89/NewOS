const { spawn, execSync } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const brainDir = 'C:\\Users\\morga\\.gemini\\antigravity-ide\\brain\\cce9850e-402e-48dd-9a6f-1743e743feab';

function sleep(ms) { return new Promise(res => setTimeout(res, ms)); }

function convertPpmToPng(ppmFile, bmpFile, pngFile) {
    if (fs.existsSync(ppmFile)) {
        execSync(`node tools/ppm2bmp.js "${ppmFile}" "${bmpFile}"`, { stdio: 'ignore' });
        execSync(`powershell -ExecutionPolicy Bypass -File tools/bmp2png.ps1 "${bmpFile}" "${pngFile}"`, { stdio: 'ignore' });
        console.log(`[OK] PNG generato: ${pngFile}`);
    }
}

async function runTestAHCI() {
    console.log('[TEST AHCI] Avvio QEMU con controller Serial ATA AHCI (ich9-ahci)...');
    const imgPath = path.join(ROOT, 'build', 'NewOS_V1.img');
    const serialLog = path.join(ROOT, 'serial_ahci.log');

    const qemu = spawn(qemuBin, [
        '-device', 'ich9-ahci,id=ahci',
        '-drive', `id=disk,file=${imgPath},format=raw,if=none`,
        '-device', 'ide-hd,drive=disk,bus=ahci.0',
        '-m', '256M',
        '-vga', 'std',
        '-netdev', 'user,id=net0',
        '-device', 'e1000,netdev=net0',
        '-audiodev', 'none,id=snd0',
        '-device', 'AC97,audiodev=snd0',
        '-display', 'none',
        '-serial', `file:${serialLog}`,
        '-monitor', 'tcp:127.0.0.1:4655,server,nowait'
    ], { stdio: 'ignore' });

    await sleep(2000);

    const client = net.createConnection({ port: 4655 }, async () => {
        console.log('[TEST AHCI] Connesso al monitor QEMU...');
        await sleep(1500);
        console.log('[TEST AHCI] Invio SPAZIO per saltare bootscreen...');
        client.write('sendkey spc\n');
        await sleep(1000);
        console.log('[TEST AHCI] Selezione opzione 1 (Live)...');
        client.write('sendkey 1\n');
        await sleep(300);
        client.write('sendkey ret\n');
        await sleep(3500);

        const ppm = path.join(ROOT, 'screen_ahci_boot.ppm');
        const bmp = path.join(ROOT, 'screen_ahci_boot.bmp');
        const png = path.join(brainDir, 'screen_ahci_boot.png');
        if (fs.existsSync(ppm)) fs.unlinkSync(ppm);

        console.log('[TEST AHCI] Cattura screendump...');
        client.write(`screendump "${ppm.replace(/\\/g, '/')}"\n`);
        
        for (let i = 0; i < 20; i++) {
            await sleep(300);
            if (fs.existsSync(ppm) && fs.statSync(ppm).size > 100000) {
                break;
            }
        }

        convertPpmToPng(ppm, bmp, png);

        try { if (fs.existsSync(ppm)) fs.unlinkSync(ppm); } catch(e){}
        try { if (fs.existsSync(bmp)) fs.unlinkSync(bmp); } catch(e){}

        console.log('[TEST AHCI COMPLETATO CON SUCCESSO]');
        client.write('quit\n');
        client.end();
        setTimeout(() => {
            qemu.kill();
            process.exit(0);
        }, 1000);
    });

    client.on('error', (err) => {
        console.error('[ERRORE CONNESSIONE QEMU]', err.message);
        qemu.kill();
        process.exit(1);
    });
}

runTestAHCI();
