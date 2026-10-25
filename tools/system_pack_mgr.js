/**
 * NewOS V1 — System Package Manager & Security Integrity Audit Tool
 * 
 * Gestione, estrazione, verifica crittografica anti-manomissione (anti-tamper/anti-bruteforce),
 * monitoraggio in tempo reale (watch) e re-impacchettamento di SYSTEM.PAK (AES-256-CBC NOSPAK10).
 */

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const http = require('http');
const { exec } = require('child_process');

// Master Key crittografica simmetrica a 256 bit (32 byte) per NewOS V1
const NEWOS_AES256_KEY = Buffer.from("NewOS-V1SETUP-256BIT-AUTHENTIC26", "utf-8");
const PAK_MAGIC = "NOSPAK10";

// Calcolo CRC32 standard IEEE 802.3
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

// Calcolo SHA-256
function sha256Buffer(buf) {
    return crypto.createHash('sha256').update(buf).digest('hex');
}

// Calcolo SHA-256 per file su disco
function sha256File(filePath) {
    const data = fs.readFileSync(filePath);
    return sha256Buffer(data);
}

// Scansione ricorsiva di una cartella per ottenere i file da impacchettare
function getFilesRecursive(dir, baseDir = '') {
    let results = [];
    if (!fs.existsSync(dir)) return results;
    const list = fs.readdirSync(dir);
    for (const item of list) {
        // Ignora file di gestione, manifest, temporanei o archivi stessi
        if (item === 'SYSTEM.PAK' || item === '.pack_manifest.json' || item.endsWith('.bat') ||
            item === '.git' || item === 'node_modules' || item.endsWith('.tmp') || item.startsWith('.')) {
            continue;
        }

        const relPath = baseDir ? `${baseDir}/${item}` : item;
        const relNormalized = relPath.replace(/\\/g, '/');

        // Il profilo Morgan deve essere mantenuto solo per la versione Live
        if (relNormalized === 'User/Morgan' || relNormalized.startsWith('User/Morgan/') ||
            relNormalized === 'User/MORGAN' || relNormalized.startsWith('User/MORGAN/')) {
            console.log(`[INFO] Esclusione profilo Live: ${relNormalized}`);
            continue;
        }

        const fullPath = path.join(dir, item);
        const stat = fs.statSync(fullPath);
        if (stat.isDirectory()) {
            results = results.concat(getFilesRecursive(fullPath, relNormalized));
        } else {
            results.push({ fullPath, relPath: relNormalized, size: stat.size, mtime: stat.mtimeMs });
        }
    }
    return results;
}

/**
 * ESTRAZIONE / SCOMPATTAMENTO (Unpack)
 * Decifra SYSTEM.PAK, verifica i checksum crittografici e salva i file in destDir
 */
