const fs = require('fs');
const path = require('path');
const { execSync } = require('child_process');

const ROOT = path.resolve(__dirname, '..');
const brainDir = 'C:\\Users\\morga\\.gemini\\antigravity-ide\\brain\\f8b9e865-1f04-4340-9815-41144a7b521d';

const targets = [
    { ppm: 'screenshot_login_oobe0.ppm', bmp: 'screenshot_login_oobe0.bmp', png: 'oobe_login_step0.png' },
    { ppm: 'screenshot_login_oobe1.ppm', bmp: 'screenshot_login_oobe1.bmp', png: 'oobe_login_step1.png' },
    { ppm: 'screenshot_login_oobe2.ppm', bmp: 'screenshot_login_oobe2.bmp', png: 'oobe_login_step2.png' },
    { ppm: 'screenshot_login_oobe3.ppm', bmp: 'screenshot_login_oobe3.bmp', png: 'oobe_login_step3.png' },
    { ppm: 'screenshot_login_oobe4.ppm', bmp: 'screenshot_login_oobe4.bmp', png: 'oobe_login_step4.png' },
    { ppm: 'screenshot_login_oobe5.ppm', bmp: 'screenshot_login_oobe5.bmp', png: 'oobe_login_step5.png' },
    { ppm: 'screenshot_desktop_after_oobe.ppm', bmp: 'screenshot_desktop_after_oobe.bmp', png: 'desktop_after_oobe.png' }
];

for (const t of targets) {
    const srcPpm = path.join(ROOT, t.ppm);
    const dstBmp = path.join(ROOT, t.bmp);
    const dstPng = path.join(brainDir, t.png);

    if (fs.existsSync(srcPpm)) {
        console.log(`Conversione ${t.ppm} -> ${t.png}...`);
        execSync(`node "${path.join(ROOT, 'tools', 'ppm2bmp.js')}" "${srcPpm}" "${dstBmp}"`, { stdio: 'inherit' });
        execSync(`powershell -ExecutionPolicy Bypass -File "${path.join(ROOT, 'tools', 'bmp2png.ps1')}" "${dstBmp}" "${dstPng}"`, { stdio: 'inherit' });
    }
}
console.log('Tutti gli screenshot convertiti in PNG nella cartella brain!');
