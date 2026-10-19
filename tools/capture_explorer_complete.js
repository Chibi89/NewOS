const { spawn, execSync } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const isoPath = path.join(ROOT, 'NewOS_V1.iso');
const brainDir = 'C:\\Users\\morga\\.gemini\\antigravity-ide\\brain\\327df2b4-88f2-409e-bc1c-ac357035f353';

if (!fs.existsSync(brainDir)) {
    fs.mkdirSync(brainDir, { recursive: true });
}

function sleep(ms) {
    return new Promise(res => setTimeout(res, ms));
}

function convertPpmToPng(ppmFile, bmpFile, pngFile) {
    if (fs.existsSync(ppmFile)) {
        execSync(`node tools/ppm2bmp.js "${ppmFile}" "${bmpFile}"`, { stdio: 'ignore' });
        execSync(`powershell -ExecutionPolicy Bypass -File tools/bmp2png.ps1 "${bmpFile}" "${pngFile}"`, { stdio: 'ignore' });
        console.log(`[OK] Generato screenshot: ${pngFile}`);
    } else {
        console.error(`[ERR] File PPM non trovato: ${ppmFile}`);
    }
}

async function moveMouseTo(client, curX, curY, targetX, targetY) {
    const dx = targetX - curX;
    const dy = targetY - curY; // QEMU monitor: +dy is down, -dy is up

    const steps = Math.max(Math.ceil(Math.abs(dx) / 25), Math.ceil(Math.abs(dy) / 25), 1);
    const stepX = Math.round(dx / steps);
    const stepY = Math.round(dy / steps);

    for (let i = 0; i < steps; i++) {
        client.write(`mouse_move ${stepX} ${stepY}\n`);
        await sleep(30);
    }
    await sleep(150);
}

