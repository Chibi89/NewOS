const { spawn } = require('child_process');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const qemuBin = path.join(ROOT, 'tools', 'qemu-system-x86_64.exe');
const isoPath = path.join(ROOT, 'build', 'myos.iso');
const serialLog = path.join(ROOT, 'serial_iso.log');

if (fs.existsSync(serialLog)) fs.unlinkSync(serialLog);

console.log('Avvio QEMU con -cdrom myos.iso...');
const qemu = spawn(qemuBin, [
    '-cdrom', isoPath,
    '-m', '256M',
    '-serial', `file:${serialLog}`,
    '-display', 'none'
], { stdio: 'ignore' });

setTimeout(() => {
    qemu.kill();
    let content = '';
    if (fs.existsSync(serialLog)) {
        content = fs.readFileSync(serialLog, 'utf8');
    }
    console.log('Log seriale ottenuto:', JSON.stringify(content));
    process.exit(0);
}, 3000);
