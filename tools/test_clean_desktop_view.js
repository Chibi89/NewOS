const { spawn } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const imgPath = path.join(ROOT, 'myos.img');

const ppmLoginScreen = path.join(ROOT, 'screenshot_loginscreen_live.ppm');

if (fs.existsSync(ppmLoginScreen)) fs.unlinkSync(ppmLoginScreen);

console.log('Avvio QEMU per cattura schermata...');
const qemu = spawn(qemuBin, [
    '-drive', `format=raw,file=${imgPath}`,
    '-m', '256M',
    '-vga', 'std',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-netdev', 'user,id=net0',
    '-device', 'e1000,netdev=net0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4433,server,nowait',
    '-d', 'guest_errors'
], { stdio: 'ignore' });

setTimeout(() => {
    const client = net.createConnection({ port: 4433 });
    client.on('error', () => {});

    client.on('connect', () => {
        // Il menu di boot ha 10s di countdown. Inviamo '1' e 'ret' al 5° secondo
        setTimeout(() => {
            console.log('Invio scelta 1...');
            client.write('sendkey 1\n');
            setTimeout(() => {
                client.write('sendkey ret\n');

                // Attendi 4 secondi
                setTimeout(() => {
                    console.log('Cattura screenshot...');
                    client.write(`screendump "${ppmLoginScreen.replace(/\\/g, '/')}"\n`);

                    setTimeout(() => {
                        client.write('quit\n');
                        setTimeout(() => {
                            try { client.destroy(); } catch (e) {}
                            try { qemu.kill(); } catch (e) {}
                            console.log('Fatto!');
                        }, 500);
                    }, 1000);
                }, 4000);
            }, 500);
        }, 4500);
    });
}, 1000);