function unpackPackage(pakFilePath, destDir) {
    console.log(`\n===============================================================`);
    console.log(`  NewOS V1 — Estrazione e Decifratura di SYSTEM.PAK`);
    console.log(`===============================================================`);
    console.log(`[PAK IN]   ${pakFilePath}`);
    console.log(`[DEST DIR] ${destDir}\n`);

    if (!fs.existsSync(pakFilePath)) {
        throw new Error(`File del pacchetto non trovato: ${pakFilePath}`);
    }

    const fileBuf = fs.readFileSync(pakFilePath);
    if (fileBuf.length < 64) {
        throw new Error(`File pacchetto corrotto: dimensione inferiore all'header minimo (64 byte).`);
    }

    // Lettura e validazione Header NOSPAK10 (64 byte)
    const magic = fileBuf.toString('ascii', 0, 8);
    if (magic !== PAK_MAGIC) {
        throw new Error(`Magic Header non valido: '${magic}'. Previsto '${PAK_MAGIC}'. Possibile file manomesso o non autentico!`);
    }

    const headerSize = fileBuf.readUInt32LE(8);
    const totalFiles = fileBuf.readUInt32LE(12);
    const totalUncompressed = fileBuf.readUInt32LE(16);
    const encryptedSize = fileBuf.readUInt32LE(20);
    const iv = fileBuf.slice(24, 40);
    const expectedCrc32 = fileBuf.readUInt32LE(40);

    console.log(`[HEADER OK] Magic: ${magic} | Header Size: ${headerSize} B`);
    console.log(`[STATISTICHE] File dichiarati: ${totalFiles} | Dimensione decompressa: ${(totalUncompressed / 1024 / 1024).toFixed(2)} MB`);
    console.log(`[CIFRATURA] Algoritmo: AES-256-CBC | Dimensione Cifrata: ${encryptedSize} B`);
    console.log(`[IV CRITTOGRAFICO] 0x${iv.toString('hex').toUpperCase()}`);

    if (encryptedSize % 16 !== 0) {
        throw new Error(`Dimensione cifrata (${encryptedSize}) non multipla di 16 byte. Violazione standard AES-CBC!`);
    }

    if (fileBuf.length < 64 + encryptedSize) {
        throw new Error(`Dimensione del file (${fileBuf.length}) inferiore a quanto dichiarato nell'header (${64 + encryptedSize}). File troncato!`);
    }

    const encryptedPayload = fileBuf.slice(64, 64 + encryptedSize);

    // CONTROLLO ANTI-BRUTEFORCE & ANTI-TAMPERING SUL CIPHERTEXT
    console.log(`\n--> [AUDIT] Verifica checksum di integrità del payload cifrato...`);
    const actualCrc32 = crc32Buffer(encryptedPayload);
    if (actualCrc32 !== expectedCrc32) {
        throw new Error(`MANOMISSIONE CIPHERTEXT RILEVATA!\n` +
            `CRC32 calcolato: 0x${actualCrc32.toString(16).toUpperCase()} != atteso: 0x${expectedCrc32.toString(16).toUpperCase()}.\n` +
            `Il file cifrato e' stato alterato, corrotto o sottoposto a manipolazione bit-flip!`);
    }
    console.log(`[OK] Checksum CRC32 del ciphertext valido: 0x${actualCrc32.toString(16).toUpperCase()}`);

    // Decifratura AES-256-CBC
    console.log(`--> [AES-256] Decifratura simmetrica con Master Key...`);
    let decryptedBuf;
    try {
        const decipher = crypto.createDecipheriv('aes-256-cbc', NEWOS_AES256_KEY, iv);
        decipher.setAutoPadding(false);
        decryptedBuf = Buffer.concat([decipher.update(encryptedPayload), decipher.final()]);
    } catch (e) {
        throw new Error(`FALLIMENTO DECIFRATURA AES: la chiave o i blocchi crittografici sono invalidi (${e.message}). Possibile bruteforce non riuscito!`);
    }

    console.log(`[OK] Decifratura completata con successo (${decryptedBuf.length} byte).`);

    // Parsing dei record e scrittura file
    if (!fs.existsSync(destDir)) fs.mkdirSync(destDir, { recursive: true });

    let offset = 0;
    let extractedCount = 0;
    const manifestEntries = [];

    console.log(`--> Estrazione dei file e calcolo impronte digitali (CRC32 & SHA-256)...`);

    while (offset < totalUncompressed && extractedCount < totalFiles) {
        if (offset + 2 > decryptedBuf.length) break;
        const pathLen = decryptedBuf.readUInt16LE(offset);
        offset += 2;

        if (offset + pathLen > decryptedBuf.length) {
            throw new Error(`Struttura record corrotta all'offset ${offset} (path_len ${pathLen} eccede il buffer).`);
        }
        const relPath = decryptedBuf.toString('utf-8', offset, offset + pathLen);
        offset += pathLen;

        const fileSize = decryptedBuf.readUInt32LE(offset);
        offset += 4;

        const recordCrc32 = decryptedBuf.readUInt32LE(offset);
        offset += 4;

        if (offset + fileSize > decryptedBuf.length) {
            throw new Error(`Dimensione file ${relPath} (${fileSize} B) eccede la fine del payload decifrato.`);
        }
        const fileData = decryptedBuf.slice(offset, offset + fileSize);
        offset += fileSize;

        // Verifica integrità per-file
        const fileActualCrc = crc32Buffer(fileData);
        if (fileActualCrc !== recordCrc32) {
            console.error(`[ALLARME MANOMISSIONE] File corrotto: ${relPath} (CRC 0x${fileActualCrc.toString(16).toUpperCase()} != atteso 0x${recordCrc32.toString(16).toUpperCase()})`);
        }

        const fileSha256 = sha256Buffer(fileData);

        // Scrittura file
        const targetPath = path.join(destDir, relPath);
        const parentDir = path.dirname(targetPath);
        if (!fs.existsSync(parentDir)) fs.mkdirSync(parentDir, { recursive: true });
        fs.writeFileSync(targetPath, fileData);

        manifestEntries.push({
            path: relPath.replace(/\\/g, '/'),
            size: fileSize,
            crc32: `0x${recordCrc32.toString(16).toUpperCase().padStart(8, '0')}`,
            sha256: fileSha256,
            status: (fileActualCrc === recordCrc32) ? 'AUTHENTIC' : 'CORRUPTED'
        });

        extractedCount++;
        if (extractedCount % 30 === 0 || extractedCount === totalFiles) {
            console.log(`  -> Estratti ${extractedCount}/${totalFiles} file... (${relPath})`);
        }
    }

    // Salvataggio del Manifest di sicurezza
    const manifestPath = path.join(destDir, '.pack_manifest.json');
    const manifestData = {
        package_info: {
            magic: PAK_MAGIC,
            total_files: extractedCount,
            total_uncompressed_bytes: totalUncompressed,
            encrypted_payload_crc32: `0x${actualCrc32.toString(16).toUpperCase().padStart(8, '0')}`,
            timestamp: new Date().toISOString(),
            extracted_from: path.resolve(pakFilePath)
        },
        files: manifestEntries
    };
    fs.writeFileSync(manifestPath, JSON.stringify(manifestData, null, 2));

    console.log(`\n[ESTRAZIONE COMPLETATA]`);
    console.log(`  File estratti: ${extractedCount} in: ${destDir}`);
    console.log(`  Manifest di sicurezza salvato in: ${manifestPath}\n`);
    return manifestData;
}

