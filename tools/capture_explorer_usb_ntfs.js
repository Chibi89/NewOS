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
        await sleep(80);
    }
    client.write('sendkey ret\n');
    await sleep(700);
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

async function runExplorerUsbCapture() {
    console.log('[EXPLORER USB CAPTURE] Avvio QEMU con NewOS_V1.iso...');

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
        '-monitor', 'tcp:127.0.0.1:4555,server,nowait'
    ], { stdio: 'ignore' });

    setTimeout(() => {
        const client = net.createConnection({ port: 4555 }, async () => {
            console.log('[EXPLORER USB CAPTURE] Connesso al monitor QEMU...');
            await sleep(2000);

            // 1. Boot option 4: Prompt CLI
            console.log('[EXPLORER USB CAPTURE] Invio scelta boot 4 (Prompt CLI)...');
            client.write('sendkey 4\n');
            await sleep(3500);

            // Collaudo 1: ntfs write per attivare NTFS su U: e scrivere i file
            console.log('[EXPLORER USB CAPTURE] Esecuzione ntfs write...');
            await typeString(client, 'ntfs write');
            await sleep(2000);

            // Collaudo 2: Apri Risorse del Computer e cattura
            console.log('[EXPLORER USB CAPTURE] Apertura Risorse del computer (explorer)...');
            await typeString(client, 'explorer');
            await sleep(3500);

            const ppm1 = path.join(brainDir, 'screen_exp_pc.ppm');
            const bmp1 = path.join(brainDir, 'screen_exp_pc.bmp');
            const png1 = path.join(brainDir, 'screen_explorer_this_pc.png');
            client.write(`screendump ${ppm1.replace(/\\/g, '/')}\n`);
            await sleep(1500);

            // Collaudo 3: Naviga dentro l'unità U: (NTFS) premendo 'u' e Invio
            console.log('[EXPLORER USB CAPTURE] Selezione ed apertura Unita Rimovibile (U:) con tasto U e Invio...');
            client.write('sendkey u\n');
            await sleep(1000);
            client.write('sendkey ret\n');
            await sleep(3500);

            const ppm2 = path.join(brainDir, 'screen_exp_u.ppm');
            const bmp2 = path.join(brainDir, 'screen_exp_u.bmp');
            const png2 = path.join(brainDir, 'screen_explorer_drive_u.png');
            client.write(`screendump ${ppm2.replace(/\\/g, '/')}\n`);
            await sleep(1500);

            // Termina QEMU
            client.write('quit\n');
            await sleep(1000);
            qemu.kill();

            console.log('[EXPLORER USB CAPTURE] Conversione immagini...');
            convertPpmToPng(ppm1, bmp1, png1);
            convertPpmToPng(ppm2, bmp2, png2);

            try { fs.unlinkSync(ppm1); fs.unlinkSync(bmp1); } catch(e){}
            try { fs.unlinkSync(ppm2); fs.unlinkSync(bmp2); } catch(e){}

            console.log('[EXPLORER USB CAPTURE] Tutte le schermate sono state generate con successo!');
            process.exit(0);
        });

        client.on('error', (err) => {
            console.error('[ERR] Connessione al monitor fallita:', err);
            qemu.kill();
            process.exit(1);
        });
    }, 1500);
}

runExplorerUsbCapture().catch(console.error);
