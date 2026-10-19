const fs = require('fs');
const path = require('path');

const ppmPath = process.argv[2] ? path.resolve(process.argv[2]) : path.join(__dirname, '..', 'screenshot.ppm');
const bmpPath = process.argv[3] ? path.resolve(process.argv[3]) : path.join(__dirname, '..', 'screenshot.bmp');

if (!fs.existsSync(ppmPath)) {
    console.error('PPM non trovato');
    process.exit(1);
}

const data = fs.readFileSync(ppmPath);

// Parsing header PPM P6
let offset = 0;
function readToken() {
    while (offset < data.length && (data[offset] === 32 || data[offset] === 10 || data[offset] === 13 || data[offset] === 9)) {
        offset++;
    }
    if (offset >= data.length) return null;
    let start = offset;
    while (offset < data.length && data[offset] !== 32 && data[offset] !== 10 && data[offset] !== 13 && data[offset] !== 9) {
        offset++;
    }
    return data.toString('ascii', start, offset);
}

const magic = readToken();
const width = parseInt(readToken(), 10);
const height = parseInt(readToken(), 10);
const maxVal = parseInt(readToken(), 10);
offset++; // Salta il newline dopo maxVal

console.log(`PPM Info: ${magic}, ${width}x${height}, max: ${maxVal}, data offset: ${offset}`);

// Creazione BMP (24bpp)
const rowSize = Math.floor((24 * width + 31) / 32) * 4;
const pixelArraySize = rowSize * height;
const fileSize = 54 + pixelArraySize;

const bmp = Buffer.alloc(fileSize);

// BMP Header (14 bytes)
bmp.write('BM', 0);
bmp.writeUInt32LE(fileSize, 2);
bmp.writeUInt32LE(0, 6);
bmp.writeUInt32LE(54, 10); // Offset to pixel array

// DIB Header (BITMAPINFOHEADER 40 bytes)
bmp.writeUInt32LE(40, 14);
bmp.writeInt32LE(width, 18);
bmp.writeInt32LE(height, 22); // Dal basso verso l'alto
bmp.writeUInt16LE(1, 26);     // Piani
bmp.writeUInt16LE(24, 28);    // 24 bpp
bmp.writeUInt32LE(0, 30);     // BI_RGB (nessuna compressione)
bmp.writeUInt32LE(pixelArraySize, 34);
bmp.writeInt32LE(2835, 38);
bmp.writeInt32LE(2835, 42);
bmp.writeUInt32LE(0, 46);
bmp.writeUInt32LE(0, 50);

// In BMP le righe vanno dal basso verso l'alto (riga height-1 prima) e i colori sono BGR
for (let y = 0; y < height; y++) {
    const srcY = (height - 1 - y); // inverti Y
    const srcRowOffset = offset + (srcY * width * 3);
    const dstRowOffset = 54 + (y * rowSize);

    for (let x = 0; x < width; x++) {
        const r = data[srcRowOffset + x * 3];
        const g = data[srcRowOffset + x * 3 + 1];
        const b = data[srcRowOffset + x * 3 + 2];

        bmp[dstRowOffset + x * 3]     = b; // Blue
        bmp[dstRowOffset + x * 3 + 1] = g; // Green
        bmp[dstRowOffset + x * 3 + 2] = r; // Red
    }
}

fs.writeFileSync(bmpPath, bmp);
console.log(`[OK] Convertito con successo in BMP: ${bmpPath} (${fileSize} byte)`);
