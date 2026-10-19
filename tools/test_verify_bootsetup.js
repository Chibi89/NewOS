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
    { name: 'bootsetup_step4_complete', ppm: path.join(ROOT, 'bootsetup_step4.ppm') },
    { name: 'bootsetup_step5_oobe', ppm: path.join(ROOT, 'bootsetup_step5_oobe.ppm') }
];

for (const s of steps) {
    if (fs.existsSync(s.ppm)) fs.unlinkSync(s.ppm);
}

const serialLog = path.join(ROOT, 'serial_bootsetup.log');
if (fs.existsSync(serialLog)) fs.unlinkSync(serialLog);

console.log('[TEST] Avvio QEMU per test e verifica completa BootSetup Installer...');
const qemu = spawn(qemuBin, [
    '-drive', `format=raw,file=${imgPath}`,
    '-m', '256M',
    '-vga', 'std',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-netdev', 'user,id=net0',
    '-device', 'e1000,netdev=net0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4488,server,nowait',
    '-serial', `file:${serialLog}`,
    '-d', 'guest_errors'
], { stdio: 'ignore' });

setTimeout(() => {
    const client = net.createConnection({ port: 4488 });
    client.on('error', (err) => console.log('Socket error:', err.message));

    client.on('connect', () => {
        console.log('[TEST] Connesso al monitor QEMU.');

        // 1. Al menu di boot selezioniamo l'opzione 2 (Installa NewOS V1)
        setTimeout(() => {
            console.log('[TEST] Invio tasto 2 (Scelta Boot: Installa NewOS V1)...');
            client.write('sendkey 2\n');

            // 2. Cattura Step 0: Benvenuto
            setTimeout(() => {
                console.log('[TEST] Cattura Passo 0: Benvenuto Wizard...');
                client.write(`screendump "${steps[0].ppm.replace(/\\/g, '/')}"\n`);

                // 3. Invio [Invio] -> Passo 1: Hardware ATA
                setTimeout(() => {
                    console.log('[TEST] Invio [ret] -> Passo 1 (Hardware ATA)...');
                    client.write('sendkey ret\n');

                    setTimeout(() => {
                        console.log('[TEST] Cattura Passo 1: Hardware ATA...');
                        client.write(`screendump "${steps[1].ppm.replace(/\\/g, '/')}"\n`);

                        // 4. Invio [Invio] -> Passo 2: Partizione exFAT
                        setTimeout(() => {
                            console.log('[TEST] Invio [ret] -> Passo 2 (Partizione exFAT)...');
                            client.write('sendkey ret\n');

                            setTimeout(() => {
                                console.log('[TEST] Cattura Passo 2: Partizione exFAT...');
                                client.write(`screendump "${steps[2].ppm.replace(/\\/g, '/')}"\n`);

                                // 5. Invio [Invio] -> Passo 3: Decifratura ed Estrazione AES-256
                                setTimeout(() => {
                                    console.log('[TEST] Invio [ret] -> Passo 3 (Decifratura ed Estrazione AES-256)...');
                                    client.write('sendkey ret\n');

                                    // Cattura durante l'estrazione a metà avanzamento
                                    setTimeout(() => {
                                        console.log('[TEST] Cattura Passo 3 durante estrazione live...');
                                        client.write(`screendump "${steps[3].ppm.replace(/\\/g, '/')}"\n`);

                                        // Attesa completamento estrazione (passa automaticamente a Step 4)
                                        setTimeout(() => {
                                            console.log('[TEST] Cattura Passo 4: Installazione Completata...');
                                            client.write(`screendump "${steps[4].ppm.replace(/\\/g, '/')}"\n`);

                                            // 6. Invio [Invio] per avviare OOBE (Regola 3)
                                            setTimeout(() => {
                                                console.log('[TEST] Invio [ret] -> Handoff a OOBE...');
                                                client.write('sendkey ret\n');

                                                setTimeout(() => {
                                                    console.log('[TEST] Cattura Passo 5: Schermata OOBE aperta dopo installazione!');
                                                    client.write(`screendump "${steps[5].ppm.replace(/\\/g, '/')}"\n`);

                                                    setTimeout(() => {
                                                        console.log('[TEST] Chiusura QEMU e conversione screenshot...');
                                                        client.write('quit\n');
                                                        client.end();
                                                        try { qemu.kill(); } catch (e) {}

                                                        // Converti PPM in PNG
                                                        for (const s of steps) {
                                                            const bmpPath = s.ppm.replace('.ppm', '.bmp');
                                                            const pngPath = path.join(brainDir, `${s.name}.png`);
                                                            if (fs.existsSync(s.ppm)) {
                                                                try {
                                                                    execSync(`node "${path.join(ROOT, 'tools', 'ppm2bmp.js')}" "${s.ppm}" "${bmpPath}"`, { stdio: 'ignore' });
                                                                    execSync(`powershell -ExecutionPolicy Bypass -File "${path.join(ROOT, 'tools', 'bmp2png.ps1')}" "${bmpPath}" "${pngPath}"`, { stdio: 'ignore' });
                                                                    console.log(`[OK] Generato: ${s.name}.png`);
                                                                } catch (err) {
                                                                    console.error(`Errore conversione ${s.name}:`, err.message);
                                                                }
                                                            }
                                                        }
                                                        process.exit(0);
                                                    }, 1500);
                                                }, 2500);
                                            }, 1200);
                                        }, 4000);
                                    }, 1200);
                                }, 1200);
                            }, 1200);
                        }, 1200);
                    }, 1200);
                }, 1200);
            }, 1800);
        }, 2500);
    });
}, 1000);
