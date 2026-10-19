const fs = require('fs');
const path = require('path');

const sounds = ['Windows Startup.wav', 'ding.wav', 'tada.wav'];

for (const name of sounds) {
    const p = path.join(__dirname, '..', 'resources', 'sounds', name);
    if (!fs.existsSync(p)) {
        console.log(`[NON TROVATO] ${name}`);
        continue;
    }
    const buf = fs.readFileSync(p);
    const riff = buf.toString('ascii', 0, 4);
    const wave = buf.toString('ascii', 8, 12);
    const channels = buf.readUInt16LE(22);
    const sampleRate = buf.readUInt32LE(24);
    const bitsPerSample = buf.readUInt16LE(34);
    
    // Cerca chunk "data"
    let dataOffset = 12;
    while (dataOffset < buf.length - 8) {
        const id = buf.toString('ascii', dataOffset, dataOffset + 4);
        const sz = buf.readUInt32LE(dataOffset + 4);
        if (id === 'data') {
            console.log(`[OK] ${name}: ${channels} canali, ${sampleRate} Hz, ${bitsPerSample} bit, data size: ${sz} bytes (offset ${dataOffset + 8})`);
            break;
        }
        dataOffset += 8 + sz;
    }
}