/**
 * AUDIT DI INTEGRITA' & RILEVAMENTO MANOMISSIONI (Verify / Checksum)
 * Verifica lo stato di ogni file nella cartella rispetto al manifest originale
 */
function verifyIntegrity(workDir, pakFilePath = null) {
    console.log(`\n===============================================================`);
    console.log(`  NewOS V1 — Audit di Sicurezza & Rilevamento Manomissioni`);
    console.log(`===============================================================`);
    console.log(`[WATCH DIR] ${workDir}\n`);

    const report = {
        timestamp: new Date().toISOString(),
        total_disk_files: 0,
        authentic_count: 0,
        modified_count: 0,
        tampered_count: 0,
        new_count: 0,
        missing_count: 0,
        files: []
    };

    // 1. Audit del file .PAK se specificato
    if (pakFilePath && fs.existsSync(pakFilePath)) {
        console.log(`[AUDIT PAK] Analisi binaria di ${pakFilePath}...`);
        const fileBuf = fs.readFileSync(pakFilePath);
        if (fileBuf.length < 64) {
            console.log(`  [CRITICO] Il file PAK e' inferiore all'header minimo.`);
        } else {
            const magic = fileBuf.toString('ascii', 0, 8);
            const totalFiles = fileBuf.readUInt32LE(12);
            const encryptedSize = fileBuf.readUInt32LE(20);
            const iv = fileBuf.slice(24, 40);
            const expectedCrc = fileBuf.readUInt32LE(40);
            const payload = fileBuf.slice(64, 64 + encryptedSize);
            const actualCrc = crc32Buffer(payload);

            const isCrcOk = (actualCrc === expectedCrc);
            console.log(`  Magic: ${magic} (${magic === PAK_MAGIC ? 'OK' : 'MANOMESSO'})`);
            console.log(`  File dichiarati: ${totalFiles}`);
            console.log(`  CRC32 Ciphertext: 0x${actualCrc.toString(16).toUpperCase()} (Atteso: 0x${expectedCrc.toString(16).toUpperCase()}) -> ${isCrcOk ? 'INTEGRO' : 'MANOMESSO / BRUTEFORCE DETECTED'}`);
        }
        console.log(``);
    }

    // 2. Controllo dei file su disco rispetto al manifest
    const manifestPath = path.join(workDir, '.pack_manifest.json');
    let manifestMap = new Map();

    if (fs.existsSync(manifestPath)) {
        try {
            const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf-8'));
            for (const f of manifest.files) {
                manifestMap.set(f.path.toLowerCase(), f);
            }
            console.log(`[MANIFEST] Caricato manifest originale con ${manifest.files.length} record firmati.`);
        } catch (e) {
            console.warn(`[ATTENZIONE] Impossibile leggere .pack_manifest.json: ${e.message}`);
        }
    } else {
        console.log(`[NOTA] Nessun .pack_manifest.json trovato. Verra' eseguita la scansione calcolando nuovi checksum.`);
    }

    // Scansione di tutti i file attuali
    const currentFiles = getFilesRecursive(workDir);
    report.total_disk_files = currentFiles.length;
    const seenInDisk = new Set();

    for (const f of currentFiles) {
        const relNorm = f.relPath.replace(/\\/g, '/');
        const key = relNorm.toLowerCase();
        seenInDisk.add(key);

        const fileData = fs.readFileSync(f.fullPath);
        const actualCrc32 = `0x${crc32Buffer(fileData).toString(16).toUpperCase().padStart(8, '0')}`;
        const actualSha256 = sha256Buffer(fileData);

        let status = 'NEW';
        let detail = 'Nuovo file (non presente nel manifest originale)';

        if (manifestMap.has(key)) {
            const orig = manifestMap.get(key);
            if (orig.sha256 === actualSha256 && orig.size === f.size) {
                status = 'AUTHENTIC';
                detail = 'Integro e autentico al 100%';
                report.authentic_count++;
            } else {
                // Differenza rilevata
                status = 'MODIFIED';
                detail = `Modificato rispetto all'originale (SHA256: ${actualSha256.substring(0, 8)}... != orig: ${orig.sha256.substring(0, 8)}...)`;
                report.modified_count++;
            }
        } else {
            report.new_count++;
        }

        report.files.push({
            path: relNorm,
            size: f.size,
            crc32: actualCrc32,
            sha256: actualSha256,
            status,
            detail
        });
    }

    // Verifica file mancanti rispetto al manifest
    for (const [key, orig] of manifestMap.entries()) {
        if (!seenInDisk.has(key)) {
            report.missing_count++;
            report.files.push({
                path: orig.path,
                size: orig.size,
                crc32: orig.crc32,
                sha256: orig.sha256,
                status: 'DELETED',
                detail: 'File rimosso o assente nella cartella di lavoro'
            });
        }
    }

    // Stampa riepilogo a console
    console.log(`\n=== RIEPILOGO AUDIT DI SICUREZZA ===`);
    console.log(`  File Totali Rilevati:  ${report.total_disk_files}`);
    console.log(`  File Autentici/Intatti: \x1b[32m${report.authentic_count}\x1b[0m`);
    console.log(`  File Modificati:        \x1b[33m${report.modified_count}\x1b[0m`);
    console.log(`  Nuovi File:             \x1b[36m${report.new_count}\x1b[0m`);
    console.log(`  File Rimossi:           \x1b[31m${report.missing_count}\x1b[0m`);

    if (report.modified_count > 0) {
        console.log(`\n[DETTAGLIO FILE MODIFICATI]:`);
        report.files.filter(f => f.status === 'MODIFIED').forEach(f => {
            console.log(`  * \x1b[33m${f.path}\x1b[0m (${f.size} B, CRC: ${f.crc32})`);
        });
    }

    if (report.new_count > 0) {
        console.log(`\n[DETTAGLIO NUOVI FILE AGGIUNTI]:`);
        report.files.filter(f => f.status === 'NEW').forEach(f => {
            console.log(`  + \x1b[36m${f.path}\x1b[0m (${f.size} B)`);
        });
    }

    if (report.missing_count > 0) {
        console.log(`\n[DETTAGLIO FILE MANCANTI]:`);
        report.files.filter(f => f.status === 'DELETED').forEach(f => {
            console.log(`  - \x1b[31m${f.path}\x1b[0m`);
        });
    }
    console.log(``);

    return report;
}

