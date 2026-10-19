const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..');
const ICON_DIR = path.join(ROOT, 'resources', 'icon');

// Elenco icone supportate (la cartella 'da definire' viene esplicitamente esclusa)
const ICON_LIST = [
    { id: 'ICON_THIS_PC',        file: 'thisPC.ico',           name: 'Questo PC' },
    { id: 'ICON_CESTINO',        file: 'cestino.ico',          name: 'Cestino' },
    { id: 'ICON_CESTINO_PIENO',  file: 'cestino-pieno.ico',    name: 'Cestino Pieno' },
    { id: 'ICON_FOLDER',         file: 'folder.ico',           name: 'Cartella' },
    { id: 'ICON_FOLDER_VOID',    file: 'folder-void.ico',      name: 'Cartella Vuota' },
    { id: 'ICON_DOCUMENT',       file: 'document.ico',         name: 'Documento' },
    { id: 'ICON_CONTROL_PANEL',  file: 'ControlPannel.ico',    name: 'Pannello Controllo' },
    { id: 'ICON_SOUND',          file: 'sound.ico',            name: 'Audio' },
    { id: 'ICON_RUN',            file: 'Run.ico',              name: 'Esegui' },
    { id: 'ICON_SEARCH',         file: 'Search.ico',           name: 'Cerca' },
    { id: 'ICON_DESKTOP',        file: 'Desktop.ico',          name: 'Desktop' },
    { id: 'ICON_HD',             file: 'HD.ico',               name: 'Disco Fisso' },
    { id: 'ICON_FLOPPY',         file: 'FloppyDrive.ico',      name: 'Floppy' },
    { id: 'ICON_CD',             file: 'CD.ico',               name: 'CD-ROM' },
    { id: 'ICON_NET',            file: 'net.ico',              name: 'Rete' },
    { id: 'ICON_INFO',           file: 'info.ico',             name: 'Info' },
    { id: 'ICON_KEYBOARD',       file: 'keyboard.ico',         name: 'Tastiera' },
    { id: 'ICON_MOUSE',          file: 'mouse.ico',            name: 'Mouse' },
    { id: 'ICON_REGISTRY',       file: 'registry.ico',         name: 'Registro' },
    { id: 'ICON_ACCESSIBILITY',  file: 'accessibility.ico',    name: 'Accessibilita' },
    { id: 'ICON_DATABASE',       file: 'database.ico',         name: 'Database' },
    { id: 'ICON_FONT',           file: 'font.ico',             name: 'Font' },
    { id: 'ICON_IMAGE',          file: 'image.ico',            name: 'Immagine' },
    { id: 'ICON_IMAGE_PNG',      file: 'image-png.ico',        name: 'Immagine PNG' },
    { id: 'ICON_PLAY',           file: 'play.ico',             name: 'Play' },
    { id: 'ICON_ENERGY',         file: 'EnergyManagement.ico', name: 'Energia' },
    { id: 'ICON_MAINTENANCE',    file: 'ManutenchTool.ico',    name: 'Manutenzione' },
    { id: 'ICON_NET_ON',         file: 'UnityNetOn.ico',       name: 'Rete Attiva' },
    { id: 'ICON_NET_OFF',        file: 'UnityNetOff.ico',      name: 'Rete Disattivata' }
];

console.log('=== Elaborazione Icone di Sistema (resources/icon) ===');
console.log(`[INFO] Totale icone definite: ${ICON_LIST.length} (esclusa cartella 'da definire')`);

function extractDIBPixels(filePath, targetW, targetH) {
    const buf = fs.readFileSync(filePath);
    const count = buf.readUInt16LE(4);
    let entry = null;
    for (let i = 0; i < count; i++) {
        const off = 6 + i * 16;
        const w = buf[off] || 256;
        const h = buf[off + 1] || 256;
        const bpp = buf.readUInt16LE(off + 6);
        const dataOff = buf.readUInt32LE(off + 12);
        if (w === targetW && h === targetH && bpp === 32) {
            entry = { dataOff };
            break;
        }
    }
    if (!entry) {
        throw new Error(`Impossibile trovare formato ${targetW}x${targetH} 32bpp in ${filePath}`);
    }

    const biSize = buf.readUInt32LE(entry.dataOff);
    const pixels = Buffer.alloc(targetW * targetH * 4);

    // I pixel in formato DIB sono memorizzati dal basso verso l'alto (riga targetH-1 prima)
    for (let y = 0; y < targetH; y++) {
        const srcRow = entry.dataOff + biSize + (targetH - 1 - y) * targetW * 4;
        const dstRow = (y * targetW) * 4;
        for (let x = 0; x < targetW; x++) {
            // Formato little-endian 32-bit: B, G, R, A -> 0xAARRGGBB
            pixels[dstRow + x * 4 + 0] = buf[srcRow + x * 4 + 0]; // B
            pixels[dstRow + x * 4 + 1] = buf[srcRow + x * 4 + 1]; // G
            pixels[dstRow + x * 4 + 2] = buf[srcRow + x * 4 + 2]; // R
            pixels[dstRow + x * 4 + 3] = buf[srcRow + x * 4 + 3]; // A
        }
    }
    return pixels;
}

