const { spawn } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const imgPath = path.join(ROOT, 'myos.img');

const ppmDesktopFin = path.join(ROOT, 'screenshot_desktop_real_final.ppm');
const serialLog     = path.join(ROOT, 'serial_final_desktop.log');

for (const p of [ppmDesktopFin, serialLog]) {
    if (fs.existsSync(p)) fs.unlinkSync(p);
}

console.log('Avvio QEMU per completare OOBE e catturare il vero Desktop...');
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
    const client = net.createConnection({ port: 4466 });
    client.on('error', (err) => console.log('Socket error:', err.message));

    client.on('connect', () => {
        setTimeout(() => {
            console.log('  -> Boot menu [1]');
            client.write('sendkey 1\n');

            setTimeout(() => {
                // Inviamo 6 volte [ret] con 600ms di intervallo per attraversare tutti i passi:
                // 1: Step 0 -> 1
                // 2: Step 1 -> 2
                // 3: Step 2 -> 3
                // 4: Step 3 -> 4
                // 5: Step 4 -> 5
                // 6: Step 5 -> Avvia NewOS V1 (Completato!)
                let count = 0;
                const interval = setInterval(() => {
                    count++;
                    console.log(`  -> Invio ret #${count}...`);
                    client.write('sendkey ret\n');
                    if (count >= 8) {
                        clearInterval(interval);
                        console.log('  -> Tutti gli 8 invii completati. Attendo caricamento desktop...');

                        setTimeout(() => {
                            console.log('  -> Cattura screenshot Desktop definitivo...');
                            client.write(`screendump "${ppmDesktopFin.replace(/\\/g, '/')}"\n`);

                            setTimeout(() => {
                                client.write('quit\n');
                                setTimeout(() => {
                                    try { client.destroy(); } catch (e) {}
                                    try { qemu.kill(); } catch (e) {}
                                    console.log('Test completato!');
                                }, 500);
                            }, 1000);
                        }, 5000); // tempo per wm_init e desktop
                    }
                }, 800);
            }, 2500);
        }, 3200);
    });
}, 1000);