/**
 * RE-IMPACCHETTAMENTO (Pack)
 * Converte i file di workDir nel pacchetto cifrato AES-256-CBC SYSTEM.PAK
 */
function packPackage(workDir, outPakPath, syncToBuild = true) {
    console.log(`\n===============================================================`);
    console.log(`  NewOS V1 — Generazione & Cifratura di SYSTEM.PAK`);
    console.log(`===============================================================`);
    console.log(`[SORGENTE] ${workDir}`);
    console.log(`[OUTPUT]   ${outPakPath}\n`);

    const files = getFilesRecursive(workDir);
    if (files.length === 0) {
        throw new Error(`Nessun file trovato in ${workDir} da impacchettare!`);
    }

    console.log(`[FILE] Rilevati ${files.length} file validi da impacchettare.`);

    // Calcolo dimensione payload decifrato
    let uncompressedTotal = 0;
    for (const f of files) {
        uncompressedTotal += (2 + Buffer.byteLength(f.relPath, 'utf-8') + 4 + 4 + f.size);
    }

    const plainBuf = Buffer.alloc(uncompressedTotal);
    let offset = 0;
    const manifestEntries = [];

    for (const f of files) {
        const fileData = fs.readFileSync(f.fullPath);
        const pathBuf = Buffer.from(f.relPath, 'utf-8');
        const fileCrc = crc32Buffer(fileData);
        const fileSha = sha256Buffer(fileData);

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

        manifestEntries.push({
            path: f.relPath,
            size: f.size,
            crc32: `0x${fileCrc.toString(16).toUpperCase().padStart(8, '0')}`,
            sha256: fileSha,
            status: 'AUTHENTIC'
        });
    }

    // Padding PKCS#7 / a 16 byte per AES
    const padLen = 16 - (plainBuf.length % 16);
    const padBuf = Buffer.alloc(padLen, padLen);
    const fullPlain = Buffer.concat([plainBuf, padBuf]);

    // Genera IV casuale a 128 bit (16 byte)
    const iv = crypto.randomBytes(16);

    // Cifratura simmetrica AES-256-CBC
    const cipher = crypto.createCipheriv('aes-256-cbc', NEWOS_AES256_KEY, iv);
    cipher.setAutoPadding(false);
    const encryptedPayload = Buffer.concat([cipher.update(fullPlain), cipher.final()]);
    const payloadCrc = crc32Buffer(encryptedPayload);

    // Header NOSPAK10 a 64 byte
    const headerBuf = Buffer.alloc(64, 0);
    headerBuf.write(PAK_MAGIC, 0, 8, 'ascii');
    headerBuf.writeUInt32LE(64, 8);                         // header_size
    headerBuf.writeUInt32LE(files.length, 12);              // total_files
    headerBuf.writeUInt32LE(uncompressedTotal, 16);         // total_uncompressed
    headerBuf.writeUInt32LE(encryptedPayload.length, 20);   // encrypted_size
    iv.copy(headerBuf, 24, 0, 16);                          // aes_iv (16 byte)
    headerBuf.writeUInt32LE(payloadCrc, 40);                // payload_crc32

    const finalPak = Buffer.concat([headerBuf, encryptedPayload]);

    const outDir = path.dirname(outPakPath);
    if (!fs.existsSync(outDir)) fs.mkdirSync(outDir, { recursive: true });
    fs.writeFileSync(outPakPath, finalPak);

    // Aggiorna anche il manifest locale in workDir
    const manifestPath = path.join(workDir, '.pack_manifest.json');
    const manifestData = {
        package_info: {
            magic: PAK_MAGIC,
            total_files: files.length,
            total_uncompressed_bytes: uncompressedTotal,
            encrypted_payload_crc32: `0x${payloadCrc.toString(16).toUpperCase().padStart(8, '0')}`,
            timestamp: new Date().toISOString(),
            output_file: path.resolve(outPakPath)
        },
        files: manifestEntries
    };
    fs.writeFileSync(manifestPath, JSON.stringify(manifestData, null, 2));

    console.log(`[GENERAZIONE COMPLETATA]`);
    console.log(`  File inclusi:             ${files.length}`);
    console.log(`  Dimensione non compressa: ${uncompressedTotal} byte (${(uncompressedTotal / 1024 / 1024).toFixed(2)} MB)`);
    console.log(`  Dimensione cifrata:       ${finalPak.length} byte (${(finalPak.length / 1024 / 1024).toFixed(2)} MB)`);
    console.log(`  Checksum CRC32 Cipher:    0x${payloadCrc.toString(16).toUpperCase()}`);
    console.log(`  Pacchetto salvato in:     ${outPakPath}`);

    // Sincronizzazione opzionale con la cartella build di sistema
    if (syncToBuild) {
        const rootDir = path.resolve(__dirname, '..');
        const sysPakBuild1 = path.join(rootDir, 'build', 'bin', 'BootSetup', 'SYSTEM.PAK');
        const sysPakBuild2 = path.join(rootDir, 'build', 'SYSTEM.PAK');

        try {
            if (fs.existsSync(path.dirname(sysPakBuild1))) {
                fs.copyFileSync(outPakPath, sysPakBuild1);
                console.log(`  [SYNC OK] Sincronizzato con: ${sysPakBuild1}`);
            }
            if (fs.existsSync(path.dirname(sysPakBuild2))) {
                fs.copyFileSync(outPakPath, sysPakBuild2);
                console.log(`  [SYNC OK] Sincronizzato con: ${sysPakBuild2}`);
            }
        } catch (e) {
            console.warn(`  [SYNC WARNING] Impossibile sincronizzare automaticamente in build: ${e.message}`);
        }
    }
    console.log(``);
    return finalPak.length;
}

