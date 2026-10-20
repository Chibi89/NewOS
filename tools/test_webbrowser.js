const { spawn, execSync } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const imgPath = path.join(ROOT, 'build', 'NewOS_V1.img');
const ppmBrowser = path.join(ROOT, 'screenshot_browser.ppm');
const bmpBrowser = path.join(ROOT, 'screenshot_browser.bmp');
const pngBrowser = 'C:\\Users\\morga\\.gemini\\antigravity-ide\\brain\\cce9850e-402e-48dd-9a6f-1743e743feab\\screenshot_webbrowser.png';

const ppmNet = path.join(ROOT, 'screenshot_browser_net.ppm');
const bmpNet = path.join(ROOT, 'screenshot_browser_net.bmp');
const pngNet = 'C:\\Users\\morga\\.gemini\\antigravity-ide\\brain\\cce9850e-402e-48dd-9a6f-1743e743feab\\screenshot_webbrowser_net.png';

if (fs.existsSync(ppmBrowser)) fs.unlinkSync(ppmBrowser);
if (fs.existsSync(ppmNet)) fs.unlinkSync(ppmNet);

console.log('[TEST] Avvio QEMU per collaudo visuale NewOS Web Browser...');
const qemu = spawn(qemuBin, [
    '-drive', `format=raw,file=${imgPath}`,
    '-m', '256M',
    '-vga', 'std',
    '-netdev', 'user,id=net0',
    '-device', 'e1000,netdev=net0',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4460,server,nowait',
    '-serial', `file:${path.join(ROOT, 'serial_browser.log')}`,
    '-d', 'guest_errors',
    '-D', path.join(ROOT, 'qemu_debug.log')
], { stdio: 'ignore' });

function sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

setTimeout(async () => {
    console.log('Connessione al monitor QEMU (porta 4460)...');
    const client = net.createConnection({ port: 4460 });

    client.on('error', (err) => console.error('Monitor err:', err.message));
    await sleep(500);

    // Attendiamo che il Desktop Live completi il boot e apra Web Browser (4.5s)
    console.log('Attesa caricamento desktop e avvio Web Browser...');
    await sleep(4500);

    // Cattura screenshot della Home Page del Web Browser
    console.log('Cattura screenshot Web Browser Home Page...');
    client.write(`screendump "${ppmBrowser.replace(/\\/g, '/')}"\n`);
    await sleep(1000);

    // Simuliamo la navigazione verso "about:rules" digitando nella barra degli indirizzi o inviando comando
    // Finestra Web Browser è centrata: x=132, y=104, w=760, h=520
    // Barra degli indirizzi: x=212, y=177 (altezza 22, larghezza 576)
    // Tasto "Vai": x=800, y=177
    console.log('Interazione con Web Browser: digitazione about:network...');
    // Clic sulla barra degli indirizzi (x=300, y=188)
    // Portiamo prima a (0, 0)
    for (let i = 0; i < 20; i++) {
        client.write('mouse_move -60 60\n');
        await sleep(20);
    }
    await sleep(100);

    // Spostamento a (300, 188)
    client.write('mouse_move 300 -188\n');
    await sleep(200);
    client.write('mouse_button 1\n');
    await sleep(60);
    client.write('mouse_button 0\n');
    await sleep(200);

    // Digitiamo "about:rules" e premiamo invio
    for (const ch of 'about:rules') {
        client.write(`sendkey ${ch}\n`);
        await sleep(50);
    }
    client.write('sendkey ret\n');
    await sleep(2000);

    // Cattura secondo screenshot dopo la navigazione su about:rules
    console.log('Cattura screenshot navigazione a about:rules...');
    client.write(`screendump "${ppmNet.replace(/\\/g, '/')}"\n`);
    await sleep(1000);

    client.write('quit\n');
    await sleep(400);
    try { client.destroy(); } catch (e) {}
    try { qemu.kill(); } catch (e) {}

    // Conversione PPM -> BMP -> PNG
    if (fs.existsSync(ppmBrowser)) {
        execSync(`node "${path.join(ROOT, 'tools', 'ppm2bmp.js')}" "${ppmBrowser}" "${bmpBrowser}"`, { stdio: 'inherit' });
        execSync(`powershell -ExecutionPolicy Bypass -File "${path.join(ROOT, 'tools', 'bmp2png.ps1')}" "${bmpBrowser}" "${pngBrowser}"`, { stdio: 'inherit' });
        console.log(`[OK] Screenshot Home Web Browser salvato in: ${pngBrowser}`);
    }

    if (fs.existsSync(ppmNet)) {
        execSync(`node "${path.join(ROOT, 'tools', 'ppm2bmp.js')}" "${ppmNet}" "${bmpNet}"`, { stdio: 'inherit' });
        execSync(`powershell -ExecutionPolicy Bypass -File "${path.join(ROOT, 'tools', 'bmp2png.ps1')}" "${bmpNet}" "${pngNet}"`, { stdio: 'inherit' });
        console.log(`[OK] Screenshot Navigazione Web Browser salvato in: ${pngNet}`);
    }

    process.exit(0);
}, 1500);
