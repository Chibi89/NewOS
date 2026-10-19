const { spawn, execSync } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const isoPath = path.join(ROOT, 'NewOS_V1.iso');
const brainDir = 'C:\\Users\\morga\\.gemini\\antigravity-ide\\brain\\201a47cf-7636-4592-93f8-cb22ec54e1bd';

const ppmReg1 = path.join(ROOT, 'test_reg1.ppm');
const bmpReg1 = path.join(ROOT, 'test_reg1.bmp');
const pngReg1 = path.join(brainDir, 'screenshot_regedit_main.png');

const ppmReg2 = path.join(ROOT, 'test_reg2.ppm');
const bmpReg2 = path.join(ROOT, 'test_reg2.bmp');
const pngReg2 = path.join(brainDir, 'screenshot_regedit_alert.png');

const ppmReg3 = path.join(ROOT, 'test_reg3.ppm');
const bmpReg3 = path.join(ROOT, 'test_reg3.bmp');
const pngReg3 = path.join(brainDir, 'screenshot_regedit_audit.png');

console.log('[TEST REGEDIT] Avvio QEMU per testare REGEDIT.EXE e Avviso di Sicurezza...');

const qemu = spawn(qemuBin, [
    '-cdrom', isoPath,
    '-m', '128M',
    '-vga', 'std',
    '-netdev', 'user,id=net0',
    '-device', 'e1000,netdev=net0',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4512,server,nowait',
    '-serial', `file:${path.join(ROOT, 'serial_regedit_test.log')}`
], { stdio: 'ignore' });

function sleep(ms) {
    return new Promise(res => setTimeout(res, ms));
}

setTimeout(() => {
    const client = net.createConnection({ port: 4512 }, async () => {
        console.log('Connesso al monitor QEMU...');
        await sleep(1500);

        // 1. Boot option 4: Prompt dei Comandi
        console.log('Selezione boot option 4 (Prompt dei Comandi)...');
        client.write('sendkey 4\n');
        await sleep(2500);

        // 2. Digita comando "regedit"
        console.log('Digitazione "regedit"...');
        const cmd = 'regedit';
        for (const c of cmd) {
            client.write(`sendkey ${c}\n`);
            await sleep(80);
        }
        client.write('sendkey ret\n');
        await sleep(2000);

        // 3. Screenshot 1: Finestra principale REGEDIT.EXE
        console.log('Cattura Screenshot 1: REGEDIT.EXE...');
        client.write(`screendump "${ppmReg1.replace(/\\/g, '/')}"\n`);
        await sleep(1200);

        // 4. Apri modifica valore e triggera Alert di Sicurezza:
        // Clic su "Modifica..."
        // In una finestra 1024x768, la finestra regedit è 620x420 centrata:
        // dx = (1024 - 620) / 2 = 202, dy = (740 - 420) / 2 = 160
        // cx = 206, cy = 184, cw = 612
        // btn_edit_x = cx + cw - 180 = 638, addr_y = cy + 4 + 24 + 4 = 216
        // Clic su Modifica: coordinate relative o premiamo Invio (tasto rapido open_edit_for_selected)
        console.log('Apertura dialogo Modifica (sendkey ret)...');
        client.write('sendkey ret\n');
        await sleep(800);

        // Premiamo Invio nel dialogo modifica per validare e aprire l'Avviso di Sicurezza
        console.log('Invio nel dialogo modifica (trigger Avviso di Sicurezza)...');
        client.write('sendkey ret\n');
        await sleep(1000);

        // 5. Screenshot 2: Avviso di Sicurezza del Registro
        console.log('Cattura Screenshot 2: Avviso di Sicurezza...');
        client.write(`screendump "${ppmReg2.replace(/\\/g, '/')}"\n`);
        await sleep(1200);

        // Confermiamo la modifica premendo Invio
        console.log('Conferma modifica (sendkey ret)...');
        client.write('sendkey ret\n');
        await sleep(800);

        // Passiamo alla scheda "Audit Log (Sicurezza)" premendo TAB:
        console.log('Passaggio alla scheda Audit Log (sendkey tab)...');
        client.write('sendkey tab\n');
        await sleep(1000);

        // 6. Screenshot 3: Audit Log
        console.log('Cattura Screenshot 3: Audit Log...');
        client.write(`screendump "${ppmReg3.replace(/\\/g, '/')}"\n`);
        await sleep(1200);

        client.write('quit\n');
        client.end();

        // Converti screenshots
        try {
            const ppm2bmp = path.join(ROOT, 'tools', 'ppm2bmp.js');
            const bmp2png = path.join(ROOT, 'tools', 'bmp2png.ps1');

            if (fs.existsSync(ppmReg1)) {
                execSync(`node "${ppm2bmp}" "${ppmReg1}" "${bmpReg1}"`, { stdio: 'ignore' });
                execSync(`powershell -ExecutionPolicy Bypass -File "${bmp2png}" "${bmpReg1}" "${pngReg1}"`, { stdio: 'ignore' });
                console.log(`[OK] Generato: ${pngReg1}`);
            }
            if (fs.existsSync(ppmReg2)) {
                execSync(`node "${ppm2bmp}" "${ppmReg2}" "${bmpReg2}"`, { stdio: 'ignore' });
                execSync(`powershell -ExecutionPolicy Bypass -File "${bmp2png}" "${bmpReg2}" "${pngReg2}"`, { stdio: 'ignore' });
                console.log(`[OK] Generato: ${pngReg2}`);
            }
            if (fs.existsSync(ppmReg3)) {
                execSync(`node "${ppm2bmp}" "${ppmReg3}" "${bmpReg3}"`, { stdio: 'ignore' });
                execSync(`powershell -ExecutionPolicy Bypass -File "${bmp2png}" "${bmpReg3}" "${pngReg3}"`, { stdio: 'ignore' });
                console.log(`[OK] Generato: ${pngReg3}`);
            }
        } catch (e) {
            console.error('Errore conversione:', e);
        }

        setTimeout(() => {
            try { qemu.kill(); } catch (e) {}
            process.exit(0);
        }, 1000);
    });

    client.on('error', (err) => {
        console.error('Errore connessione monitor:', err.message);
        try { qemu.kill(); } catch (e) {}
        process.exit(1);
    });
}, 1000);