/**
 * MODALITA' WATCHER (Monitoraggio in tempo reale)
 * Rileva modifiche ai file nella cartella e avvisa con calcolo checksum istantaneo
 */
function startWatcher(workDir, autoRepack = false, outPakPath = null) {
    console.log(`\n===============================================================`);
    console.log(`  NewOS V1 — File Watcher & Monitoraggio Anti-Manomissione`);
    console.log(`===============================================================`);
    console.log(`[CARTELLA SOTTO OSSERVAZIONE] ${workDir}`);
    console.log(`[MODALITA'] Auto-Repack: ${autoRepack ? 'ATTIVO' : 'MANUALE'}`);
    console.log(`In ascolto di modifiche... Premi CTRL+C per arrestare.\n`);

    let debounceTimer = null;
    let pendingChanges = new Set();

    fs.watch(workDir, { recursive: true }, (eventType, filename) => {
        if (!filename) return;
        const norm = filename.replace(/\\/g, '/');
        if (norm === 'SYSTEM.PAK' || norm === '.pack_manifest.json' || norm.endsWith('.tmp') ||
            norm.endsWith('.bat') || norm.startsWith('.')) return;

        pendingChanges.add({ type: eventType, file: norm, time: new Date() });

        if (debounceTimer) clearTimeout(debounceTimer);
        debounceTimer = setTimeout(() => {
            console.log(`\n[EVENTO RILEVATO @ ${new Date().toLocaleTimeString()}]`);
            for (const ch of pendingChanges) {
                const fullP = path.join(workDir, ch.file);
                if (fs.existsSync(fullP)) {
                    const st = fs.statSync(fullP);
                    if (!st.isDirectory()) {
                        const data = fs.readFileSync(fullP);
                        const crc = `0x${crc32Buffer(data).toString(16).toUpperCase().padStart(8, '0')}`;
                        const sha = sha256Buffer(data).substring(0, 16);
                        console.log(`  -> File Modificato: \x1b[33m${ch.file}\x1b[0m (${st.size} B | CRC: ${crc} | SHA256: ${sha}...)`);
                    }
                } else {
                    console.log(`  -> File Rimosso: \x1b[31m${ch.file}\x1b[0m`);
                }
            }
            pendingChanges.clear();

            if (autoRepack && outPakPath) {
                console.log(`[AUTO-REPACK] Avvio rigenerazione crittografica di SYSTEM.PAK...`);
                try {
                    packPackage(workDir, outPakPath, true);
                    console.log(`[AUTO-REPACK OK] Pacchetto aggiornato con successo!\n`);
                } catch (e) {
                    console.error(`[AUTO-REPACK ERRORE] ${e.message}\n`);
                }
            } else {
                console.log(`[INFO] Modifiche registrate. Esegui 'pack' per aggiornare SYSTEM.PAK.\n`);
            }
        }, 600);
    });
}

/**
 * SERVER GUI WEB (Interfaccia Grafica Utente nel Browser)
 */
