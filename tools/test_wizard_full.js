const { spawn } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const imgPath = path.join(ROOT, 'build', 'myos.img');

const screens = [
    { name: 'step0', ppm: path.join(ROOT, 'screenshot_step0.ppm') },
    { name: 'step1', ppm: path.join(ROOT, 'screenshot_step1.ppm') },
    { name: 'step2', ppm: path.join(ROOT, 'screenshot_step2.ppm') },
    { name: 'step3', ppm: path.join(ROOT, 'screenshot_step3.ppm') },
    { name: 'step4', ppm: path.join(ROOT, 'screenshot_step4.ppm') },
    { name: 'step5', ppm: path.join(ROOT, 'screenshot_step5.ppm') },
];

screens.forEach(s => {
    if (fs.existsSync(s.ppm)) fs.unlinkSync(s.ppm);
});

console.log('Avvio QEMU per test completo Installer Wizard...');
const qemu = spawn(qemuBin, [
    '-drive', `format=raw,file=${imgPath}`,
    '-m', '256M',
    '-vga', 'std',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4449,server,nowait',
    '-serial', `file:${path.join(ROOT, 'serial.log')}`,
    '-d', 'guest_errors',
    '-D', path.join(ROOT, 'qemu_debug.log')
], { stdio: 'ignore' });

function sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

setTimeout(async () => {
    console.log('Connessione a QEMU monitor porta 4449...');
    const client = net.createConnection({ port: 4449 });

    client.on('error', (err) => {
        console.error('Monitor err:', err.message);
    });

    await sleep(200);

    // 1. Invio tasto '2' al Boot Menu
    console.log('1. Selezione opzione 2 nel Boot Menu...');
    client.write('sendkey 2\n');
    await sleep(1500);

    // 2. Step 0: Benvenuto
    console.log('2. Cattura Step 0 (Benvenuto & Hardware)...');
    client.write(`screendump "${screens[0].ppm.replace(/\\/g, '/')}"\n`);
    await sleep(600);

    // Invio Invio -> Step 1
    console.log('Avanzamento a Step 1...');
    client.write('sendkey ret\n');
    await sleep(600);

    // 3. Step 1: Account Utente
    console.log('3. Cattura Step 1 (Account Utente & Cartella Desktop)...');
    client.write(`screendump "${screens[1].ppm.replace(/\\/g, '/')}"\n`);
    await sleep(600);

    // Invio Invio -> Step 2
    console.log('Avanzamento a Step 2...');
    client.write('sendkey ret\n');
    await sleep(600);

    // 4. Step 2: Preferenze Tema ed Energia
    console.log('4. Cattura Step 2 (Tema ed Energia)...');
    client.write(`screendump "${screens[2].ppm.replace(/\\/g, '/')}"\n`);
    await sleep(600);

    // Invio Invio -> Step 3
    console.log('Avanzamento a Step 3...');
    client.write('sendkey ret\n');
    await sleep(600);

    // 5. Step 3: Selezione Disco
    console.log('5. Cattura Step 3 (Selezione Disco ATA)...');
    client.write(`screendump "${screens[3].ppm.replace(/\\/g, '/')}"\n`);
    await sleep(600);

    // Invio Invio -> Step 4 (Avvio Installazione)
    console.log('Avvio installazione su disco (Step 4)...');
    client.write('sendkey ret\n');
    await sleep(300);

    // 6. Step 4: Installazione in corso
    console.log('6. Cattura Step 4 (Installazione a blocchi in corso)...');
    client.write(`screendump "${screens[4].ppm.replace(/\\/g, '/')}"\n`);
    await sleep(2200); // attesa che completi i 5 blocchi e passi a Step 5

    // 7. Step 5: Completamento
    console.log('7. Cattura Step 5 (Completamento Installazione)...');
    client.write(`screendump "${screens[5].ppm.replace(/\\/g, '/')}"\n`);
    await sleep(600);

    console.log('Chiusura QEMU...');
    client.write('quit\n');
    await sleep(500);
    try { client.destroy(); } catch (e) {}
    try { qemu.kill(); } catch (e) {}
    console.log('Tutti i passaggi completati con successo!');
    process.exit(0);
}, 1500);
