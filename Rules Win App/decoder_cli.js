/**
 * decoder_cli.js - Utilità a riga di comando per decodificare e verificare file .NOR
 * Uso:
 *   node decoder_cli.js <file.NOR>
 *   node decoder_cli.js --all
 */

const fs = require('fs');
const path = require('path');

const CIPHER_KEY = Buffer.from('NewSoft-NewOS-RuleSecurityKey-26', 'ascii');

function calcCrc32(buf) {
    let crc = 0xFFFFFFFF;
    for (let i = 0; i < buf.length; i++) {
        crc ^= buf[i];
        for (let j = 0; j < 8; j++) {
            crc = (crc >>> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
        }
    }
    return (crc ^ 0xFFFFFFFF) >>> 0;
}

function decodeNorFile(filePath) {
    if (!fs.existsSync(filePath)) {
        console.error(`[ERRORE] File non trovato: ${filePath}`);
        return null;
    }

    const data = fs.readFileSync(filePath);
    if (data.length < 128) {
        console.error(`[ERRORE] File troppo piccolo per intestazione .NOR (128 byte richiesti).`);
        return null;
    }

    const magic = data.toString('ascii', 0, 4);
    if (magic !== 'NOSR') {
        console.error(`[ERRORE] Magic header non valido: '${magic}' invece di 'NOSR'.`);
        return null;
    }

    const version = data.readUInt32LE(4);
    const headerSize = data.readUInt32LE(8);
    const flags = data.readUInt32LE(12);
    const ruleId = data.toString('ascii', 16, 48).replace(/\0+$/, '');
    const category = data.toString('ascii', 48, 80).replace(/\0+$/, '');
    const severity = data.toString('ascii', 80, 96).replace(/\0+$/, '');
    const authTag = data.toString('ascii', 96, 112).replace(/\0+$/, '');
    const timestamp = data.readUInt32LE(112);
    const payloadCrc32 = data.readUInt32LE(116);
    const payloadSize = data.readUInt32LE(120);

    const cipherPayload = data.slice(128, 128 + payloadSize);
    const plainPayload = Buffer.alloc(payloadSize);

    for (let i = 0; i < payloadSize; i++) {
        const k = CIPHER_KEY[i % 32];
        const mix = (i * 11 + 0x6A) & 0xFF;
        plainPayload[i] = cipherPayload[i] ^ k ^ mix;
    }

    const calcCrc = calcCrc32(plainPayload);
    const isVerified = (calcCrc === payloadCrc32);

    return {
        filePath,
        magic,
        version: '0x' + version.toString(16).toUpperCase(),
        flags,
        ruleId,
        category,
        severity,
        authTag,
        timestamp,
        payloadCrc32: '0x' + payloadCrc32.toString(16).toUpperCase().padStart(8, '0'),
        calcCrc32: '0x' + calcCrc.toString(16).toUpperCase().padStart(8, '0'),
        payloadSize,
        isVerified,
        text: plainPayload.toString('utf8')
    };
}

const arg = process.argv[2];

if (!arg || arg === '--help' || arg === '-h') {
    console.log(`
================================================================================
NewOS V1 - Decodificatore CLI Regole Proprietarie .NOR
================================================================================
Uso:
  node decoder_cli.js <percorso_file.nor>     Decodifica e visualizza il file
  node decoder_cli.js --all                   Decodifica tutte le regole standard
  node decoder_cli.js --export <cartella>     Esporta tutte le regole in formato .TXT
`);
    process.exit(0);
}

if (arg === '--all') {
    const rulesDir = path.join(__dirname, 'rules');
    const targetDir = fs.existsSync(rulesDir) ? rulesDir : path.join(__dirname, '..', 'build', 'bin', 'NewOS', 'Rules');
    if (!fs.existsSync(targetDir)) {
        console.error(`[ERRORE] Cartella regole non trovata: ${targetDir}`);
        process.exit(1);
    }
    const files = fs.readdirSync(targetDir).filter(f => f.toUpperCase().endsWith('.NOR')).sort();
    console.log(`\nTrovate ${files.length} regole in ${targetDir}:\n`);
    for (const f of files) {
        const res = decodeNorFile(path.join(targetDir, f));
        if (res) {
            console.log(`[${res.isVerified ? 'VERIFICATO' : 'ALTERATO'}] ${f} | ID: ${res.ruleId} | Categoria: ${res.category} | Severita: ${res.severity} | CRC: ${res.calcCrc32}`);
        }
    }
    process.exit(0);
}

const res = decodeNorFile(path.resolve(arg));
if (res) {
    console.log(`\n================================================================================`);
    console.log(`REGOLA NEWOS: ${res.ruleId}`);
    console.log(`Categoria : ${res.category}`);
    console.log(`Severita' : ${res.severity}`);
    console.log(`Magic     : ${res.magic} (Header 128 byte) | Versione: ${res.version}`);
    console.log(`Integrita': ${res.isVerified ? 'VERIFICATA (CRC32 MATCH)' : 'ERRORE CRC32'}`);
    console.log(`CRC32     : Dichiarato ${res.payloadCrc32} | Calcolato ${res.calcCrc32}`);
    console.log(`Dimensione: ${res.payloadSize} byte`);
    console.log(`================================================================================\n`);
    console.log(res.text);
}
