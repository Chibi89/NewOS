const { spawn } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const imgPath = path.join(ROOT, 'build', 'NewOS_V1.img');
const isoPath = path.join(ROOT, 'build', 'NewOS_V1.iso');

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
    for (let w = 0; w < 16; w++) {
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

async function runTest() {
    const serialLog = path.join(ROOT, 'serial_block_a.log');
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
        '-monitor', `tcp:127.0.0.1:4488,server,nowait`
    ], { stdio: 'ignore' });

    await sleep(1500);

    return new Promise((resolve, reject) => {
        const client = net.createConnection({ port: 4488 }, async () => {
            try {
                console.log(`[TEST] Attesa avvio Boot Menu...`);
                for (let i = 0; i < 40; i++) {
                    await sleep(150);
                    if (fs.existsSync(serialLog)) {
                        const content = fs.readFileSync(serialLog, 'utf8');
                        if (content.includes('[BOOT_MENU] Pronto!')) break;
                    }
                }

                console.log(`[TEST] Boot Menu pronto! Invio tasto '1' per avvio Desktop...`);
                client.write('sendkey 1\n');
                await sleep(100);
                client.write('sendkey ret\n');

                // Attesa avvio bootscreen, login e desktop
                console.log(`[TEST] Attesa avvio ambiente Desktop (7s)...`);
                await sleep(7000);

                console.log(`[TEST] Cattura screenshot Desktop con Kernel alleggerito (Blocco A completato)...`);
                await captureScreen(client, 'screenshot_block_a_desktop');

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

runTest().then(() => {
    console.log('[TEST] Collaudo Blocco A completato con successo.');
    process.exit(0);
}).catch(err => {
    console.error('[TEST FATAL]', err);
    process.exit(1);
});
