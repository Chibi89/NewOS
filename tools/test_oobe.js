const { spawn } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const imgPath = path.join(ROOT, 'myos.img');
const ppmOobe0 = path.join(ROOT, 'screenshot_oobe_step0.ppm');
const ppmOobe1 = path.join(ROOT, 'screenshot_oobe_step1.ppm');
const ppmOobe2 = path.join(ROOT, 'screenshot_oobe_step2.ppm');
const ppmOobe3 = path.join(ROOT, 'screenshot_oobe_step3.ppm');
const serialLog = path.join(ROOT, 'serial_oobe.log');

for (const p of [ppmOobe0, ppmOobe1, ppmOobe2, ppmOobe3, serialLog]) {
    if (fs.existsSync(p)) fs.unlinkSync(p);
}

console.log('Avvio QEMU per collaudo guidato OOBE...');
const qemu = spawn(qemuBin, [
    '-drive', `format=raw,file=${imgPath}`,
    '-m', '256M',
    '-vga', 'std',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-netdev', 'user,id=net0',
    '-device', 'e1000,netdev=net0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4466,server,nowait',
    '-serial', `file:${serialLog}`,
    '-d', 'guest_errors'
], { stdio: 'ignore' });

setTimeout(() => {
    console.log('Connessione al monitor QEMU...');
    const client = net.createConnection({ port: 4466 }, () => {
        // Attendi 3.5 secondi (bootscreen + boot menu -> default 1 o OOBE)
        setTimeout(() => {
            console.log('  -> Invio [1] al boot menu per avviare...');
            client.write('sendkey 1\n');

            // Attendi caricamento desktop e OOBE
            setTimeout(() => {
                console.log('  -> Invio [ret] per login...');
                client.write('sendkey ret\n');

                setTimeout(() => {
                    console.log('  -> Cattura screenshot OOBE Step 0 (Benvenuto)...');
                    client.write(`screendump "${ppmOobe0.replace(/\\/g, '/')}"\n`);

                    setTimeout(() => {
                        console.log('  -> Invio [ret] per passare a Step 1 (Video)...');
                        client.write('sendkey ret\n');

                        setTimeout(() => {
                            console.log('  -> Cattura screenshot OOBE Step 1 (Video)...');
                            client.write(`screendump "${ppmOobe1.replace(/\\/g, '/')}"\n`);

                            setTimeout(() => {
                                console.log('  -> Invio [ret] per passare a Step 2 (Audio)...');
                                client.write('sendkey ret\n');

                                setTimeout(() => {
                                    console.log('  -> Cattura screenshot OOBE Step 2 (Audio)...');
                                    client.write(`screendump "${ppmOobe2.replace(/\\/g, '/')}"\n`);

                                    setTimeout(() => {
                                        console.log('  -> Invio [ret] per passare a Step 3 (Rete)...');
                                        client.write('sendkey ret\n');

                                        setTimeout(() => {
                                            console.log('  -> Cattura screenshot OOBE Step 3 (Rete)...');
                                            client.write(`screendump "${ppmOobe3.replace(/\\/g, '/')}"\n`);

                                            setTimeout(() => {
                                                client.write('quit\n');
                                                setTimeout(() => {
                                                    try { client.destroy(); } catch (e) {}
                                                    try { qemu.kill(); } catch (e) {}
                                                    console.log('Collaudo OOBE terminato con successo.');
                                                }, 500);
                                            }, 1000);
                                        }, 1000);
                                    }, 1000);
                                }, 1000);
                            }, 1000);
                        }, 1000);
                    }, 1000);
                }, 1500);
            }, 3000);
        }, 3200);
    });
}, 1000);
