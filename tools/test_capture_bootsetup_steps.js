const { spawn } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');
const { execSync } = require('child_process');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const imgPath = path.join(ROOT, 'myos.img');
const brainDir = 'C:\\Users\\morga\\.gemini\\antigravity-ide\\brain\\f8b9e865-1f04-4340-9815-41144a7b521d';

const steps = [
    { name: 'bootsetup_step0_welcome', ppm: path.join(ROOT, 'bootsetup_step0.ppm') },
    { name: 'bootsetup_step1_hardware', ppm: path.join(ROOT, 'bootsetup_step1.ppm') },
    { name: 'bootsetup_step2_partition', ppm: path.join(ROOT, 'bootsetup_step2.ppm') },
    { name: 'bootsetup_step3_extract', ppm: path.join(ROOT, 'bootsetup_step3.ppm') },
    { name: 'bootsetup_step4_complete', ppm: path.join(ROOT, 'bootsetup_step4.ppm') }
];

for (const s of steps) {
    if (fs.existsSync(s.ppm)) fs.unlinkSync(s.ppm);
}

const serialLog = path.join(ROOT, 'serial_bootsetup_steps.log');
if (fs.existsSync(serialLog)) fs.unlinkSync(serialLog);

console.log('[TEST] Avvio QEMU per cattura precisa di ogni step di BootSetup...');
const qemu = spawn(qemuBin, [
    '-drive', `format=raw,file=${imgPath}`,
    '-m', '256M',
    '-vga', 'std',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-netdev', 'user,id=net0',
    '-device', 'e1000,netdev=net0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4498,server,nowait',
    '-serial', `file:${serialLog}`,
    '-d', 'guest_errors'
], { stdio: 'ignore' });

function sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

setTimeout(async () => {
    const client = net.createConnection({ port: 4498 });
    client.on('error', (err) => console.log('Socket error:', err.message));

    await new Promise(resolve => client.once('connect', resolve));
    console.log('[TEST] Connesso al monitor QEMU.');

    // 1. Attendi avvio boot menu
    await sleep(4500);

    // 2. Seleziona BootSetup (opzione 2)
    console.log('[TEST] Selezione opzione 2...');
    client.write('sendkey 2\n');
    await sleep(2500);

    // Step 0: Benvenuto
    console.log('[TEST] Cattura Passo 0: Benvenuto...');
    client.write(`screendump "${steps[0].ppm.replace(/\\/g, '/')}"\n`);
    await sleep(2000);

    // Step 1: Hardware ATA
    console.log('[TEST] Invio [ret] -> Passo 1: Hardware ATA...');
    client.write('sendkey ret\n');
    await sleep(2000);
    console.log('[TEST] Cattura Passo 1: Hardware ATA...');
    client.write(`screendump "${steps[1].ppm.replace(/\\/g, '/')}"\n`);
    await sleep(2000);

    // Step 2: Partizione exFAT
    console.log('[TEST] Invio [ret] -> Passo 2: Partizione exFAT...');
    client.write('sendkey ret\n');
    await sleep(2000);
    console.log('[TEST] Cattura Passo 2: Partizione exFAT...');
    client.write(`screendump "${steps[2].ppm.replace(/\\/g, '/')}"\n`);
    await sleep(2000);

    // Step 3: Avvia Decifratura ed Estrazione AES-256
    console.log('[TEST] Invio [ret] -> Passo 3: Decifratura ed Estrazione AES-256...');
    client.write('sendkey ret\n');
    await sleep(300);
    client.write('sendkey ret\n');
    await sleep(1500);
    console.log('[TEST] Cattura Passo 3 durante estrazione live...');
    client.write(`screendump "${steps[3].ppm.replace(/\\/g, '/')}"\n`);

    // Attendi completamento (passo 4 automatico dopo 148 file)
    console.log('[TEST] Attesa completamento estrazione (passaggio a Passo 4)...');
    await sleep(15000);
    console.log('[TEST] Cattura Passo 4: Installazione Completata...');
    client.write(`screendump "${steps[4].ppm.replace(/\\/g, '/')}"\n`);
    await sleep(2000);

    console.log('[TEST] Chiusura QEMU e conversione immagini...');
    client.write('quit\n');
    client.end();
    try { qemu.kill(); } catch (e) {}

    // Conversione PPM -> PNG
    for (const s of steps) {
        const bmpPath = s.ppm.replace('.ppm', '.bmp');
        const pngPath = path.join(brainDir, `${s.name}.png`);
        if (fs.existsSync(s.ppm)) {
            execSync(`node "${path.join(ROOT, 'tools', 'ppm2bmp.js')}" "${s.ppm}" "${bmpPath}"`, { stdio: 'inherit' });
            execSync(`powershell -ExecutionPolicy Bypass -File "${path.join(ROOT, 'tools', 'bmp2png.ps1')}" "${bmpPath}" "${pngPath}"`, { stdio: 'inherit' });
            console.log(`[OK] Generato: ${s.name}.png`);
        }
    }
    console.log('[TEST COMPLETATO CON SUCCESSO]');
    process.exit(0);
}, 1000);
