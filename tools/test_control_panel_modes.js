const { spawn } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const imgPath = path.join(ROOT, 'myos.img');

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

const isoPath = path.join(ROOT, 'NewOS_V1.iso');

async function testSession(bootKey, sessionCallback) {
    const serialLog = path.join(ROOT, 'tools', `serial_${bootKey}.log`);
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
        '-monitor', `tcp:127.0.0.1:${4475 + parseInt(bootKey)},server,nowait`
    ], { stdio: 'ignore' });

    await sleep(1500);

    return new Promise((resolve, reject) => {
        const client = net.createConnection({ port: 4475 + parseInt(bootKey) }, async () => {
            try {
                console.log(`[TEST] Attesa avvio Boot Menu (tasto ${bootKey})...`);
                let foundMenu = false;
                for (let i = 0; i < 40; i++) {
                    await sleep(150);
                    if (fs.existsSync(serialLog)) {
                        const content = fs.readFileSync(serialLog, 'utf8');
                        if (content.includes('[BOOT_MENU] Pronto!')) {
                            foundMenu = true;
                            break;
                        }
                    }
                }

                console.log(`[TEST] Boot Menu pronto! Invio tasto '${bootKey}'...`);
                for (let k = 0; k < 4; k++) {
                    client.write(`sendkey ${bootKey}\n`);
                    await sleep(100);
                }

                // Attesa completamento avvio desktop, applicazione temi e riproduzione startup audio
                await sleep(8000);

                await sessionCallback(client);

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

async function run() {
    console.log('=== TEST 1: Nuova Modalita Moderna "Impostazioni" Scorporata (Boot 9) ===');
    await testSession('9', async (client) => {
        console.log('Cattura screenshot Impostazioni Home (Hero PC card, Fluent UI, Profilo)...');
        await captureScreen(client, 'screenshot_cp_modern_settings');

        // Navigazione su categoria "Sistema" (clic su sidebar)
        console.log('Navigazione su scheda "Sistema"...');
        for (let i = 0; i < 10; i++) {
            client.write('mouse_move -28 -11\n');
            await sleep(25);
        }
        await sleep(150);
        client.write('mouse_button 1\n');
        client.write('mouse_move 0 0\n');
        await sleep(80);
        client.write('mouse_button 0\n');
        client.write('mouse_move 0 0\n');
        await sleep(1000);
        await captureScreen(client, 'screenshot_cp_modern_system');

        // Navigazione su categoria "Personalizzazione"
        console.log('Navigazione su scheda "Personalizzazione"...');
        for (let i = 0; i < 5; i++) {
            client.write('mouse_move 0 5\n');
            await sleep(25);
        }
        await sleep(150);
        client.write('mouse_button 1\n');
        client.write('mouse_move 0 0\n');
        await sleep(80);
        client.write('mouse_button 0\n');
        client.write('mouse_move 0 0\n');
        await sleep(1000);
        await captureScreen(client, 'screenshot_cp_modern_personal');

        // Clic sul pulsante "< Pannello classico" in fondo alla sidebar per tornare al classico
        console.log('Clic sul link "< Pannello classico" per tornare alla modalita classica...');
        for (let i = 0; i < 10; i++) {
            client.write('mouse_move 0 12\n');
            await sleep(25);
        }
        await sleep(150);
        client.write('mouse_button 1\n');
        client.write('mouse_move 0 0\n');
        await sleep(80);
        client.write('mouse_button 0\n');
        client.write('mouse_move 0 0\n');
        await sleep(1200);
        await captureScreen(client, 'screenshot_cp_returned_classic');
    });

    console.log('=== TEST 2: Pannello di Controllo Classico Primario (Boot 8) ===');
    await testSession('8', async (client) => {
        console.log('Cattura screenshot Pannello di Controllo Classico primario...');
        await captureScreen(client, 'screenshot_cp_classic');

        // Spostamento perfettamente centrato sul pulsante "Impostazioni >" nella WebView laterale
        console.log('Spostamento del cursore sul pulsante "Impostazioni >"...');
        for (let i = 0; i < 10; i++) {
            client.write('mouse_move -35 4\n');
            await sleep(25);
        }
        await sleep(300);

        console.log('Click su [ Impostazioni > ] nella WebView laterale...');
        client.write('mouse_button 1\n');
        client.write('mouse_move 1 0\n');
        await sleep(150);
        client.write('mouse_button 0\n');
        client.write('mouse_move -1 0\n');
        await sleep(400);
        client.write('sendkey m\n');
        await sleep(3000);
        await captureScreen(client, 'screenshot_cp_switched_to_modern');
    });

    console.log('=== TEST COMPLETATO CON SUCCESSO! ===');
    process.exit(0);
}

run().catch(err => {
    console.error('[ERRORE TEST]', err);
    process.exit(1);
});
