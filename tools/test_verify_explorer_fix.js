const { spawn, execSync } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const isoPath = path.join(ROOT, 'NewOS_V1.iso');

function sleep(ms) { return new Promise(res => setTimeout(res, ms)); }

function convertPpmToPng(ppmFile, bmpFile, pngFile) {
    if (fs.existsSync(ppmFile)) {
        execSync(`node tools/ppm2bmp.js "${ppmFile}" "${bmpFile}"`, { stdio: 'ignore' });
        execSync(`powershell -ExecutionPolicy Bypass -File tools/bmp2png.ps1 "${bmpFile}" "${pngFile}"`, { stdio: 'ignore' });
        console.log(`[OK] PNG generato: ${pngFile}`);
    }
}

async function run() {
    console.log('[TEST] Avvio QEMU per verifica navigazione Explorer (INVIO)...');
    const qemu = spawn(qemuBin, [
        '-cdrom', isoPath,
        '-m', '256M',
        '-vga', 'std',
        '-netdev', 'user,id=net0',
        '-device', 'e1000,netdev=net0',
        '-audiodev', 'none,id=snd0',
        '-device', 'AC97,audiodev=snd0',
        '-display', 'none',
        '-monitor', 'tcp:127.0.0.1:4638,server,nowait'
    ], { stdio: 'ignore' });

    await sleep(2500);

    const client = net.createConnection({ port: 4638 }, async () => {
        console.log('[TEST] Attesa avvio desktop...');
        await sleep(4500);

        async function capture(tag) {
            const ppm = path.join(ROOT, tag + '.ppm');
            const bmp = path.join(ROOT, tag + '.bmp');
            const png = path.join(ROOT, tag + '.png');
            if (fs.existsSync(ppm)) fs.unlinkSync(ppm);
            client.write(`screendump "${ppm.replace(/\\/g, '/')}"\n`);
            await sleep(1000);
            convertPpmToPng(ppm, bmp, png);
            try { if (fs.existsSync(ppm)) fs.unlinkSync(ppm); } catch(e){}
            try { if (fs.existsSync(bmp)) fs.unlinkSync(bmp); } catch(e){}
        }

        // 1. Cattura Risorse del Computer
        console.log('[TEST] Cattura Risorse del computer...');
        await capture('screen_test_this_pc');

        // 2. Premi INVIO per aprire Disco locale (C:)
        console.log('[TEST] Invio tasto RET per aprire Disco locale (C:)...');
        client.write('sendkey ret\n');
        await sleep(1000);

        // 3. Cattura C:\
        console.log('[TEST] Cattura Disco locale (C:)...');
        await capture('screen_test_drivec');

        // 4. In C:\, Item 0 è "NewOS". Premi INVIO per aprirlo
        console.log('[TEST] Invio tasto RET per entrare in NewOS...');
        client.write('sendkey ret\n');
        await sleep(1000);

        // 5. Cattura C:\NewOS
        console.log('[TEST] Cattura cartella NewOS...');
        await capture('screen_test_folder_newos');

        console.log('[TEST] Test completato con successo!');
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
