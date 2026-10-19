const { spawn } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const imgPath = path.join(ROOT, 'myos.img');

const ppmSecondBoot = path.join(ROOT, 'screenshot_second_boot_loginscreen.ppm');
const serialLog     = path.join(ROOT, 'serial_second_boot.log');

if (fs.existsSync(ppmSecondBoot)) fs.unlinkSync(ppmSecondBoot);
if (fs.existsSync(serialLog)) fs.unlinkSync(serialLog);

console.log('Avvio QEMU secondo boot: OOBE è già completato, deve mostrare la Login Screen...');
const qemu = spawn(qemuBin, [
    '-drive', `format=raw,file=${imgPath}`,
    '-m', '256M',
    '-vga', 'std',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-netdev', 'user,id=net0',
    '-device', 'e1000,netdev=net0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4455,server,nowait',
    '-serial', `file:${serialLog}`,
    '-d', 'guest_errors'
], { stdio: 'ignore' });

setTimeout(() => {
    const client = net.createConnection({ port: 4455 });
    client.on('error', (err) => console.log('Socket error:', err.message));

    client.on('connect', () => {
        setTimeout(() => {
            console.log('  -> Boot menu [1]');
            client.write('sendkey 1\n');

            // Poiché OOBE è completato, deve avviare direttamente la schermata di Logon standard LOGIN.MOD!
            setTimeout(() => {
                console.log('  -> Cattura schermata del secondo boot (Login screen)...');
                client.write(`screendump "${ppmSecondBoot.replace(/\\/g, '/')}"\n`);

                setTimeout(() => {
                    client.write('quit\n');
                    setTimeout(() => {
                        try { client.destroy(); } catch (e) {}
                        try { qemu.kill(); } catch (e) {}
                        console.log('Secondo boot testato con successo!');
                    }, 500);
                }, 1000);
            }, 3000);
        }, 3200);
    });
}, 1000);
