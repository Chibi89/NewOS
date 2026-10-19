const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..');
const startupWav = path.join(ROOT, 'resources', 'sounds', 'Windows Startup.wav');
const dingWav    = path.join(ROOT, 'resources', 'sounds', 'ding.wav');
const outC       = path.join(ROOT, 'kernel', 'drivers', 'sound_data.c');
const outH       = path.join(ROOT, 'kernel', 'drivers', 'sound_data.h');

function extractPcm(wavPath) {
    const buf = fs.readFileSync(wavPath);
    let offset = 12;
    while (offset < buf.length - 8) {
        const id = buf.toString('ascii', offset, offset + 4);
        const sz = buf.readUInt32LE(offset + 4);
        if (id === 'data') {
            return buf.subarray(offset + 8, offset + 8 + sz);
        }
        offset += 8 + sz;
    }
    throw new Error(`Chunk data non trovato in ${wavPath}`);
}

const startupPcm = extractPcm(startupWav);
const dingPcm    = extractPcm(dingWav);

console.log(`Startup PCM: ${startupPcm.length} byte`);
console.log(`Ding PCM:    ${dingPcm.length} byte`);

// Header
let hContent = `#ifndef DRIVERS_SOUND_DATA_H
#define DRIVERS_SOUND_DATA_H

#include <types.h>

extern const uint8_t sound_startup_pcm[${startupPcm.length}];
#define SOUND_STARTUP_SIZE ${startupPcm.length}

extern const uint8_t sound_ding_pcm[${dingPcm.length}];
#define SOUND_DING_SIZE ${dingPcm.length}

#endif // DRIVERS_SOUND_DATA_H
`;
fs.writeFileSync(outH, hContent);

// C file
let cStream = fs.createWriteStream(outC);
cStream.write('#include <drivers/sound_data.h>\n\n');

function writeArray(name, buf) {
    cStream.write(`const uint8_t ${name}[${buf.length}] __attribute__((aligned(16))) = {\n`);
    for (let i = 0; i < buf.length; i += 16) {
        let chunk = [];
        for (let j = 0; j < 16 && (i + j) < buf.length; j++) {
            chunk.push('0x' + buf[i + j].toString(16).padStart(2, '0'));
        }
        cStream.write('    ' + chunk.join(', ') + ',\n');
    }
    cStream.write('};\n\n');
}

writeArray('sound_startup_pcm', startupPcm);
writeArray('sound_ding_pcm', dingPcm);
cStream.end(() => {
    console.log(`[OK] Generati con successo ${outH} e ${outC}`);
});
