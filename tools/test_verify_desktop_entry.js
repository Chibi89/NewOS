const { spawn } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const imgPath = path.join(ROOT, 'myos.img');

const ppmStep5   = path.join(ROOT, 'screenshot_step5_modal.ppm');
const ppmDesktop = path.join(ROOT, 'screenshot_desktop_clean.ppm');
const serialLog  = path.join(ROOT, 'serial_desktop_clean.log');

for (const p of [ppmStep5, ppmDesktop, serialLog]) {
    if (fs.existsSync(p)) fs.unlinkSync(p);
}

console.log('Avvio QEMU per collaudo completo OOBE -> Desktop...');
const qemu = spawn(qemuBin, [
    '-drive', `format=raw,file=${imgPath}`,
    '-m', '256M',
    '-vga', 'std',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-netdev', 'user,id=net0',
    '-device', 'e1000,netdev=net0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4499,server,nowait',
    '-serial', `file:${serialLog}`,
    '-d', 'guest_errors'
], { stdio: 'ignore' });

setTimeout(() => {
    const client = net.createConnection({ port: 4499 });
    client.on('error', (err) => {
        console.log('Monitor socket error handled:', err.message);
    });

    client.on('connect', () => {
        console.log('Connesso al monitor QEMU...');

        // 1. Attendi 3.5s per Bootscreen -> Boot Menu
        setTimeout(() => {
            console.log('  1. Selezione opzione 1 nel Boot Menu...');
            client.write('sendkey 1\n');

            // 2. Attendi 2.5s per avvio OOBE modale a livello di Login
            setTimeout(() => {
                console.log('  2. Step 0 (Benvenuto) -> Step 1 (Display)...');
                client.write('sendkey ret\n');

                setTimeout(() => {
                    console.log('  3. Step 1 (Display) -> Step 2 (Audio)...');
                    client.write('sendkey ret\n');

                    setTimeout(() => {
                        console.log('  4. Step 2 (Audio) -> Step 3 (Rete)...');
                        client.write('sendkey ret\n');

                        setTimeout(() => {
                            console.log('  5. Step 3 (Rete) -> Step 4 (Account)...');
                            client.write('sendkey ret\n');

                            setTimeout(() => {
                                console.log('  6. Step 4 (Account) -> Step 5 (Riepilogo OK)...');
                                client.write('sendkey ret\n');

                                setTimeout(() => {
                                    console.log('  7. Cattura screenshot Step 5 prima del click finale...');
                                    client.write(`screendump "${ppmStep5.replace(/\\/g, '/')}"\n`);

                                    setTimeout(() => {
                                        console.log('  8. Click finale "Avvia NewOS V1" [ret]...');
                                        client.write('sendkey ret\n');

                                        // 3. Attendi 4 secondi per wm_init, wallpaper, sound, explorer
                                        setTimeout(() => {
                                            console.log('  9. Cattura screenshot Desktop...');
                                            client.write(`screendump "${ppmDesktop.replace(/\\/g, '/')}"\n`);

                                            setTimeout(() => {
                                                console.log('  10. Conclusione test...');
                                                client.write('quit\n');
                                                setTimeout(() => {
                                                    try { client.destroy(); } catch (e) {}
                                                    try { qemu.kill(); } catch (e) {}
                                                    console.log('Test completato!');
                                                }, 500);
                                            }, 1000);
                                        }, 4000);
                                    }, 1000);
                                }, 1000);
                            }, 1000);
                        }, 1000);
                    }, 1000);
                }, 1000);
            }, 2500);
        }, 3500);
    });
}, 1000);
