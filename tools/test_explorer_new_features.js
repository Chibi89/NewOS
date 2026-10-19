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

async function typeString(client, str) {
    for (const ch of str) {
        if (ch === ' ') {
            client.write('sendkey spc\n');
        } else if (ch === ':') {
            client.write('sendkey shift-semicolon\n');
        } else if (ch === '.') {
            client.write('sendkey dot\n');
        } else if (ch === '-') {
            client.write('sendkey minus\n');
        } else if (ch === '_') {
            client.write('sendkey shift-minus\n');
        } else if (ch === '\\') {
            client.write('sendkey backslash\n');
        } else {
            client.write(`sendkey ${ch.toLowerCase()}\n`);
        }
        await sleep(70);
    }
    client.write('sendkey ret\n');
    await sleep(700);
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

async function runFeatureCapture() {
    console.log('[EXPLORER FEATURES] Avvio QEMU per collaudo completo funzioni Explorer...');

    const qemu = spawn(qemuBin, [
        '-cdrom', isoPath,
        '-m', '128M',
        '-vga', 'std',
        '-device', 'piix3-usb-uhci',
        '-device', 'qemu-xhci',
        '-netdev', 'user,id=net0',
        '-device', 'e1000,netdev=net0',
        '-audiodev', 'none,id=snd0',
        '-device', 'AC97,audiodev=snd0',
        '-display', 'none',
        '-monitor', 'tcp:127.0.0.1:4595,server,nowait'
    ], { stdio: 'ignore' });

    setTimeout(() => {
        const client = net.createConnection({ port: 4595 }, async () => {
            console.log('[EXPLORER FEATURES] Connesso al monitor QEMU...');
            await sleep(3000);

            // Invia scelta boot 4 più volte per essere certi che entri in modalità Prompt CLI
            console.log('[EXPLORER FEATURES] Invio scelta boot 4 (Prompt CLI)...');
            for (let k = 0; k < 6; k++) {
                client.write('sendkey 4\n');
                await sleep(500);
            }
            await sleep(3500);

            // Apri Explorer con comando "explorer"
            console.log('[EXPLORER FEATURES] Apertura Explorer con comando "explorer"...');
            await typeString(client, 'explorer');
            await sleep(3500);

            // Explorer window: x=80, y=45, w=600, h=430
            // Mouse starts at (512, 384)
            let mouseX = 512;
            let mouseY = 384;

            // FEATURE 1: Menu a tendina "File"
            // File header: screen x = 80 + 25 = 105, y = 45 + 32 = 77.
            console.log('[EXPLORER FEATURES] Clic su menu File (105, 77)...');
            await moveMouseTo(client, mouseX, mouseY, 105, 77);
            mouseX = 105;
            mouseY = 77;
            client.write('mouse_button 1\n');
            await sleep(60);
            client.write('mouse_button 0\n');
            await sleep(1500);

            const ppmMenu = path.join(brainDir, 'screen_exp_menu.ppm');
            const bmpMenu = path.join(brainDir, 'screen_exp_menu.bmp');
            const pngMenu = path.join(brainDir, 'screen_explorer_menu.png');
            client.write(`screendump ${ppmMenu.replace(/\\/g, '/')}\n`);
            await sleep(1200);

            // FEATURE 2: Chiudi menu File e clicca su [ Cartelle ]
            // Toolbar [ Cartelle ]: Screen: x = 80 + 355 = 435, y = 45 + 56 = 101.
            console.log('[EXPLORER FEATURES] Clic su [ Cartelle ] per attivare visualizzazione ad albero (435, 101)...');
            await moveMouseTo(client, mouseX, mouseY, 435, 101);
            mouseX = 435;
            mouseY = 101;
            client.write('mouse_button 1\n');
            await sleep(60);
            client.write('mouse_button 0\n');
            await sleep(1500);

            const ppmTree = path.join(brainDir, 'screen_exp_tree.ppm');
            const bmpTree = path.join(brainDir, 'screen_exp_tree.bmp');
            const pngTree = path.join(brainDir, 'screen_explorer_tree.png');
            client.write(`screendump ${ppmTree.replace(/\\/g, '/')}\n`);
            await sleep(1200);

            // FEATURE 3: Menu Contestuale con Tasto Destro nel riquadro file
            // Main content area with tree view: side_w=160, main_x = cx+side_w+4 = 168.
            // Screen x = 80 + 350 = 430, y = 45 + 180 = 225.
            console.log('[EXPLORER FEATURES] Tasto destro nel riquadro file (430, 225)...');
            await moveMouseTo(client, mouseX, mouseY, 430, 225);
            mouseX = 430;
            mouseY = 225;
            client.write('mouse_button 2\n');
            await sleep(80);
            client.write('mouse_button 0\n');
            await sleep(1500);

            const ppmCtx = path.join(brainDir, 'screen_exp_ctx.ppm');
            const bmpCtx = path.join(brainDir, 'screen_exp_ctx.bmp');
            const pngCtx = path.join(brainDir, 'screen_explorer_context.png');
            client.write(`screendump ${ppmCtx.replace(/\\/g, '/')}\n`);
            await sleep(1200);

            // FEATURE 4: Clic su "Proprieta" nel menu contestuale per aprire il dialogo Proprietà
            // Context menu starts at (430, 225). "Proprieta" is at rel_y 50..70.
            // Screen x = 430 + 50 = 480, y = 225 + 60 = 285.
            console.log('[EXPLORER FEATURES] Clic su Proprieta nel menu contestuale (480, 285)...');
            await moveMouseTo(client, mouseX, mouseY, 480, 285);
            mouseX = 480;
            mouseY = 285;
            client.write('mouse_button 1\n');
            await sleep(60);
            client.write('mouse_button 0\n');
            await sleep(1500);

            const ppmProps = path.join(brainDir, 'screen_exp_props.ppm');
            const bmpProps = path.join(brainDir, 'screen_exp_props.bmp');
            const pngProps = path.join(brainDir, 'screen_explorer_props.png');
            client.write(`screendump ${ppmProps.replace(/\\/g, '/')}\n`);
            await sleep(1200);

            // Chiudi dialogo proprietà (ESC)
            console.log('[EXPLORER FEATURES] Chiusura dialogo proprieta con ESC...');
            client.write('sendkey esc\n');
            await sleep(800);

            // FEATURE 5: Toolbar [ Cerca ]
            // Toolbar [ Cerca ]: mx = 262..318 rel. Screen: x = 80 + 290 = 370, y = 45 + 56 = 101.
            console.log('[EXPLORER FEATURES] Clic su [ Cerca ] e digitazione filtro (370, 101)...');
            await moveMouseTo(client, mouseX, mouseY, 370, 101);
            mouseX = 370;
            mouseY = 101;
            client.write('mouse_button 1\n');
            await sleep(60);
            client.write('mouse_button 0\n');
            await sleep(800);

            // Digita filtro "disco"
            client.write('sendkey d\n');
            await sleep(100);
            client.write('sendkey i\n');
            await sleep(100);
            client.write('sendkey s\n');
            await sleep(100);
            client.write('sendkey c\n');
            await sleep(100);
            client.write('sendkey o\n');
            await sleep(1200);

            const ppmSearch = path.join(brainDir, 'screen_exp_search.ppm');
            const bmpSearch = path.join(brainDir, 'screen_exp_search.bmp');
            const pngSearch = path.join(brainDir, 'screen_explorer_search.png');
            client.write(`screendump ${ppmSearch.replace(/\\/g, '/')}\n`);
            await sleep(1200);

            // Termina QEMU
            console.log('[EXPLORER FEATURES] Collaudo completato con successo. Chiusura QEMU...');
            client.write('quit\n');
            await sleep(800);
            qemu.kill();

            console.log('[EXPLORER FEATURES] Conversione immagini in PNG...');
            convertPpmToPng(ppmMenu, bmpMenu, pngMenu);
            convertPpmToPng(ppmTree, bmpTree, pngTree);
            convertPpmToPng(ppmCtx, bmpCtx, pngCtx);
            convertPpmToPng(ppmProps, bmpProps, pngProps);
            convertPpmToPng(ppmSearch, bmpSearch, pngSearch);

            try { fs.unlinkSync(ppmMenu); fs.unlinkSync(bmpMenu); } catch(e){}
            try { fs.unlinkSync(ppmTree); fs.unlinkSync(bmpTree); } catch(e){}
            try { fs.unlinkSync(ppmCtx); fs.unlinkSync(bmpCtx); } catch(e){}
            try { fs.unlinkSync(ppmProps); fs.unlinkSync(bmpProps); } catch(e){}
            try { fs.unlinkSync(ppmSearch); fs.unlinkSync(bmpSearch); } catch(e){}

            console.log('[EXPLORER FEATURES] Tutte le immagini sono state salvate nel brain!');
            process.exit(0);
        });

        client.on('error', (err) => {
            console.error('[ERR] Connessione al monitor fallita:', err);
            qemu.kill();
            process.exit(1);
        });
    }, 1500);
}

runFeatureCapture().catch(console.error);
