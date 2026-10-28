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

async function runResizeTest() {
    const bootKey = '1';
    const serialLog = path.join(ROOT, 'tools', 'serial_resize.log');
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
        '-monitor', `tcp:127.0.0.1:4498,server,nowait`
    ], { stdio: 'ignore' });

    await sleep(1500);

    return new Promise((resolve, reject) => {
        const client = net.createConnection({ port: 4498 }, async () => {
            try {
                console.log(`[TEST] Attesa avvio Boot Menu...`);
                for (let i = 0; i < 40; i++) {
                    await sleep(150);
                    if (fs.existsSync(serialLog)) {
                        const content = fs.readFileSync(serialLog, 'utf8');
                        if (content.includes('[BOOT_MENU] Pronto!')) break;
                    }
                }

                console.log(`[TEST] Boot Menu pronto! Invio tasto '${bootKey}' per avviare il Desktop...`);
                for (let k = 0; k < 4; k++) {
                    client.write(`sendkey ${bootKey}\n`);
                    await sleep(100);
                }

                // Attesa ingresso sul desktop e apertura Explorer
                await sleep(8000);

                // 1. Screenshot iniziale con la finestra (600x430) e i nuovi pulsanti + resize grip
                console.log('1. Cattura screenshot iniziale con finestra Explorer (600x430) e Resize Grip...');
                await captureScreen(client, 'screenshot_window_initial');

                // Test A: Massimizzazione a pieno schermo
                // Mouse parte da (512, 384)
                // Centro pulsante Massimizza [□]: x=649, y=56
                // dx = 649 - 512 = +137 (7 passi da +19, 1 da +4)
                // dy = 56 - 384 = -328 (8 passi da -41)
                console.log('2. Spostamento mouse sul pulsante Massimizza [□] a (649, 56)...');
                for (let step = 0; step < 7; step++) {
                    client.write('mouse_move 19 -41\n');
                    await sleep(60);
                }
                client.write('mouse_move 4 -41\n');
                await sleep(400);

                console.log('3. Clic sul pulsante Massimizza [□]...');
                client.write('mouse_button 1\n');
                for (let c = 0; c < 3; c++) {
                    client.write('mouse_move 1 0\n');
                    await sleep(80);
                    client.write('mouse_move -1 0\n');
                    await sleep(80);
                }
                client.write('mouse_button 0\n');
                client.write('mouse_move 0 0\n');
                await sleep(1200);

                console.log('4. Cattura screenshot finestra Massimizzata a pieno schermo (1024x740)...');
                await captureScreen(client, 'screenshot_window_maximized');

                // Test B: Ripristino dimensioni originali [❐]
                // Quando massimizzata: w=1024, h=740, x=0, y=0
                // close_x = 0 + 1024 - 21 = 1003
                // max_x = 1003 - 18 = 985 (centro a 993, y = 0 + 4 + 7 = 11)
                // Cursore attuale a (649, 56)
                // dx = 993 - 649 = +344 (8 passi da +43)
                // dy = 11 - 56 = -45 (5 passi da -9)
                console.log('5. Spostamento cursore sul pulsante Ripristina [❐] a (993, 11)...');
                for (let step = 0; step < 8; step++) {
                    client.write('mouse_move 43 -5\n');
                    await sleep(50);
                }
                client.write('mouse_move 0 -5\n');
                await sleep(400);

                console.log('6. Clic sul pulsante Ripristina [❐]...');
                client.write('mouse_button 1\n');
                for (let c = 0; c < 3; c++) {
                    client.write('mouse_move 1 0\n');
                    await sleep(80);
                    client.write('mouse_move -1 0\n');
                    await sleep(80);
                }
                client.write('mouse_button 0\n');
                client.write('mouse_move 0 0\n');
                await sleep(1200);

                // Test C: Spostamento sul Resize Grip
                // Finestra ripristinata a (80, 45, 600, 430)
                // Angolo Resize Grip a (672, 464)
                // Cursore attuale a (993, 11)
                // dx = 672 - 993 = -321 (8 passi da -40)
                // dy = 464 - 11 = +453 (9 passi da +50)
                console.log('7. Spostamento mouse sul Resize Grip a (672, 464)...');
                for (let step = 0; step < 8; step++) {
                    client.write('mouse_move -40 50\n');
                    await sleep(50);
                }
                client.write('mouse_move -1 53\n');
                await sleep(500);

                console.log('8. Cattura screenshot in hover sul Resize Grip (mostra cursore ⤡)...');
                await captureScreen(client, 'screenshot_window_hover_resize');

                // Test D: Drag resize (+120px orizzontale, +90px verticale)
                console.log('9. Pressione tasto sinistro sul Resize Grip e trascinamento (+120, +90)...');
                client.write('mouse_button 1\n');
                client.write('mouse_move 1 1\n');
                await sleep(200);

                for (let step = 0; step < 10; step++) {
                    client.write('mouse_move 12 9\n');
                    await sleep(80);
                }
                await sleep(200);

                console.log('10. Rilascio tasto sinistro (ridimensionamento completato)...');
                client.write('mouse_button 0\n');
                client.write('mouse_move 0 0\n');
                await sleep(1000);

                console.log('11. Cattura screenshot dopo ridimensionamento della finestra...');
                await captureScreen(client, 'screenshot_window_resized');

                client.write('quit\n');
                client.end();
                setTimeout(() => {
                    qemu.kill();
                    resolve();
                }, 500);
            } catch (err) {
                console.error('[ERRORE TEST]:', err);
                client.write('quit\n');
                qemu.kill();
                reject(err);
            }
        });

        client.on('error', (err) => {
            console.error('[ERRORE CONNESSIONE]:', err.message);
            qemu.kill();
            reject(err);
        });
    });
}

runResizeTest().then(() => {
    console.log('[FINE TEST] Tutti i collaudi di ridimensionamento completati con successo.');
    process.exit(0);
}).catch(err => {
    console.error('[FALLIMENTO TEST]:', err);
    process.exit(1);
});
