const { spawn } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const imgPath = path.join(ROOT, 'myos.img');
const isoPath = path.join(ROOT, 'NewOS_V1.iso');

function sleep(ms) {
    return new Promise(res => setTimeout(res, ms));
}

async function captureScreen(client, filename) {
    const ppm = path.join(ROOT, filename + '.ppm');
    const bmp = path.join(ROOT, filename + '.bmp');
    const png = path.join(ROOT, filename + '.png');
    if (fs.existsSync(ppm)) fs.unlinkSync(ppm);
    await sleep(300);
    client.write(`screendump "${ppm.replace(/\\/g, '/')}"\n`);
    for (let w = 0; w < 12; w++) {
        await sleep(250);
        if (fs.existsSync(ppm) && fs.statSync(ppm).size > 10000) break;
    }
    if (fs.existsSync(ppm)) {
        require('child_process').execSync(`node "${path.join(ROOT, 'tools', 'ppm2bmp.js')}" "${ppm}" "${bmp}"`);
        require('child_process').execSync(`powershell -ExecutionPolicy Bypass -File "${path.join(ROOT, 'tools', 'bmp2png.ps1')}" "${bmp}" "${png}"`);
        console.log(`[OK] Screenshot catturato: ${png}`);
    } else {
        console.error(`[ERRORE] File ${ppm} non generato`);
    }
}

async function runScrollbarTest() {
    const bootKey = '9';
    const serialLog = path.join(ROOT, 'tools', `serial_scrollbar.log`);
    if (fs.existsSync(serialLog)) {
        try { fs.unlinkSync(serialLog); } catch (e) {}
    }

    const qemu = spawn(qemuBin, [
        '-drive', `format=raw,file=${imgPath}`,
        '-cdrom', isoPath,
        '-m', '256M',
        '-vga', 'std',
        '-audiodev', 'none,id=snd0',
        '-device', 'AC97,audiodev=snd0',
        '-serial', `file:${serialLog}`,
        '-display', 'none',
        '-monitor', `tcp:127.0.0.1:4499,server,nowait`
    ], { stdio: 'ignore' });

    await sleep(1500);

    return new Promise((resolve, reject) => {
        const client = net.createConnection({ port: 4499 }, async () => {
            try {
                console.log(`[TEST] Attesa avvio Boot Menu...`);
                for (let i = 0; i < 40; i++) {
                    await sleep(150);
                    if (fs.existsSync(serialLog)) {
                        const content = fs.readFileSync(serialLog, 'utf8');
                        if (content.includes('[BOOT_MENU] Pronto!')) break;
                    }
                }

                console.log(`[TEST] Boot Menu pronto! Invio tasto '${bootKey}' per aprire Impostazioni...`);
                for (let k = 0; k < 4; k++) {
                    client.write(`sendkey ${bootKey}\n`);
                    await sleep(100);
                }

                // Attesa avvio desktop e caricamento SETTINGS.DLL
                await sleep(8000);

                console.log('1. Cattura screenshot Impostazioni con scrollbar in posizione iniziale (top)...');
                await captureScreen(client, 'screenshot_cp_modern_scrollbar_top');

                console.log('2. Invio scorrimento verso il basso (3 tasti "j" distanziati)...');
                for (let s = 0; s < 3; s++) {
                    client.write('sendkey j\n');
                    await sleep(400);
                }
                await sleep(2000);

                console.log('3. Cattura screenshot Impostazioni dopo scorrimento verso il basso...');
                await captureScreen(client, 'screenshot_cp_modern_scrolled_down');

                console.log('4. Invio scorrimento verso l\'alto (4 tasti "k" distanziati)...');
                for (let s = 0; s < 4; s++) {
                    client.write('sendkey k\n');
                    await sleep(400);
                }
                await sleep(2500);

                console.log('5. Cattura screenshot Impostazioni dopo scorrimento verso l\'alto...');
                await captureScreen(client, 'screenshot_cp_modern_scrolled_up');

                client.write('quit\n');
                await sleep(500);
                try { client.destroy(); } catch (e) {}
                try { qemu.kill(); } catch (e) {}
                resolve();
            } catch (err) {
                try { client.destroy(); } catch (e) {}
                try { qemu.kill(); } catch (e) {}
                reject(err);
            }
        });

        client.on('error', (err) => {
            try { qemu.kill(); } catch (e) {}
            reject(err);
        });
    });
}

runScrollbarTest().then(() => {
    console.log('=== TEST SCROLLBAR COMPLETATO CON SUCCESSO! ===');
    process.exit(0);
}).catch(err => {
    console.error('[ERRORE TEST SCROLLBAR]', err);
    process.exit(1);
});