// Struttura del file icons.bin:
// - Header: 2048 byte (4 settori)
//   Offset 0: Magic 'MICO' (0x4F43494D)
//   Offset 4: Version (1)
//   Offset 8: Count (29)
//   Offset 12: Entry size (32)
//   Offset 16..2047: Array di 29 voci (32 byte ciascuna):
//     char name[20]
//     uint32 offset32 (offset assoluto nel file)
//     uint32 offset16 (offset assoluto nel file)
//     uint32 flags
// - Dati: 29 * (32x32*4 + 16x16*4) = 29 * (4096 + 1024) = 148480 byte
const HEADER_SIZE = 2048;
const entryData = [];
let currentOffset = HEADER_SIZE;

const buffersToConcat = [];
const headerBuf = Buffer.alloc(HEADER_SIZE, 0);

headerBuf.write('MICO', 0, 'ascii');
headerBuf.writeUInt32LE(1, 4);
headerBuf.writeUInt32LE(ICON_LIST.length, 8);
headerBuf.writeUInt32LE(32, 12);

for (let i = 0; i < ICON_LIST.length; i++) {
    const item = ICON_LIST[i];
    const icoPath = path.join(ICON_DIR, item.file);

    if (!fs.existsSync(icoPath)) {
        console.error(`[ERRORE] File icona non trovato: ${icoPath}`);
        process.exit(1);
    }

    const p32 = extractDIBPixels(icoPath, 32, 32);
    const p16 = extractDIBPixels(icoPath, 16, 16);

    const off32 = currentOffset;
    buffersToConcat.push(p32);
    currentOffset += p32.length;

    const off16 = currentOffset;
    buffersToConcat.push(p16);
    currentOffset += p16.length;

    const entOff = 16 + i * 32;
    headerBuf.write(item.id.replace('ICON_', ''), entOff, 19, 'ascii');
    headerBuf.writeUInt32LE(off32, entOff + 20);
    headerBuf.writeUInt32LE(off16, entOff + 24);
    headerBuf.writeUInt32LE(0, entOff + 28);

    console.log(`  [OK] ${item.id.padEnd(20)} (${item.file}) -> 32x32 @ ${off32}, 16x16 @ ${off16}`);
}

// Allineamento a multiplo di 512 byte (settori disco)
const totalDataSize = currentOffset;
const sectorsNeeded = Math.ceil(totalDataSize / 512);
const finalBinSize = sectorsNeeded * 512;
const padding = Buffer.alloc(finalBinSize - totalDataSize, 0);

const iconsBin = Buffer.concat([headerBuf, ...buffersToConcat, padding]);
const outBinPath = path.join(ROOT, 'resources', 'icons.bin');
fs.writeFileSync(outBinPath, iconsBin);
console.log(`\n[OK] Generato ${outBinPath} (${iconsBin.length} byte, ${sectorsNeeded} settori disco)`);

// Generazione header C kernel/gui/icons.h
let hContent = `// Autogenerato da tools/process_icons.js - NON MODIFICARE A MANO
#ifndef GUI_ICONS_H
#define GUI_ICONS_H

#include <types.h>

typedef enum {
`;

for (let i = 0; i < ICON_LIST.length; i++) {
    hContent += `    ${ICON_LIST[i].id.padEnd(24)} = ${i},\n`;
}
hContent += `    NUM_ICONS                = ${ICON_LIST.length}
} icon_id_t;

// Inizializza il repository icone caricando il pacchetto dal disco (LBA 1024)
void icons_init(void);

// Restituisce il puntatore al buffer pixel ARGB 32-bit (32x32 = 1024 dword, 16x16 = 256 dword)
const uint32_t* icon_get_32(icon_id_t id);
const uint32_t* icon_get_16(icon_id_t id);

// Disegna un'icona con trasparenza alpha nativa (per-pixel alpha blending)
void gfx_draw_icon_32(int32_t x, int32_t y, icon_id_t id);
void gfx_draw_icon_16(int32_t x, int32_t y, icon_id_t id);

// Funzioni di rendering generico di sprite ARGB con canale alpha
void gfx_draw_sprite_32_alpha(int32_t x, int32_t y, const uint32_t* pixels);
void gfx_draw_sprite_16_alpha(int32_t x, int32_t y, const uint32_t* pixels);

#endif // GUI_ICONS_H
`;

const outHPath = path.join(ROOT, 'kernel', 'gui', 'icons.h');
fs.writeFileSync(outHPath, hContent);
console.log(`[OK] Generato ${outHPath}`);
