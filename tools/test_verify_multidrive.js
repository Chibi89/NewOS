const { spawn, execSync } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
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
    console.log('[TEST] Generazione secondo hard disk virtuale di collaudo (64 MB)...');
    const secDiskPath = path.join(ROOT, 'build', 'secondary_disk.img');
    if (!fs.existsSync(secDiskPath)) {
        const dummySec = Buffer.alloc(64 * 1024 * 1024, 0);
        fs.writeFileSync(secDiskPath, dummySec);
    }

    console.log('[TEST] Avvio QEMU con 2 Hard Disk (C: Primario di Sistema + E: Secondario non allocato)...');
    const imgPath = path.join(ROOT, 'build', 'NewOS_V1.img');
    const qemu = spawn(qemuBin, [
        '-drive', `format=raw,file=${imgPath},index=0,media=disk`,
        '-drive', `format=raw,file=${secDiskPath},index=1,media=disk`,
        '-m', '256M',
        '-vga', 'std',
        '-netdev', 'user,id=net0',
        '-device', 'e1000,netdev=net0',
        '-audiodev', 'none,id=snd0',
        '-device', 'AC97,audiodev=snd0',
        '-display', 'none',
        '-serial', `file:${path.join(ROOT, 'serial_multidrive.log')}`,
        '-monitor', 'tcp:127.0.0.1:4649,server,nowait'
    ], { stdio: 'ignore' });

    await sleep(2500);

    const client = net.createConnection({ port: 4649 }, async () => {
        console.log('[TEST] Connesso al monitor QEMU. Superamento boot menu...');
        await sleep(1000);
        client.write('sendkey ret\n'); // Seleziona opzione Live o Boot normale
        console.log('[TEST] Attesa caricamento desktop e montaggio unità VFS...');
        await sleep(4500);

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

        console.log('[TEST] Cattura Risorse del computer con entrambi i dischi montati (C: ed E:)...');
        await capture('screen_multidrive_explorer');

        // In Risorse del computer gli elementi sono ordinati: 0=C:, 1=A:, 2=D:, 3=U:, 4=E:
        // Seleziona il disco E: premendo Tab 4 volte
        console.log('[TEST] Selezione Disco locale (E:) tramite Tab x 4...');
        client.write('sendkey tab\n');
        await sleep(300);
        client.write('sendkey tab\n');
        await sleep(300);
        client.write('sendkey tab\n');
        await sleep(300);
        client.write('sendkey tab\n');
        await sleep(500);

        // Cattura per vedere la selezione visiva su E: e le info Web View laterali!
        await capture('screen_multidrive_e_selected');

        console.log('[TEST] Apertura Disco locale (E:) via Invio...');
        client.write('sendkey ret\n');
        await sleep(2500);

        console.log('[TEST] Cattura cartella radice E:\\...');
        await capture('screen_multidrive_e_root');

        // Torna su con backspace (in QEMU monitor il nome del tasto è 'backspace')
        console.log('[TEST] Ritorno al livello superiore (Backspace)...');
        client.write('sendkey backspace\n');
        await sleep(2000);
        await capture('screen_multidrive_back_to_pc');

        console.log('[TEST] Collaudo completato con successo!');
        client.write('quit\n');
        setTimeout(() => {
            try { client.destroy(); } catch (e) {}
            try { qemu.kill(); } catch (e) {}
            process.exit(0);
        }, 500);
    });

    client.on('error', (err) => {
        console.error('[ERRORE] Connessione a QEMU fallita:', err.message);
        try { qemu.kill(); } catch (e) {}
        process.exit(1);
    });
}

run();
