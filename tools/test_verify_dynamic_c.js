const { spawn, execSync } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const isoPath = path.join(ROOT, 'NewOS_V1.iso');
const brainDir = 'C:\\Users\\morga\\.gemini\\antigravity-ide\\brain\\cce9850e-402e-48dd-9a6f-1743e743feab';

function sleep(ms) { return new Promise(res => setTimeout(res, ms)); }

function convertPpmToPng(ppmFile, bmpFile, pngFile) {
    if (fs.existsSync(ppmFile)) {
        execSync(`node tools/ppm2bmp.js "${ppmFile}" "${bmpFile}"`, { stdio: 'ignore' });
        execSync(`powershell -ExecutionPolicy Bypass -File tools/bmp2png.ps1 "${bmpFile}" "${pngFile}"`, { stdio: 'ignore' });
        console.log(`[OK] PNG generato: ${pngFile}`);
    }
}

async function run() {
    console.log('[TEST] Avvio QEMU per collaudo dinamico autorilevamento C:\\...');
    const imgPath = path.join(ROOT, 'build', 'NewOS_V1.img');
    const qemu = spawn(qemuBin, [
        '-drive', `format=raw,file=${imgPath}`,
        '-m', '256M',
        '-vga', 'std',
        '-netdev', 'user,id=net0',
        '-device', 'e1000,netdev=net0',
        '-audiodev', 'none,id=snd0',
        '-device', 'AC97,audiodev=snd0',
        '-display', 'none',
        '-serial', `file:${path.join(ROOT, 'serial_debug.log')}`,
        '-monitor', 'tcp:127.0.0.1:4648,server,nowait'
    ], { stdio: 'ignore' });

    await sleep(2500);

    const client = net.createConnection({ port: 4648 }, async () => {
        console.log('[TEST] Connesso al monitor QEMU. Superamento rapido del boot menu...');
        await sleep(1000);
        client.write('sendkey ret\n'); // Seleziona opzione 1 Live nel boot menu
        console.log('[TEST] Attesa caricamento desktop...');
        await sleep(4000);

        async function capture(tag) {
            const ppm = path.join(ROOT, tag + '.ppm');
            const bmp = path.join(ROOT, tag + '.bmp');
            const png = path.join(brainDir, tag + '.png');
            if (fs.existsSync(ppm)) fs.unlinkSync(ppm);
            client.write(`screendump "${ppm.replace(/\\/g, '/')}"\n`);
            await sleep(1500);
            convertPpmToPng(ppm, bmp, png);
            try { if (fs.existsSync(ppm)) fs.unlinkSync(ppm); } catch(e){}
            try { if (fs.existsSync(bmp)) fs.unlinkSync(bmp); } catch(e){}
            return png;
        }

        // 1. Cattura Risorse del computer iniziale
        console.log('[TEST] Cattura Risorse del computer...');
        await capture('screen_this_pc_live');

        // 2. Apri Disco locale (C:) premendo Invio (è il primo elemento selezionato)
        console.log('[TEST] Apertura Disco locale (C:) via Invio...');
        client.write('sendkey ret\n');
        await sleep(2500);

        // 3. Cattura Disco locale (C:) dinamico
        console.log('[TEST] Cattura C:\\ ad autorilevamento dinamico...');
        await capture('screen_dynamic_c_root');

        // 4. Naviga sulla cartella BootSetup (due caselle a destra) ed entra
        console.log('[TEST] Navigazione nella cartella BootSetup...');
        client.write('sendkey right\n');
        await sleep(400);
        client.write('sendkey right\n');
        await sleep(400);
        client.write('sendkey ret\n');
        await sleep(2500);
        await capture('screen_dynamic_subfolder');

        console.log('[TEST] Collaudo completato!');
        client.write('quit\n');
        setTimeout(() => {
            try { client.destroy(); } catch (e) {}
            try { qemu.kill(); } catch (e) {}
            process.exit(0);
        }, 500);
    });

    client.on('error', (err) => {
        console.error('[TEST ERRORE]', err.message);
        try { qemu.kill(); } catch(e) {}
        process.exit(1);
    });
}

run();
