const { spawn } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const imgPath = path.join(ROOT, 'myos.img');

const ppmDesktopReal = path.join(ROOT, 'screenshot_desktop_real.ppm');
const ppmRebootLogin = path.join(ROOT, 'screenshot_reboot_login.ppm');
const serialLog      = path.join(ROOT, 'serial_finish_reboot.log');

for (const p of [ppmDesktopReal, ppmRebootLogin, serialLog]) {
    if (fs.existsSync(p)) fs.unlinkSync(p);
}

console.log('Avvio QEMU per completare OOBE ed entrare nel desktop...');
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
    const client = net.createConnection({ port: 4488 }, () => {
        // Attendi boot menu
        setTimeout(() => {
            console.log('1. Selezione boot [1]...');
            client.write('sendkey 1\n');

            // Ora siamo in OOBE a livello di login!
            setTimeout(() => {
                console.log('2. Passaggio attraverso i passi OOBE con [ret]...');
                client.write('sendkey ret\n'); // Step 0 -> 1

                setTimeout(() => {
                    client.write('sendkey ret\n'); // Step 1 -> 2

                    setTimeout(() => {
                        client.write('sendkey ret\n'); // Step 2 -> 3

                        setTimeout(() => {
                            client.write('sendkey ret\n'); // Step 3 -> 4

                            setTimeout(() => {
                                client.write('sendkey ret\n'); // Step 4 -> 5

                                setTimeout(() => {
                                    console.log('3. Conferma finale "Avvia NewOS V1"...');
                                    client.write('sendkey ret\n'); // Step 5 -> Completato!

                                    // Attendi caricamento Desktop (wm_init, wallpaper, startup sound)
                                    setTimeout(() => {
                                        console.log('4. Cattura screenshot Desktop reale...');
                                        client.write(`screendump "${ppmDesktopReal.replace(/\\/g, '/')}"\n`);

                                        setTimeout(() => {
                                            client.write('quit\n');
                                            setTimeout(() => {
                                                try { client.destroy(); } catch (e) {}
                                                try { qemu.kill(); } catch (e) {}
                                                console.log('Prima sessione completata con successo!');
                                            }, 500);
                                        }, 1000);
                                    }, 4000);
                                }, 800);
                            }, 800);
                        }, 800);
                    }, 800);
                }, 800);
            }, 2500);
        }, 3200);
    });
}, 1000);