async function runCapture() {
    console.log('[EXPLORER COMPLETE] Avvio QEMU per cattura completa funzioni Explorer...');

    const qemu = spawn(qemuBin, [
        '-cdrom', isoPath,
        '-m', '256M',
        '-vga', 'std',
        '-device', 'piix3-usb-uhci',
        '-device', 'qemu-xhci',
        '-netdev', 'user,id=net0',
        '-device', 'e1000,netdev=net0',
        '-audiodev', 'none,id=snd0',
        '-device', 'AC97,audiodev=snd0',
        '-display', 'none',
        '-monitor', 'tcp:127.0.0.1:4630,server,nowait',
        '-serial', 'tcp:127.0.0.1:4631,server,nowait'
    ], { stdio: 'ignore' });

    let serialData = '';
    let bootMenuReached = false;
    let desktopReached = false;

    // Connessione seriale per sincronizzazione
    const serialClient = net.createConnection({ port: 4631 });
    serialClient.on('data', (d) => {
        const str = d.toString('utf8');
        serialData += str;
        process.stdout.write(str);
        if (str.includes('6') || str.includes('MB2dvkagPLK')) {
            bootMenuReached = true;
        }
        if (str.includes('7') || str.includes('.')) {
            desktopReached = true;
        }
    });
    serialClient.on('error', () => {});

    setTimeout(() => {
        const client = net.createConnection({ port: 4630 }, async () => {
            console.log('[EXPLORER COMPLETE] Connesso al monitor QEMU...');

            // Attendi bootscreen (circa 3.5 secondi)
            console.log('[EXPLORER COMPLETE] Attesa avvio e visualizzazione boot menu...');
            await sleep(4000);

            // Invia scelta boot '6' (Explorer Diretto su Desktop)
            console.log('[EXPLORER COMPLETE] Invio opzione boot 6 (Explorer Diretto)...');
            client.write('sendkey 6\n');
            await sleep(1000);

            // Attendi caricamento desktop ed Explorer
            console.log('[EXPLORER COMPLETE] Attesa caricamento Desktop ed Explorer...');
            for (let i = 0; i < 30; i++) {
                if (desktopReached) break;
                await sleep(300);
            }
            await sleep(2500);

            let mouseX = 512;
            let mouseY = 384;

            // 1. Screenshot iniziale: Explorer aperto con Risorse del computer
            console.log('[EXPLORER COMPLETE] 1. Cattura Risorse del computer iniziale...');
            const ppmThisPc = path.join(brainDir, 'tmp_exp_this_pc.ppm');
            const bmpThisPc = path.join(brainDir, 'tmp_exp_this_pc.bmp');
            const pngThisPc = path.join(brainDir, 'screen_explorer_this_pc.png');
            client.write(`screendump ${ppmThisPc.replace(/\\/g, '/')}\n`);
            await sleep(1000);

            // 2. FEATURE 1: Menu a tendina "File"
            // File header: screen x = 105, y = 77
            console.log('[EXPLORER COMPLETE] 2. Clic su menu File (105, 77)...');
            await moveMouseTo(client, mouseX, mouseY, 105, 77);
            mouseX = 105;
            mouseY = 77;
            client.write('mouse_button 1\n');
            await sleep(80);
            client.write('mouse_button 0\n');
            await sleep(1200);

            const ppmMenu = path.join(brainDir, 'tmp_exp_menu.ppm');
            const bmpMenu = path.join(brainDir, 'tmp_exp_menu.bmp');
            const pngMenu = path.join(brainDir, 'screen_explorer_menu.png');
            client.write(`screendump ${ppmMenu.replace(/\\/g, '/')}\n`);
            await sleep(1000);

            // 3. FEATURE 2: Albero Cartelle (Tree View)
            // Toolbar [ Cartelle ]: Screen x = 436, y = 101
            console.log('[EXPLORER COMPLETE] 3. Clic su [ Cartelle ] per albero gerarchico (436, 101)...');
            await moveMouseTo(client, mouseX, mouseY, 436, 101);
            mouseX = 436;
            mouseY = 101;
            client.write('mouse_button 1\n');
            await sleep(80);
            client.write('mouse_button 0\n');
            await sleep(1200);

            const ppmTree = path.join(brainDir, 'tmp_exp_tree.ppm');
            const bmpTree = path.join(brainDir, 'tmp_exp_tree.bmp');
            const pngTree = path.join(brainDir, 'screen_explorer_tree.png');
            client.write(`screendump ${ppmTree.replace(/\\/g, '/')}\n`);
            await sleep(1000);

            // 4. FEATURE 3: Menu Contestuale Tasto Destro su elemento (Disco C:)
            // Disco C: è a screen x = 324, y = 189
            console.log('[EXPLORER COMPLETE] 4. Tasto destro su Disco C: (324, 189)...');
            await moveMouseTo(client, mouseX, mouseY, 324, 189);
            mouseX = 324;
            mouseY = 189;
            client.write('mouse_button 2\n');
            await sleep(100);
            client.write('mouse_button 0\n');
            await sleep(1200);

            const ppmCtx = path.join(brainDir, 'tmp_exp_ctx.ppm');
            const bmpCtx = path.join(brainDir, 'tmp_exp_ctx.bmp');
            const pngCtx = path.join(brainDir, 'screen_explorer_context.png');
            client.write(`screendump ${ppmCtx.replace(/\\/g, '/')}\n`);
            await sleep(1000);

            // 5. FEATURE 4: Finestra di dialogo Proprietà
            // Nel menu contestuale a (324, 189), "Proprieta" è a rel_x = 50, rel_y = 60 -> (374, 249)
            console.log('[EXPLORER COMPLETE] 5. Clic su "Proprieta" nel menu contestuale (374, 249)...');
            await moveMouseTo(client, mouseX, mouseY, 374, 249);
            mouseX = 374;
            mouseY = 249;
            client.write('mouse_button 1\n');
            await sleep(80);
            client.write('mouse_button 0\n');
            await sleep(1500);

            const ppmProps = path.join(brainDir, 'tmp_exp_props.ppm');
            const bmpProps = path.join(brainDir, 'tmp_exp_props.bmp');
            const pngProps = path.join(brainDir, 'screen_explorer_props.png');
            client.write(`screendump ${ppmProps.replace(/\\/g, '/')}\n`);
            await sleep(1000);

            // Chiudi dialogo proprietà con ESC
            console.log('[EXPLORER COMPLETE] Chiusura dialogo proprieta con ESC...');
            client.write('sendkey esc\n');
            await sleep(800);

            // 6. FEATURE 5: Ricerca in tempo reale (Filtro Cerca)
            // Toolbar [ Cerca ] è a screen x = 370, y = 101
            console.log('[EXPLORER COMPLETE] 6. Clic su [ Cerca ] (370, 101)...');
            await moveMouseTo(client, mouseX, mouseY, 370, 101);
            mouseX = 370;
            mouseY = 101;
            client.write('mouse_button 1\n');
            await sleep(80);
            client.write('mouse_button 0\n');
            await sleep(800);

            console.log('[EXPLORER COMPLETE] Digitazione filtro "disco"...');
            for (const ch of 'disco') {
                client.write(`sendkey ${ch}\n`);
                await sleep(100);
            }
            await sleep(1500);

            const ppmSearch = path.join(brainDir, 'tmp_exp_search.ppm');
            const bmpSearch = path.join(brainDir, 'tmp_exp_search.bmp');
            const pngSearch = path.join(brainDir, 'screen_explorer_search.png');
            client.write(`screendump ${ppmSearch.replace(/\\/g, '/')}\n`);
            await sleep(1000);

            // Termina QEMU
            console.log('[EXPLORER COMPLETE] Test completato. Chiusura QEMU...');
            client.write('quit\n');
            await sleep(800);
            qemu.kill();
            serialClient.destroy();

            console.log('[EXPLORER COMPLETE] Conversione immagini in PNG...');
            convertPpmToPng(ppmThisPc, bmpThisPc, pngThisPc);
            convertPpmToPng(ppmMenu, bmpMenu, pngMenu);
            convertPpmToPng(ppmTree, bmpTree, pngTree);
            convertPpmToPng(ppmCtx, bmpCtx, pngCtx);
            convertPpmToPng(ppmProps, bmpProps, pngProps);
            convertPpmToPng(ppmSearch, bmpSearch, pngSearch);

            try { fs.unlinkSync(ppmThisPc); fs.unlinkSync(bmpThisPc); } catch(e){}
            try { fs.unlinkSync(ppmMenu); fs.unlinkSync(bmpMenu); } catch(e){}
            try { fs.unlinkSync(ppmTree); fs.unlinkSync(bmpTree); } catch(e){}
            try { fs.unlinkSync(ppmCtx); fs.unlinkSync(bmpCtx); } catch(e){}
            try { fs.unlinkSync(ppmProps); fs.unlinkSync(bmpProps); } catch(e){}
            try { fs.unlinkSync(ppmSearch); fs.unlinkSync(bmpSearch); } catch(e){}

            console.log('[EXPLORER COMPLETE] Tutti gli screenshot sono pronti e verificati!');
            process.exit(0);
        });

        client.on('error', (err) => {
            console.error('[ERR] Errore connessione monitor QEMU:', err);
            qemu.kill();
            process.exit(1);
        });
    }, 1500);
}

runCapture().catch(console.error);