function startGuiServer(workDir, defaultPakPath, port = 59800) {
    const htmlPage = `<!DOCTYPE html>
<html lang="it">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>NewOS V1 — Gestore & Audit SYSTEM.PAK</title>
<style>
  :root {
    --bg-dark: #0c1017;
    --card-bg: rgba(22, 27, 34, 0.85);
    --border: #30363d;
    --accent: #1f6feb;
    --accent-hover: #388bfd;
    --text: #f0f6fc;
    --text-muted: #8b949e;
    --success: #238636;
    --warning: #d29922;
    --danger: #da3633;
  }
  * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; }
  body { background: var(--bg-dark); color: var(--text); padding: 24px; min-height: 100vh; }
  .header { display: flex; justify-content: space-between; align-items: center; border-bottom: 1px solid var(--border); padding-bottom: 16px; margin-bottom: 24px; }
  .header h1 { font-size: 22px; font-weight: 600; display: flex; align-items: center; gap: 10px; }
  .badge-aes { background: #0e4429; color: #3fb950; padding: 4px 10px; border-radius: 12px; font-size: 12px; font-weight: bold; border: 1px solid rgba(63, 185, 80, 0.4); }
  .grid { display: grid; grid-template-columns: 320px 1fr; gap: 24px; }
  .card { background: var(--card-bg); border: 1px solid var(--border); border-radius: 8px; padding: 18px; backdrop-filter: blur(8px); }
  .card h2 { font-size: 15px; margin-bottom: 14px; color: var(--text-muted); text-transform: uppercase; letter-spacing: 0.5px; }
  .btn { display: flex; align-items: center; justify-content: center; gap: 8px; width: 100%; padding: 10px 14px; margin-bottom: 10px; background: var(--accent); color: white; border: none; border-radius: 6px; font-size: 14px; font-weight: 600; cursor: pointer; transition: background 0.15s; }
  .btn:hover { background: var(--accent-hover); }
  .btn-secondary { background: #21262d; border: 1px solid var(--border); }
  .btn-secondary:hover { background: #30363d; }
  .btn-success { background: var(--success); }
  .btn-success:hover { background: #2ea043; }
  .stats-row { display: flex; justify-content: space-between; padding: 8px 0; border-bottom: 1px solid rgba(255,255,255,0.06); font-size: 13px; }
  .stats-val { font-weight: bold; }
  .table-container { max-height: 520px; overflow-y: auto; border: 1px solid var(--border); border-radius: 6px; }
  table { width: 100%; border-collapse: collapse; font-size: 13px; text-align: left; }
  th { background: #161b22; position: sticky; top: 0; padding: 10px; border-bottom: 1px solid var(--border); font-size: 12px; color: var(--text-muted); }
  td { padding: 9px 10px; border-bottom: 1px solid rgba(255,255,255,0.04); }
  tr:hover { background: rgba(255,255,255,0.02); }
  .tag { font-size: 11px; padding: 2px 7px; border-radius: 4px; font-weight: 600; display: inline-block; }
  .tag-authentic { background: rgba(35, 134, 54, 0.2); color: #3fb950; border: 1px solid rgba(63, 185, 80, 0.3); }
  .tag-modified { background: rgba(210, 153, 34, 0.2); color: #d29922; border: 1px solid rgba(210, 153, 34, 0.3); }
  .tag-new { background: rgba(31, 111, 235, 0.2); color: #58a6ff; border: 1px solid rgba(88, 166, 255, 0.3); }
  .tag-deleted { background: rgba(218, 54, 51, 0.2); color: #f85149; border: 1px solid rgba(248, 81, 73, 0.3); }
  .console { background: #000; border: 1px solid var(--border); border-radius: 6px; padding: 12px; font-family: Consolas, monospace; font-size: 12px; color: #58a6ff; height: 160px; overflow-y: auto; margin-top: 16px; white-space: pre-wrap; }
</style>
</head>
<body>
<div class="header">
  <h1>
    <span>🛡️ NewOS V1 — SYSTEM.PAK Security & Package Manager</span>
    <span class="badge-aes">AES-256-CBC • NOSPAK10</span>
  </h1>
  <div style="font-size: 13px; color: var(--text-muted);">Cartella di Staging: <b>SystemPackFileWach</b></div>
</div>

<div class="grid">
  <div>
    <div class="card">
      <h2>Operazioni Pacchetto</h2>
      <button class="btn btn-secondary" onclick="runAction('unpack')">📥 1. Estrai da SYSTEM.PAK</button>
      <button class="btn btn-secondary" onclick="runAction('verify')">🔍 2. Audit Integrità (Checksum)</button>
      <button class="btn btn-success" onclick="runAction('pack')">🔒 3. Re-Impacchetta e Cifra</button>
    </div>

    <div class="card" style="margin-top: 16px;">
      <h2>Stato Sicurezza & Anti-Tamper</h2>
      <div class="stats-row"><span>Algoritmo:</span><span class="stats-val">AES-256-CBC</span></div>
      <div class="stats-row"><span>Chiave Master:</span><span class="stats-val" style="color: #3fb950;">256 bit (32 B)</span></div>
      <div class="stats-row"><span>Controllo Ciphertext:</span><span class="stats-val" id="st-cipher">CRC32 + NOSPAK10</span></div>
      <div class="stats-row"><span>File Totali:</span><span class="stats-val" id="st-total">--</span></div>
      <div class="stats-row"><span>Autentici:</span><span class="stats-val" id="st-auth" style="color:#3fb950;">--</span></div>
      <div class="stats-row"><span>Modificati:</span><span class="stats-val" id="st-mod" style="color:#d29922;">--</span></div>
      <div class="stats-row"><span>Nuovi / Aggiunti:</span><span class="stats-val" id="st-new" style="color:#58a6ff;">--</span></div>
    </div>
  </div>

  <div>
    <div class="card">
      <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom: 12px;">
        <h2>File nella Cartella di Lavoro</h2>
        <input type="text" id="search" placeholder="Filtra file..." style="background:#0d1117; border:1px solid var(--border); color:#fff; padding:6px 10px; border-radius:4px; font-size:12px; width:220px;" oninput="filterTable()">
      </div>
      <div class="table-container">
        <table id="files-table">
          <thead>
            <tr>
              <th>Percorso File</th>
              <th>Dimensione</th>
              <th>CRC32</th>
              <th>SHA-256 Impronta</th>
              <th>Stato Integrità</th>
            </tr>
          </thead>
          <tbody id="files-body">
            <tr><td colspan="5" style="text-align:center; color:var(--text-muted); padding:30px;">Caricamento in corso...</td></tr>
          </tbody>
        </table>
      </div>

      <div class="console" id="log-box">[CONSOLE INIZIALIZZATA] Pronto per l'audit.\n</div>
    </div>
  </div>
</div>

<script>
let allFiles = [];

function log(msg) {
  const box = document.getElementById('log-box');
  box.textContent += msg + '\\n';
  box.scrollTop = box.scrollHeight;
}

async function refreshData() {
  try {
    const res = await fetch('/api/status');
    const data = await res.json();
    document.getElementById('st-total').textContent = data.total_disk_files;
    document.getElementById('st-auth').textContent = data.authentic_count;
    document.getElementById('st-mod').textContent = data.modified_count;
    document.getElementById('st-new').textContent = data.new_count;
    allFiles = data.files || [];
    renderTable(allFiles);
  } catch(e) {
    log('Errore caricamento stato: ' + e.message);
  }
}

function renderTable(files) {
  const tbody = document.getElementById('files-body');
  if (!files || files.length === 0) {
    tbody.innerHTML = '<tr><td colspan="5" style="text-align:center; color:var(--text-muted); padding:30px;">Nessun file presente. Clicca su \"Estrai da SYSTEM.PAK\" per iniziare.</td></tr>';
    return;
  }
  tbody.innerHTML = files.map(f => {
    let tagClass = 'tag-new';
    if (f.status === 'AUTHENTIC') tagClass = 'tag-authentic';
    if (f.status === 'MODIFIED') tagClass = 'tag-modified';
    if (f.status === 'DELETED') tagClass = 'tag-deleted';
    return \`<tr>
      <td style="font-family:monospace; font-size:12px;">\${f.path}</td>
      <td>\${f.size.toLocaleString()} B</td>
      <td style="font-family:monospace; color:#8b949e;">\${f.crc32}</td>
      <td style="font-family:monospace; font-size:11px; color:#58a6ff;" title="\${f.sha256}">\${f.sha256.substring(0, 16)}...</td>
      <td><span class="tag \${tagClass}">\${f.status}</span></td>
    </tr>\`;
  }).join('');
}

function filterTable() {
  const q = document.getElementById('search').value.toLowerCase();
  const filtered = allFiles.filter(f => f.path.toLowerCase().includes(q));
  renderTable(filtered);
}

async function runAction(act) {
  log(\`--> Esecuzione operazione: \${act.toUpperCase()}...\`);
  try {
    const res = await fetch('/api/' + act, { method: 'POST' });
    const json = await res.json();
    if (json.ok) {
      log(\`[SUCCESS] Operazione completata con successo!\`);
      if (json.msg) log(json.msg);
    } else {
      log(\`[ERRORE] \${json.error}\`);
    }
    await refreshData();
  } catch(e) {
    log(\`[ERRORE NETWORK] \${e.message}\`);
  }
}

refreshData();
setInterval(refreshData, 3000);
</script>
</body>
</html>`;

    const server = http.createServer((req, res) => {
        const url = new URL(req.url, `http://localhost:${port}`);

        if (url.pathname === '/' || url.pathname === '/index.html') {
            res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
            return res.end(htmlPage);
        }

        if (url.pathname === '/api/status') {
            const report = verifyIntegrity(workDir, defaultPakPath);
            res.writeHead(200, { 'Content-Type': 'application/json' });
            return res.end(JSON.stringify(report));
        }

        if (url.pathname === '/api/unpack' && req.method === 'POST') {
            try {
                unpackPackage(defaultPakPath, workDir);
                res.writeHead(200, { 'Content-Type': 'application/json' });
                return res.end(JSON.stringify({ ok: true, msg: 'File estratti con successo da SYSTEM.PAK!' }));
            } catch (e) {
                res.writeHead(500, { 'Content-Type': 'application/json' });
                return res.end(JSON.stringify({ ok: false, error: e.message }));
            }
        }

        if (url.pathname === '/api/verify' && req.method === 'POST') {
            try {
                const report = verifyIntegrity(workDir, defaultPakPath);
                res.writeHead(200, { 'Content-Type': 'application/json' });
                return res.end(JSON.stringify({ ok: true, report }));
            } catch (e) {
                res.writeHead(500, { 'Content-Type': 'application/json' });
                return res.end(JSON.stringify({ ok: false, error: e.message }));
            }
        }

        if (url.pathname === '/api/pack' && req.method === 'POST') {
            try {
                const writtenBytes = packPackage(workDir, defaultPakPath, true);
                res.writeHead(200, { 'Content-Type': 'application/json' });
                return res.end(JSON.stringify({ ok: true, msg: `SYSTEM.PAK generato e cifrato con successo (${writtenBytes} byte)!` }));
            } catch (e) {
                res.writeHead(500, { 'Content-Type': 'application/json' });
                return res.end(JSON.stringify({ ok: false, error: e.message }));
            }
        }

        res.writeHead(404, { 'Content-Type': 'text/plain' });
        res.end('Not Found');
    });

    server.listen(port, () => {
        const urlStr = `http://localhost:${port}`;
        console.log(`\n===============================================================`);
        console.log(`  NewOS V1 — Pannello Grafico SYSTEM.PAK Avviato`);
        console.log(`===============================================================`);
        console.log(`  Interfaccia Web attiva su: \x1b[36m${urlStr}\x1b[0m`);
        console.log(`  Apertura automatica nel browser predefinito di Windows...\n`);
        exec(`start ${urlStr}`);
    });
}

