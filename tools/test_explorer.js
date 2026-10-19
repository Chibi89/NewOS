const { spawn } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const imgPath = path.join(ROOT, 'myos.img');

function sleep(ms) {
    return new Promise(res => setTimeout(res, ms));
}

async function runTest() {
    console.log('Avvio QEMU per test navigazione Explorer...');
    const qemu = spawn(qemuBin, [
        '-drive', `format=raw,file=${imgPath}`,
        '-m', '256M',
        '-vga', 'std',
        '-audiodev', 'none,id=snd0',
        '-device', 'AC97,audiodev=snd0',
        '-display', 'none',
        '-monitor', 'tcp:127.0.0.1:4450,server,nowait'
    ], { stdio: 'ignore' });

    await sleep(2500);

    const client = net.createConnection({ port: 4450 }, async () => {
        console.log('Connesso al QEMU monitor...');

        async function moveMouse(dx, dy) {
            const steps = Math.max(Math.ceil(Math.abs(dx) / 40), Math.ceil(Math.abs(dy) / 40), 1);
            const stepX = Math.round(dx / steps);
            const stepY = Math.round(dy / steps);
            for (let i = 0; i < steps; i++) {
                client.write(`mouse_move ${stepX} ${stepY}\n`);
                await sleep(40);
            }
            await sleep(100);
        }

        async function doubleClick() {
            client.write('mouse_button 1\n');
            client.write('mouse_move 0 0\n');
            await sleep(60);
            client.write('mouse_button 0\n');
            client.write('mouse_move 0 0\n');
            await sleep(80);
            client.write('mouse_button 1\n');
            client.write('mouse_move 0 0\n');
            await sleep(60);
            client.write('mouse_button 0\n');
            client.write('mouse_move 0 0\n');
            await sleep(250);
        }

        async function singleClick() {
            client.write('mouse_button 1\n');
            client.write('mouse_move 0 0\n');
            await sleep(60);
            client.write('mouse_button 0\n');
            client.write('mouse_move 0 0\n');
            await sleep(200);
        }

        async function capture(filename) {
            const ppm = path.join(ROOT, filename + '.ppm');
            const bmp = path.join(ROOT, filename + '.bmp');
            const png = path.join(ROOT, filename + '.png');
            if (fs.existsSync(ppm)) fs.unlinkSync(ppm);
            client.write(`screendump "${ppm.replace(/\\/g, '/')}"\n`);
            await sleep(1000);
            if (fs.existsSync(ppm)) {
                require('child_process').execSync(`node "${path.join(ROOT, 'tools', 'ppm2bmp.js')}" "${ppm}" "${bmp}"`);
                require('child_process').execSync(`powershell -ExecutionPolicy Bypass -File "${path.join(ROOT, 'tools', 'bmp2png.ps1')}" "${bmp}" "${png}"`);
                console.log(`[OK] Screenshot salvato: ${png}`);
            } else {
                console.error(`[ERRORE] File ${ppm} non generato`);
            }
        }

        // Mouse parte da (512, 384)
        // 1. Sposta su "Disco locale (C:)" a (324, 189)
        // dx = 324 - 512 = -188, dy = 189 - 384 = -195 (dy negativo sposta in alto!)
        console.log('1. Navigazione in Disco locale (C:)...');
        await moveMouse(-188, -195);
        await doubleClick();
        await sleep(500);
        await capture('screenshot_explorer_drivec');

        // 2. In C:\, Item 0 è la cartella "Progetti" sempre a (324, 189)!
        // Facciamo doppio clic per entrare in C:\Progetti
        console.log('2. Entriamo nella cartella "Progetti"...');
        await doubleClick();
        await sleep(500);
        await capture('screenshot_explorer_folder');

        // 3. Torniamo su con [ Livello sup. ]
        // Livello sup. è a (281, 102).
        // Da (324, 189) a (281, 102): dx = 281 - 324 = -43, dy = 102 - 189 = -87
        console.log('3. Clic su [ Livello sup. ] (torna a C:\\)...');
        await moveMouse(-43, -87);
        await singleClick();
        await sleep(400);

        console.log('4. Clic su [ Livello sup. ] di nuovo (torna a Risorse PC)...');
        await singleClick();
        await sleep(400);

        // 5. Ora siamo di nuovo in Risorse PC.
        // Item 3 è "Pannello di controllo": col 0, row 1
        // ix = 256, iy = 143 + 8 + 76 = 227. Center is (324, 265).
        // Da (281, 102) a (324, 265): dx = 324 - 281 = +43, dy = 265 - 102 = +163
        console.log('5. Navigazione nel Pannello di Controllo...');
        await moveMouse(43, 163);
        await doubleClick();
        await sleep(500);
        await capture('screenshot_explorer_control_panel');

        console.log('Test completato con successo!');
        client.write('quit\n');
        setTimeout(() => {
            try { client.destroy(); } catch (e) {}
            try { qemu.kill(); } catch (e) {}
            process.exit(0);
        }, 500);
    });

    client.on('error', (err) => {
        console.error('Errore monitor:', err.message);
        try { qemu.kill(); } catch (e) {}
        process.exit(1);
    });
}

runTest();
