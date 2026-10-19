const { spawn } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const imgPath = path.join(ROOT, 'myos.img');
const ppmBoot = path.join(ROOT, 'screenshot_bootscreen.ppm');
const ppmLogin = path.join(ROOT, 'screenshot_loginscreen.ppm');
const serialLog = path.join(ROOT, 'serial_screens.log');

if (fs.existsSync(ppmBoot)) fs.unlinkSync(ppmBoot);
if (fs.existsSync(ppmLogin)) fs.unlinkSync(ppmLogin);
if (fs.existsSync(serialLog)) fs.unlinkSync(serialLog);

console.log('Avvio QEMU per collaudo Moduli Schermata Protetti...');
const qemu = spawn(qemuBin, [
    '-drive', `format=raw,file=${imgPath}`,
    '-m', '256M',
    '-vga', 'std',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4455,server,nowait',
    '-serial', `file:${serialLog}`,
    '-d', 'guest_errors'
], { stdio: 'ignore' });

setTimeout(() => {
    console.log('Connessione al monitor QEMU per cattura Bootscreen...');
    const client = net.createConnection({ port: 4455 }, () => {
        // 1. Cattura screenshot del Bootscreen
        client.write(`screendump "${ppmBoot.replace(/\\/g, '/')}"\n`);
        console.log('  -> Screendump Bootscreen inviato.');

        // 2. Dopo 2.8 secondi (fine bootscreen e menu avvio), invia '1' per Live
        setTimeout(() => {
            console.log('  -> Invio tasto [1] per selezione avvio Live...');
            client.write('sendkey 1\n');

            // 3. Attendi che il modulo protetto LOGIN.MOD carichi e visualizzi la schermata
            setTimeout(() => {
                console.log('  -> Cattura screenshot Loginscreen...');
                client.write(`screendump "${ppmLogin.replace(/\\/g, '/')}"\n`);

                setTimeout(() => {
                    console.log('  -> Invio tasto [ret] per login utente...');
                    client.write('sendkey ret\n');

                    setTimeout(() => {
                        client.write('quit\n');
                        setTimeout(() => {
                            try { client.destroy(); } catch (e) {}
                            try { qemu.kill(); } catch (e) {}

                            if (fs.existsSync(serialLog)) {
                                console.log('\n=== LOG SERIALE REGISTRATO DAL KERNEL ===');
                                console.log(fs.readFileSync(serialLog, 'utf8'));
                            }
                            process.exit(0);
                        }, 500);
                    }, 1000);
                }, 1500);
            }, 2500);
        }, 2800);
    });

    client.on('error', (err) => {
        console.error('Errore connessione monitor:', err.message);
        try { qemu.kill(); } catch (e) {}
        process.exit(1);
    });
}, 1200);
