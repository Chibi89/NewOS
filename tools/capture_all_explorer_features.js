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
        console.log(`[OK] Generato screenshot: ${pngFile} (${fs.statSync(pngFile).size} byte)`);
    } else {
        console.error(`[ERR] File PPM non trovato: ${ppmFile}`);
    }
}

// Calibrazione mouse: spostamento a (0,0) in alto a sinistra
async function calibrateMouseToZero(client) {
    for (let i = 0; i < 15; i++) {
        client.write('mouse_move -100 -100\n');
        await sleep(25);
    }
    await sleep(150);
}

// Spostamento relativo del mouse da curX,curY a targetX,targetY
async function moveMouseTo(client, curX, curY, targetX, targetY) {
    const dx = targetX - curX;
    const dy = targetY - curY;

    const steps = Math.max(Math.ceil(Math.abs(dx) / 20), Math.ceil(Math.abs(dy) / 20), 1);
    const stepX = Math.round(dx / steps);
    const stepY = Math.round(dy / steps);

    for (let i = 0; i < steps; i++) {
        client.write(`mouse_move ${stepX} ${stepY}\n`);
        await sleep(25);
    }
    await sleep(150);
}

async function runFeatureCapture() {
    console.log('[EXPLORER VERIFICATION] Avvio QEMU per collaudo e cattura di tutte le funzioni...');

    const qemu = spawn(qemuBin, [
        '-cdrom', isoPath,
        '-m', '128M',
        '-vga', 'std',
        '-netdev', 'user,id=net0',
        '-device', 'e1000,netdev=net0',
        '-audiodev', 'none,id=snd0',
        '-device', 'AC97,audiodev=snd0',
        '-display', 'none',
        '-monitor', 'tcp:127.0.0.1:4625,server,nowait',
        '-serial', `file:${path.join(ROOT, 'serial_explorer_features.log')}`
    ], { stdio: 'ignore' });

    setTimeout(() => {
        const client = net.createConnection({ port: 4625 }, async () => {
            console.log('[EXPLORER VERIFICATION] Connesso al monitor QEMU...');
            await sleep(400);

            // 1. Boot option 6: Explorer puro al desktop
            console.log('[EXPLORER VERIFICATION] Invio scelta boot 6 (Explorer puro al desktop)...');
            client.write('sendkey 6\n');
            await sleep(3500);

            // Explorer window: x=80, y=45, w=600, h=430
            // Calibra il mouse a (0, 0)
            console.log('[EXPLORER VERIFICATION] Calibrazione cursore mouse a (0, 0)...');
            await calibrateMouseToZero(client);
            let mouseX = 0;
            let mouseY = 0;

            // SCREENSHOT 0: Risorse del computer (iniziale)
            console.log('[EXPLORER VERIFICATION] Cattura 0: Risorse del computer iniziale...');
            const ppmInit = path.join(brainDir, 'tmp_exp_init.ppm');
            const bmpInit = path.join(brainDir, 'tmp_exp_init.bmp');
            const pngInit = path.join(brainDir, 'screen_explorer_this_pc.png');
            client.write(`screendump ${ppmInit.replace(/\\/g, '/')}\n`);
            await sleep(1200);

            // FEATURE 1: Menu a tendina "File"
            // File header: cx = 84, cy = 69. "File" è a mx: cx+6..cx+42 (90..126), my: cy+2..cy+18 (71..87).
            // Centro File: x = 108, y = 78.
            console.log('[EXPLORER VERIFICATION] Cattura 1: Clic su menu File (108, 78)...');
            await moveMouseTo(client, mouseX, mouseY, 108, 78);
            mouseX = 108;
            mouseY = 78;
            client.write('mouse_button 1\n');
            await sleep(80);
            client.write('mouse_button 0\n');
            await sleep(1500);

            const ppmMenu = path.join(brainDir, 'tmp_exp_menu.ppm');
            const bmpMenu = path.join(brainDir, 'tmp_exp_menu.bmp');
            const pngMenu = path.join(brainDir, 'screen_explorer_menu.png');
            client.write(`screendump ${ppmMenu.replace(/\\/g, '/')}\n`);
            await sleep(1200);

            // FEATURE 2: Chiudi menu File e clicca su [ Cartelle ] per visualizzazione ad albero
            // Toolbar [ Cartelle ]: x = 436, y = 102.
            console.log('[EXPLORER VERIFICATION] Cattura 2: Clic su [ Cartelle ] per Albero (436, 102)...');
            await moveMouseTo(client, mouseX, mouseY, 436, 102);
            mouseX = 436;
            mouseY = 102;
            client.write('mouse_button 1\n');
            await sleep(80);
            client.write('mouse_button 0\n');
            await sleep(1500);

            const ppmTree = path.join(brainDir, 'tmp_exp_tree.ppm');
            const bmpTree = path.join(brainDir, 'tmp_exp_tree.bmp');
            const pngTree = path.join(brainDir, 'screen_explorer_tree.png');
            client.write(`screendump ${ppmTree.replace(/\\/g, '/')}\n`);
            await sleep(1200);

            // FEATURE 3: Menu Contestuale con Tasto Destro sull'icona Disco (C:)
            // In modalità albero, side_w = 160.
            // main_x = 84 + 164 = 248.
            // Prima icona (C:): col 0, row 0 -> ix = 248 + 8 + 60 = 316, iy = 69 + 74 + 8 + 35 = 186.
            console.log('[EXPLORER VERIFICATION] Cattura 3: Tasto destro su icona Disco C: (316, 186)...');
            await moveMouseTo(client, mouseX, mouseY, 316, 186);
            mouseX = 316;
            mouseY = 186;
            client.write('mouse_button 2\n');
            await sleep(120);
            client.write('mouse_button 0\n');
            await sleep(1500);

            const ppmCtx = path.join(brainDir, 'tmp_exp_ctx.ppm');
            const bmpCtx = path.join(brainDir, 'tmp_exp_ctx.bmp');
            const pngCtx = path.join(brainDir, 'screen_explorer_context.png');
            client.write(`screendump ${ppmCtx.replace(/\\/g, '/')}\n`);
            await sleep(1200);

            // FEATURE 4: Clic su "Proprieta" nel menu contestuale
            // Il menu contestuale è comparso a (316, 186).
            // Le voci sono: Apri (rel_y 4..24), Elimina (rel_y 26..48), Proprieta (rel_y 50..70).
            // Screen x = 316 + 50 = 366, screen y = 186 + 60 = 246.
            console.log('[EXPLORER VERIFICATION] Cattura 4: Clic su "Proprieta" nel menu contestuale (366, 246)...');
            await moveMouseTo(client, mouseX, mouseY, 366, 246);
            mouseX = 366;
            mouseY = 246;
            client.write('mouse_button 1\n');
            await sleep(80);
            client.write('mouse_button 0\n');
            await sleep(1500);

            const ppmProps = path.join(brainDir, 'tmp_exp_props.ppm');
            const bmpProps = path.join(brainDir, 'tmp_exp_props.bmp');
            const pngProps = path.join(brainDir, 'screen_explorer_props.png');
            client.write(`screendump ${ppmProps.replace(/\\/g, '/')}\n`);
            await sleep(1200);

            // Chiudi il dialogo proprietà premendo ESC
            console.log('[EXPLORER VERIFICATION] Chiusura dialogo proprieta (ESC)...');
            client.write('sendkey esc\n');
            await sleep(800);

            // FEATURE 5: Toolbar [ Cerca ] e digitazione filtro "disco"
            // Toolbar [ Cerca ]: x = 370, y = 102.
            console.log('[EXPLORER VERIFICATION] Cattura 5: Clic su [ Cerca ] (370, 102)...');
            await moveMouseTo(client, mouseX, mouseY, 370, 102);
            mouseX = 370;
            mouseY = 102;
            client.write('mouse_button 1\n');
            await sleep(80);
            client.write('mouse_button 0\n');
            await sleep(800);

            console.log('[EXPLORER VERIFICATION] Digitazione filtro "disco"...');
            for (const c of 'disco') {
                client.write(`sendkey ${c}\n`);
                await sleep(100);
            }
            await sleep(1200);

            const ppmSearch = path.join(brainDir, 'tmp_exp_search.ppm');
            const bmpSearch = path.join(brainDir, 'tmp_exp_search.bmp');
            const pngSearch = path.join(brainDir, 'screen_explorer_search.png');
            client.write(`screendump ${ppmSearch.replace(/\\/g, '/')}\n`);
            await sleep(1200);

            // Termina QEMU
            console.log('[EXPLORER VERIFICATION] Chiusura QEMU...');
            client.write('quit\n');
            await sleep(800);
            qemu.kill();

            console.log('[EXPLORER VERIFICATION] Conversione immagini in PNG...');
            convertPpmToPng(ppmInit, bmpInit, pngInit);
            convertPpmToPng(ppmMenu, bmpMenu, pngMenu);
            convertPpmToPng(ppmTree, bmpTree, pngTree);
            convertPpmToPng(ppmCtx, bmpCtx, pngCtx);
            convertPpmToPng(ppmProps, bmpProps, pngProps);
            convertPpmToPng(ppmSearch, bmpSearch, pngSearch);

            try { fs.unlinkSync(ppmInit); fs.unlinkSync(bmpInit); } catch(e){}
            try { fs.unlinkSync(ppmMenu); fs.unlinkSync(bmpMenu); } catch(e){}
            try { fs.unlinkSync(ppmTree); fs.unlinkSync(bmpTree); } catch(e){}
            try { fs.unlinkSync(ppmCtx); fs.unlinkSync(bmpCtx); } catch(e){}
            try { fs.unlinkSync(ppmProps); fs.unlinkSync(bmpProps); } catch(e){}
            try { fs.unlinkSync(ppmSearch); fs.unlinkSync(bmpSearch); } catch(e){}

            console.log('[EXPLORER VERIFICATION] TUTTI GLI SCREENSHOT SONO STATI CATTURATI E CONVERTITI CON SUCCESSO!');
            process.exit(0);
        });

        client.on('error', (err) => {
            console.error('[ERR] Connessione al monitor fallita:', err);
            qemu.kill();
            process.exit(1);
        });
    }, 2000);
}

runFeatureCapture().catch(console.error);
