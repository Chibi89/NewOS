const fs = require('fs');
const path = require('path');

const ppmPath = path.join(__dirname, '..', 'screenshot.ppm');
const data = fs.readFileSync(ppmPath);

// Header offset: 16
const offset = 16;
const width = 1024;

function getPixel(x, y) {
    const idx = offset + (y * width + x) * 3;
    return {
        r: data[idx],
        g: data[idx + 1],
        b: data[idx + 2],
        hex: '#' + [data[idx], data[idx + 1], data[idx + 2]].map(v => v.toString(16).padStart(2, '0')).join('')
    };
}

console.log('--- Analisi Pixel Schermo MyOS ---');
console.log('Desktop Teal (x=600, y=100):', getPixel(600, 100));
console.log('Taskbar Grigio (x=100, y=755):', getPixel(100, 755));
console.log('Finestra Command Prompt (x=120, y=100):', getPixel(120, 100));
console.log('Titolo Finestra Gradiente (x=120, y=68):', getPixel(120, 68));
console.log('Pulsante Start (x=30, y=755):', getPixel(30, 755));

// Conta colori unici per confermare la ricchezza grafica
const colorSet = new Set();
for (let i = offset; i < data.length; i += 3) {
    const key = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
    colorSet.add(key);
}
console.log('Totale colori unici renderizzati:', colorSet.size);
