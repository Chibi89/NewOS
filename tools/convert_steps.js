const fs = require('fs');
const path = require('path');
const { execSync } = require('child_process');

const ROOT = path.resolve(__dirname, '..');
const brainDir = 'C:\\Users\\morga\\.gemini\\antigravity-ide\\brain\\201a47cf-7636-4592-93f8-cb22ec54e1bd';

for (let i = 0; i <= 5; i++) {
    const ppm = path.join(ROOT, `screenshot_step${i}.ppm`);
    const bmp = path.join(ROOT, `screenshot_step${i}.bmp`);
    const png = path.join(brainDir, `screenshot_step${i}.png`);

    if (fs.existsSync(ppm)) {
        console.log(`Conversione Step ${i}...`);
        execSync(`node "${path.join(ROOT, 'tools', 'ppm2bmp.js')}" "${ppm}" "${bmp}"`, { stdio: 'inherit' });
        execSync(`powershell -ExecutionPolicy Bypass -File "${path.join(ROOT, 'tools', 'bmp2png.ps1')}" "${bmp}" "${png}"`, { stdio: 'inherit' });
    }
}
console.log('Tutti gli screenshot convertiti!');
