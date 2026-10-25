const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

// Master Key crittografica simmetrica a 256 bit (32 byte) per NewOS V1
const NEWOS_AES256_KEY = Buffer.from("NewOS-V1SETUP-256BIT-AUTHENTIC26", "utf-8");

function crc32Buffer(buf) {
    let crc = 0xFFFFFFFF;
    for (let i = 0; i < buf.length; i++) {
        crc ^= buf[i];
        for (let j = 0; j < 8; j++) {
            crc = (crc >>> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
        }
    }
    return (crc ^ 0xFFFFFFFF) >>> 0;
}

function getAllFiles(dir, baseDir = '') {
    let results = [];
    const list = fs.readdirSync(dir);
    for (const file of list) {
        const fnameUpper = file.toUpperCase();
        if (fnameUpper === 'SYSTEM.PAK' || fnameUpper === 'BOOTSETUP.MOD' || fnameUpper === 'BOOTSETUP.INI' ||
            fnameUpper === 'KERNEL.BIN' || fnameUpper === 'KERNEL.ELF' || fnameUpper === 'STAGE2.BIN' || fnameUpper === 'BOOTSECT.BIN' ||
            fnameUpper === 'BOOTX64.EFI' || fnameUpper === 'BOOTX64.BIN' ||
            fnameUpper.endsWith('.IMG') || fnameUpper.endsWith('.ISO') || fnameUpper.endsWith('.VDI')) continue;
        const relPath = baseDir ? `${baseDir}/${file}` : file;
        const relNormalized = relPath.replace(/\\/g, '/');

        // Il profilo Morgan deve essere mantenuto solo per la versione Live (escluso da SYSTEM.PAK)
        if (relNormalized === 'User/Morgan' || relNormalized.startsWith('User/Morgan/') ||
            relNormalized === 'User/MORGAN' || relNormalized.startsWith('User/MORGAN/')) {
            continue;
        }

        const fullPath = path.join(dir, file);
        const stat = fs.statSync(fullPath);
        if (stat.isDirectory()) {
            results = results.concat(getAllFiles(fullPath, relNormalized));
        } else {
            results.push({ fullPath, relPath: relNormalized, size: stat.size });
        }
    }
    return results;
}

function packSystem(binDir, outPakPath) {
    console.log(`[PACK_SYSTEM] Generazione archivio cifrato AES-256 da: ${binDir}`);
    const files = getAllFiles(binDir);
    console.log(`[PACK_SYSTEM] Rilevati ${files.length} file da cifrare e impacchettare.`);

    // Calcola dimensione payload decifrato
    let uncompressedTotal = 0;
    for (const f of files) {
        uncompressedTotal += (2 + Buffer.byteLength(f.relPath, 'utf-8') + 4 + 4 + f.size);
    }

    // Alloca buffer per i record di tutti i file
    const plainBuf = Buffer.alloc(uncompressedTotal);
    let offset = 0;

    for (const f of files) {
        const fileData = fs.readFileSync(f.fullPath);
        const pathBuf = Buffer.from(f.relPath, 'utf-8');
        const fileCrc = crc32Buffer(fileData);

        // Record header: path_len (uint16), path, size (uint32), crc32 (uint32)
        plainBuf.writeUInt16LE(pathBuf.length, offset);
        offset += 2;

        pathBuf.copy(plainBuf, offset);
        offset += pathBuf.length;

        plainBuf.writeUInt32LE(fileData.length, offset);
        offset += 4;

        plainBuf.writeUInt32LE(fileCrc, offset);
        offset += 4;

        fileData.copy(plainBuf, offset);
        offset += fileData.length;
    }

    // Padding a 16 byte per cifratura a blocchi AES (PKCS#7)
    const padLen = 16 - (plainBuf.length % 16);
    const padBuf = Buffer.alloc(padLen, padLen);
    const fullPlain = Buffer.concat([plainBuf, padBuf]);

    // Genera IV crittografico a 128 bit (16 byte)
    const iv = crypto.randomBytes(16);

    // Cifratura simmetrica AES-256-CBC
    const cipher = crypto.createCipheriv('aes-256-cbc', NEWOS_AES256_KEY, iv);
    cipher.setAutoPadding(false); // Padding gestito manualmente
    const encryptedPayload = Buffer.concat([cipher.update(fullPlain), cipher.final()]);
    const payloadCrc = crc32Buffer(encryptedPayload);

    // Header del contenitore protetto NOSPAK10 (64 byte)
    const headerBuf = Buffer.alloc(64, 0);
    headerBuf.write("NOSPAK10", 0, 8, 'ascii');      // Magic 8 byte
    headerBuf.writeUInt32LE(64, 8);                   // header_size
    headerBuf.writeUInt32LE(files.length, 12);        // total_files
    headerBuf.writeUInt32LE(uncompressedTotal, 16);   // total_uncompressed
    headerBuf.writeUInt32LE(encryptedPayload.length, 20); // encrypted_size
    iv.copy(headerBuf, 24, 0, 16);                    // aes_iv (16 byte, offset 24..39)
    headerBuf.writeUInt32LE(payloadCrc, 40);          // checksum_crc32 del payload cifrato
    // bytes 44..63 reserved/padding a 0

    // Concatena Header + Payload cifrato
    const finalPak = Buffer.concat([headerBuf, encryptedPayload]);

    const outDir = path.dirname(outPakPath);
    if (!fs.existsSync(outDir)) fs.mkdirSync(outDir, { recursive: true });
    fs.writeFileSync(outPakPath, finalPak);

    console.log(`[PACK_SYSTEM OK] Archivio cifrato generato: ${outPakPath}`);
    console.log(`  -> File contenuti: ${files.length}`);
    console.log(`  -> Dimensione non compressa: ${uncompressedTotal} byte (${(uncompressedTotal/1024/1024).toFixed(2)} MB)`);
    console.log(`  -> Dimensione cifrata AES-256: ${finalPak.length} byte (${(finalPak.length/1024/1024).toFixed(2)} MB)`);
    console.log(`  -> Checksum payload CRC32: 0x${payloadCrc.toString(16).toUpperCase()}`);
    return finalPak.length;
}

module.exports = { packSystem, NEWOS_AES256_KEY };

if (require.main === module) {
    const rootDir = path.resolve(__dirname, '..');
    const binDir = path.join(rootDir, 'build', 'bin');
    const pakOut = path.join(rootDir, 'build', 'SYSTEM.PAK');
    packSystem(binDir, pakOut);
}
