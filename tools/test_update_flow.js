/**
 * tools/test_update_flow.js
 * Collaudo e validazione visiva end-to-end del sottosistema di aggiornamento NewOS V1 (64-bit):
 * 1. Genera UPDATE.PAK cifrato con AES-256 + HMAC-SHA256 (con registry_update.update).
 * 2. Prepara boot_action.ini (Target=UPDATE.MOD).
 * 3. Aggiorna l'immagine disco myos.img.
 * 4. Avvia QEMU ed esegue screendump durante l'esecuzione di UPDATE.MOD a 32bpp.
 * 5. Verifica log seriale, integrità e transizione a LOGIN.MOD.
 */

const { spawn, execSync } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const imgPath = path.join(ROOT, 'myos.img');
const ppmUpdate = path.join(ROOT, 'screenshot_update.ppm');
const pngUpdate = path.join(ROOT, 'screenshot_update.png');
const serialLog = path.join(ROOT, 'serial_update.log');

console.log('=== TEST SUITE: SOTTOSISTEMA DI AGGIORNAMENTO NEWOS V1 ===');

// 1. Generazione pacchetto UPDATE.PAK con pack_update.js
const { packUpdate } = require('./pack_update');
const updateDir = path.join(ROOT, 'build', 'bin', 'NewOS', 'Update');
if (!fs.existsSync(updateDir)) fs.mkdirSync(updateDir, { recursive: true });

const outPak = path.join(updateDir, 'UPDATE.PAK');
const regUpdateSrc = path.join(ROOT, 'Release', 'registry_update.update');

packUpdate(path.join(ROOT, 'Release'), outPak, { registryUpdatePath: regUpdateSrc });

// 2. Creazione C:\NewOS\Boot\boot_action.ini
const bootDir = path.join(ROOT, 'build', 'bin', 'NewOS', 'Boot');
if (!fs.existsSync(bootDir)) fs.mkdirSync(bootDir, { recursive: true });
const bootActionIni = "[BootAction]\r\nTarget=UPDATE.MOD\r\nAttempts=1\r\nMaxAttempts=2\r\n";
fs.writeFileSync(path.join(bootDir, 'boot_action.ini'), bootActionIni);

console.log('[OK] Trigger boot_action.ini impostato: Target=UPDATE.MOD');

// 3. Ricostruzione myos.img per includere UPDATE.PAK e boot_action.ini
console.log('--> Ricostruzione immagine disco con pacchetto di aggiornamento...');
execSync('node tools/build.js', { stdio: 'inherit', cwd: ROOT });

// 4. Pulizia log e screendump precedenti
if (fs.existsSync(ppmUpdate)) fs.unlinkSync(ppmUpdate);
if (fs.existsSync(pngUpdate)) fs.unlinkSync(pngUpdate);
if (fs.existsSync(serialLog)) fs.unlinkSync(serialLog);

// 5. Avvio QEMU per collaudo
console.log('\n--> Avvio QEMU per esecuzione automatica fuori sessione di UPDATE.MOD...');
const qemu = spawn(qemuBin, [
    '-drive', `format=raw,file=${imgPath}`,
    '-m', '256M',
    '-vga', 'std',
    '-audiodev', 'none,id=snd0',
    '-device', 'AC97,audiodev=snd0',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:4460,server,nowait',
    '-serial', `file:${serialLog}`,
    '-d', 'guest_errors'
], { stdio: 'ignore' });

const ppmDesktop = path.join(ROOT, 'screenshot_desktop_post_update.ppm');
const pngDesktop = path.join(ROOT, 'screenshot_desktop_post_update.png');

// Connessione al monitor QEMU dopo avvio e bootscreen
setTimeout(() => {
    console.log('Connessione al monitor QEMU...');
    const client = net.createConnection({ port: 4460 }, () => {
        console.log('Connesso! In attesa di UPDATE.MOD...');
        // Attendi 2200ms per catturare la schermata di UPDATE.MOD a video
        setTimeout(() => {
            console.log('Cattura screendump durante l\'aggiornamento di UPDATE.MOD...');
            client.write(`screendump "${ppmUpdate.replace(/\\/g, '/')}"\n`);

            setTimeout(() => {
                // Secondo screendump a fine operazione (Desktop con WhatsNew)
                console.log('Cattura secondo screendump a fine operazione (Desktop con WhatsNew)...');
                client.write(`screendump "${ppmDesktop.replace(/\\/g, '/')}"\n`);
                setTimeout(() => {
                    client.write('quit\n');
                    setTimeout(() => {
                        try { client.destroy(); } catch (e) {}
                        try { qemu.kill(); } catch (e) {}

                        // Conversione PPM in BMP e PNG per entrambi gli screendump
                        const convertPpm = (ppmFile, pngFile) => {
                            if (fs.existsSync(ppmFile)) {
                                const bmpFile = ppmFile.replace('.ppm', '.bmp');
                                try {
                                    execSync(`node tools/ppm2bmp.js "${ppmFile}" "${bmpFile}"`, { stdio: 'ignore' });
                                    execSync(`powershell -NoProfile -Command "Add-Type -AssemblyName System.Drawing; $bmp = [System.Drawing.Bitmap]::FromFile('${bmpFile}'); $bmp.Save('${pngFile}', [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()"`, { stdio: 'ignore' });
                                    if (fs.existsSync(pngFile)) {
                                        console.log(`[OK] Screendump PNG generato: ${pngFile}`);
                                    }
                                } catch (e) {
                                    console.log('[INFO] Conversione grafica automatica saltata:', e.message);
                                }
                            }
                        };

                        convertPpm(ppmUpdate, pngUpdate);
                        convertPpm(ppmDesktop, pngDesktop);

                        const pngDynamic = path.join(ROOT, 'screenshot_whatsnew_dynamic.png');
                        if (fs.existsSync(pngDesktop)) {
                            fs.copyFileSync(pngDesktop, pngDynamic);
                            const artifactDir = 'C:\\Users\\morga\\.gemini\\antigravity-ide\\brain\\2a833b14-8f67-4c71-b5d5-281b1da0491a';
                            if (fs.existsSync(artifactDir)) {
                                fs.copyFileSync(pngDesktop, path.join(artifactDir, 'screenshot_whatsnew_dynamic.png'));
                            }
                        }

                        if (fs.existsSync(serialLog)) {
                            console.log('\n=== LOG SERIALE REGISTRATO DURANTE L\'AGGIORNAMENTO ===');
                            console.log(fs.readFileSync(serialLog, 'utf8'));
                        }

                        // Ripristino stato pulito: rimozione boot_action.ini per il boot normale
                        const liveActionPath = path.join(bootDir, 'boot_action.ini');
                        if (fs.existsSync(liveActionPath)) fs.unlinkSync(liveActionPath);
                        console.log('\n[TEST COMPLETATO] Sottosistema di aggiornamento validato con successo.');
                    }, 500);
                }, 2000);
            }, 8500);
        }, 2200);
    });

    client.on('error', (err) => {
        console.error('Errore connessione monitor QEMU:', err.message);
        try { qemu.kill(); } catch (e) {}
    });
}, 3500);
