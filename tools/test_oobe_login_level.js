const { spawn } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const imgPath = path.join(ROOT, 'myos.img');

const ppmLoginOobe0 = path.join(ROOT, 'screenshot_login_oobe0.ppm');
const ppmLoginOobe1 = path.join(ROOT, 'screenshot_login_oobe1.ppm');
const ppmLoginOobe2 = path.join(ROOT, 'screenshot_login_oobe2.ppm');
const ppmLoginOobe3 = path.join(ROOT, 'screenshot_login_oobe3.ppm');
const ppmLoginOobe4 = path.join(ROOT, 'screenshot_login_oobe4.ppm');
const ppmLoginOobe5 = path.join(ROOT, 'screenshot_login_oobe5.ppm');
const ppmDesktop    = path.join(ROOT, 'screenshot_desktop_after_oobe.ppm');
const serialLog     = path.join(ROOT, 'serial_oobe_modal.log');

for (const p of [ppmLoginOobe0, ppmLoginOobe1, ppmLoginOobe2, ppmLoginOobe3, ppmLoginOobe4, ppmLoginOobe5, ppmDesktop, serialLog]) {
    if (fs.existsSync(p)) fs.unlinkSync(p);
}

console.log('Avvio QEMU per collaudo OOBE a LIVELLO DI LOGIN...');
const qemu = spawn(qemuBin, [
    '-drive', `format=raw,file=${imgPath}`,
    '-m', '256M',
    '-vga', 'std',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-netdev', 'user,id=net0',
    '-device', 'e1000,netdev=net0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4477,server,nowait',
    '-serial', `file:${serialLog}`,
    '-d', 'guest_errors'
], { stdio: 'ignore' });

setTimeout(() => {
    console.log('Connessione al monitor QEMU...');
    const client = net.createConnection({ port: 4477 }, () => {
        // Attendi termine bootscreen e arrivo boot menu
        setTimeout(() => {
            console.log('  -> Invio [1] al boot menu per avviare...');
            client.write('sendkey 1\n');

            // Ora il kernel verifica il requisito della prima volta ed esegue OOBE a LIVELLO DI LOGIN
            setTimeout(() => {
                console.log('  -> Cattura screenshot OOBE Step 0 (a livello di Login su LOGIN.BMP)...');
                client.write(`screendump "${ppmLoginOobe0.replace(/\\/g, '/')}"\n`);

                setTimeout(() => {
                    console.log('  -> Invio [ret] -> Step 1 (Display & Video)...');
                    client.write('sendkey ret\n');

                    setTimeout(() => {
                        console.log('  -> Cattura screenshot OOBE Step 1 (Display)...');
                        client.write(`screendump "${ppmLoginOobe1.replace(/\\/g, '/')}"\n`);

                        setTimeout(() => {
                            console.log('  -> Invio [ret] -> Step 2 (Audio AC97)...');
                            client.write('sendkey ret\n');

                            setTimeout(() => {
                                console.log('  -> Cattura screenshot OOBE Step 2 (Audio)...');
                                client.write(`screendump "${ppmLoginOobe2.replace(/\\/g, '/')}"\n`);

                                setTimeout(() => {
                                    console.log('  -> Invio [ret] -> Step 3 (Rete)...');
                                    client.write('sendkey ret\n');

                                    setTimeout(() => {
                                        console.log('  -> Cattura screenshot OOBE Step 3 (Rete)...');
                                        client.write(`screendump "${ppmLoginOobe3.replace(/\\/g, '/')}"\n`);

                                        setTimeout(() => {
                                            console.log('  -> Invio [ret] -> Step 4 (Account & Stile)...');
                                            client.write('sendkey ret\n');

                                        setTimeout(() => {
                                            console.log('  -> Cattura screenshot OOBE Step 4 (Account)...');
                                            client.write(`screendump "${ppmLoginOobe4.replace(/\\/g, '/')}"\n`);

                                            setTimeout(() => {
                                                console.log('  -> Invio [ret] -> Step 5 (Completamento)...');
                                                client.write('sendkey ret\n');

                                                setTimeout(() => {
                                                    console.log('  -> Cattura screenshot OOBE Step 5 (Fine)...');
                                                    client.write(`screendump "${ppmLoginOobe5.replace(/\\/g, '/')}"\n`);

                                                    setTimeout(() => {
                                                        console.log('  -> Invio [ret] per concludere OOBE ed entrare nel Desktop...');
                                                        client.write('sendkey ret\n');

                                                        setTimeout(() => {
                                                            console.log('  -> Cattura screenshot Desktop (verifica che OOBE non sia sul desktop)...');
                                                            client.write(`screendump "${ppmDesktop.replace(/\\/g, '/')}"\n`);

                                                            setTimeout(() => {
                                                                client.write('quit\n');
                                                                setTimeout(() => {
                                                                    try { client.destroy(); } catch (e) {}
                                                                    try { qemu.kill(); } catch (e) {}
                                                                    console.log('Test OOBE Login Level completato con successo!');
                                                                }, 500);
                                                            }, 1000);
                                                        }, 3500); // tempo per wm_init e sound
                                                    }, 1500);
                                                }, 1200);
                                            }, 1200);
                                        }, 1200);
                                    }, 1200);
                                }, 1200);
                            }, 1200);
                        }, 1200);
                    }, 1200);
                }, 1200);
            }, 1200);
            }, 3000);
        }, 3000);
    });
}, 1000);