// CLI Principale
function main() {
    const rootDir = path.resolve(__dirname, '..');
    const defaultWorkDir = path.join(rootDir, 'SystemPackFileWach');
    const defaultPakPath = path.join(rootDir, 'build', 'bin', 'BootSetup', 'SYSTEM.PAK');

    const args = process.argv.slice(2);
    const cmd = args[0] ? args[0].toLowerCase() : 'menu';

    if (cmd === 'unpack' || cmd === 'extract') {
        const pakPath = args[1] ? path.resolve(args[1]) : defaultPakPath;
        const outDir = args[2] ? path.resolve(args[2]) : defaultWorkDir;
        unpackPackage(pakPath, outDir);
    } else if (cmd === 'pack') {
        const inDir = args[1] ? path.resolve(args[1]) : defaultWorkDir;
        const outPak = args[2] ? path.resolve(args[2]) : defaultPakPath;
        packPackage(inDir, outPak, true);
    } else if (cmd === 'verify' || cmd === 'checksum') {
        const inDir = args[1] ? path.resolve(args[1]) : defaultWorkDir;
        const pakPath = args[2] ? path.resolve(args[2]) : defaultPakPath;
        verifyIntegrity(inDir, pakPath);
    } else if (cmd === 'watch') {
        const inDir = args[1] ? path.resolve(args[1]) : defaultWorkDir;
        const auto = args.includes('--auto-repack') || args.includes('-a');
        startWatcher(inDir, auto, defaultPakPath);
    } else if (cmd === 'gui') {
        startGuiServer(defaultWorkDir, defaultPakPath);
    } else {
        // Menu interattivo da terminale
        console.log(`\n===============================================================`);
        console.log(`  NewOS V1 — SYSTEM.PAK Package & Security Manager`);
        console.log(`===============================================================`);
        console.log(`  Cartella Staging: ${defaultWorkDir}`);
        console.log(`  File Pacchetto:   ${defaultPakPath}\n`);
        console.log(`  [1] Estrai SYSTEM.PAK in SystemPackFileWach (Unpack)`);
        console.log(`  [2] Verifica Integrita' & Checksum (Audit Anti-Manomissione)`);
        console.log(`  [3] Re-Impacchetta e Cifra in AES-256 (Pack)`);
        console.log(`  [4] Avvia Monitoraggio in Tempo Reale (Watch Mode)`);
        console.log(`  [5] Apri Interfaccia Grafica Web (GUI)`);
        console.log(`  [0] Esci\n`);

        const readline = require('readline').createInterface({
            input: process.stdin,
            output: process.stdout
        });

        readline.question('Seleziona un\'opzione [1-5]: ', (ans) => {
            readline.close();
            const choice = ans.trim();
            if (choice === '1') {
                unpackPackage(defaultPakPath, defaultWorkDir);
            } else if (choice === '2') {
                verifyIntegrity(defaultWorkDir, defaultPakPath);
            } else if (choice === '3') {
                packPackage(defaultWorkDir, defaultPakPath, true);
            } else if (choice === '4') {
                startWatcher(defaultWorkDir, false, defaultPakPath);
            } else if (choice === '5') {
                startGuiServer(defaultWorkDir, defaultPakPath);
            } else {
                console.log('Operazione annullata.');
            }
        });
    }
}

module.exports = {
    unpackPackage,
    packPackage,
    verifyIntegrity,
    startWatcher,
    startGuiServer,
    crc32Buffer,
    sha256Buffer
};

if (require.main === module) {
    main();
}
