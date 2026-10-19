const { spawn } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const imgPath = path.join(ROOT, 'build', 'myos.img');
const ppmStep0 = path.join(ROOT, 'screenshot_installer_step0.ppm');
const ppmStep1 = path.join(ROOT, 'screenshot_installer_step1.ppm');

if (fs.existsSync(ppmStep0)) fs.unlinkSync(ppmStep0);
if (fs.existsSync(ppmStep1)) fs.unlinkSync(ppmStep1);

console.log('Avvio QEMU per test Installer...');
const qemu = spawn(qemuBin, [
    '-drive', `format=raw,file=${imgPath}`,
    '-m', '256M',
    '-vga', 'std',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4446,server,nowait',
    '-serial', `file:${path.join(ROOT, 'serial.log')}`,
    '-d', 'guest_errors',
    '-D', path.join(ROOT, 'qemu_debug.log')
], { stdio: 'ignore' });

setTimeout(() => {
    console.log('Connessione al QEMU monitor su porta 4446...');
    let sentQuit = false;
    const client = net.createConnection({ port: 4446 }, () => {
        console.log('Invio tasto "2" per scegliere Installazione...');
        // sendkey 2
        client.write('sendkey 2\n');

        // Attendiamo che il kernel avvii il desktop e l'installer (1.5 sec)
        setTimeout(() => {
            console.log('Cattura screenshot Step 0 (Benvenuto & Hardware)...');
            client.write(`screendump "${ppmStep0.replace(/\\/g, '/')}"\n`);

            setTimeout(() => {
                // Calcoliamo la posizione del pulsante "Avanti >"
                // Installer window: dx = (1024 - 640)/2 = 192, dy = (768 - 430)/2 = 169
                // "Avanti >" in win coordinates:
                // cx = 4, cy = 24, cw = 640 - 8 = 632, ch = 430 - 28 = 402
                // by = cy + ch - 36 = 24 + 402 - 36 = 390
                // next_x = cx + cw - 88 = 4 + 632 - 88 = 548
                // In screen coords: screen_x = dx + next_x + 40 = 192 + 548 + 40 = 780
                // screen_y = dy + by + 12 = 169 + 390 + 12 = 571
                console.log('Spostamento mouse sul pulsante "Avanti >" e clic...');
                // Muoviamo il mouse su x=780, y=571 (da posizione iniziale 512, 384: dx = 780-512 = 268, dy: current_mouse.y -= dy, so dy = -(571 - 384) = -187)
                client.write(`mouse_move 268 -187\n`);
                setTimeout(() => {
                    client.write('mouse_button 1\n');
                    setTimeout(() => {
                        client.write('mouse_button 0\n');
                        setTimeout(() => {
                            console.log('Cattura screenshot Step 1 (Account Utente & Desktop)...');
                            client.write(`screendump "${ppmStep1.replace(/\\/g, '/')}"\n`);
                            setTimeout(() => {
                                sentQuit = true;
                                client.write('quit\n');
                                setTimeout(() => {
                                    try { client.destroy(); } catch (e) {}
                                    try { qemu.kill(); } catch (e) {}
                                    process.exit(0);
                                }, 500);
                            }, 1000);
                        }, 600);
                    }, 150);
                }, 400);
            }, 1200);
        }, 1500);
    });

    client.on('error', (err) => {
        if (!sentQuit) {
            console.error('Errore monitor:', err.message);
            try { qemu.kill(); } catch (e) {}
            process.exit(1);
        }
    });
}, 1500);
