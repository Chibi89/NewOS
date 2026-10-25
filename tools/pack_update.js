/**
 * tools/pack_update.js
 * Generatore di pacchetti di aggiornamento sicuri per NewOS V1 (64-bit).
 *
 * SPECIFICHE CRITTOGRAFICHE:
 * - Magic Header: "NOSPAK20"
 * - Schema "Encrypt-then-MAC": Cifratura AES-256-CBC + HMAC-SHA256 esterno (Verify-Before-Decrypt).
 * - Master Key AES-256: "NewOS-V1SETUP-256BIT-AUTHENTIC26"
 * - Master Key HMAC:   "NewSoft-NewOS-UpdateAuthMasterKey26"
 *
 * REGOLE PRIMARIE E VINCOLI DI SICUREZZA:
 * - Regola 1: Inviolabilità assoluta di C:\User\ (filtro case-insensitive: nessun file utente può entrare nel pacchetto).
 * - Regola 2: Divieto assoluto di sostituzione in blocco di REGISTRY.DAT.
 * - Regola 3: Inclusione autorizzata del differenziale certificato "registry_update.update".
 */

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const NEWOS_AES256_KEY = Buffer.from("NewOS-V1SETUP-256BIT-AUTHENTIC26", "utf-8");
const NEWOS_HMAC_KEY   = Buffer.from("NewSoft-NewOS-UpdateAuthMasterKey26", "utf-8");

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

/**
 * Scansione ricorsiva dei file da includere nell'aggiornamento.
 * Applica la sanitizzazione case-insensitive dei percorsi vietati (User\ e REGISTRY.DAT).
 */
function getUpdateFiles(dir, baseDir = '') {
    let results = [];
    if (!fs.existsSync(dir)) return results;

    const list = fs.readdirSync(dir);
    for (const file of list) {
        const fullPath = path.join(dir, file);
        const relPath = baseDir ? `${baseDir}/${file}` : file;
        const normPath = relPath.replace(/\\/g, '/').toLowerCase();

        // REGOLA PRIMARIA 1: Inviolabilità assoluta della cartella C:\User\ (case-insensitive)
        if (normPath === 'user' || normPath.startsWith('user/')) {
            console.log(`  [SECURITY FILTER] Scartato percorso protetto utente: ${relPath}`);
            continue;
        }

        // REGOLA PRIMARIA 2: Divieto assoluto di inclusione e sostituzione in blocco di REGISTRY.DAT
        if (normPath === 'registry.dat' || normPath.endsWith('/registry.dat')) {
            console.log(`  [SECURITY FILTER] Scartato REGISTRY.DAT monolitico (Regola 2): ${relPath}`);
            continue;
        }

        // Esclusione file temporanei, archivi o immagini disco
        const fnameUpper = file.toUpperCase();
        if (fnameUpper === 'UPDATE.PAK' || fnameUpper === 'SYSTEM.PAK' ||
            fnameUpper === 'CHANCECHATALOGY.INI' || // generato dinamicamente come 1° file
            fnameUpper.endsWith('.IMG') || fnameUpper.endsWith('.ISO') || fnameUpper.endsWith('.VDI') ||
            fnameUpper.endsWith('.BAK') || fnameUpper.endsWith('.NEW') || fnameUpper.endsWith('.TMP')) {
            continue;
        }

        const stat = fs.statSync(fullPath);
        if (stat.isDirectory()) {
            results = results.concat(getUpdateFiles(fullPath, relPath));
        } else {
            results.push({ fullPath, relPath: relPath.replace(/\\/g, '/'), size: stat.size });
        }
    }
    return results;
}

/**
 * Genera il contenitore crittografico UPDATE.PAK con schema Encrypt-then-MAC
 */
function packUpdate(sourceDir, outPakPath, options = {}) {
    console.log(`\n===============================================================================`);
    console.log(`NEWOS V1 - PACKAGER AGGIORNAMENTO SICURO (UPDATE.PAK)`);
    console.log(`Sorgente:  ${sourceDir}`);
    console.log(`Destinazione: ${outPakPath}`);
    console.log(`===============================================================================`);

    const files = getUpdateFiles(sourceDir);

    // Verifica presenza eventuale di registry_update.update esplicito
    let extraUpdateFile = options.registryUpdatePath || null;
    if (extraUpdateFile && fs.existsSync(extraUpdateFile)) {
        const hasIt = files.some(f => f.relPath.toLowerCase() === 'registry_update.update' || f.relPath.toLowerCase().endsWith('/registry_update.update'));
        if (!hasIt) {
            const stat = fs.statSync(extraUpdateFile);
            files.push({
                fullPath: extraUpdateFile,
                relPath: 'NewOS/Update/registry_update.update',
                size: stat.size
            });
            console.log(`  [PAYLOAD] Incluso differenziale registro: ${extraUpdateFile} -> NewOS/Update/registry_update.update`);
        }
    }

    // Generazione dinamica del file catalogo CHANCECHATALOGY.ini (1° file assoluto del pacchetto)
    const catalogLines = [
        "[Update_Catalog]",
        "PackageName=NewOS V1 Cumulative Update Package",
        "ReleaseDate=" + new Date().toISOString().slice(0, 10),
        "TotalFiles=" + (files.length + 1),
        "FirstFile=NewOS/Update/CHANCECHATALOGY.ini",
        "",
        "[WhatsNew]",
        "- Sottosistema di Aggiornamento Fuori Sessione (UPDATE.MOD Ring 0).",
        "- Container cifrato NOSPAK20 con schema crittografico AES-256-CBC.",
        "- Verifica integrita' Verify-Before-Decrypt con HMAC-SHA256 AuthTag.",
        "- Safety Check preventivo per proteggere chiavi HW/PCI/IRQ/RAM.",
        "- Inviolabilita' assoluta garantita per la cartella e i profili C:\\User.",
        "- Modifica granulare del registro con certificato NewSoft e rollback.",
        "- Generazione automatica dei file di ripristino REG_*_FallBack.reg.bak.",
        "- Setup Wizard: avvio aggiornamento guidato con watchdog e reboot.",
        "- Suite Windows Editor Registro per creare pacchetti .update certificati.",
        "- Player multimediale con playlist a scorrimento verticale e orizzontale.",
        "- Supporto nativo per file system Microsoft exFAT avanzato e RamFS.",
        "- Stack di rete Gigabit Intel PRO/1000 MT con DHCP dinamico e TLS 1.3.",
        "- Gestione energetica con ACPI/APM e standby display a basso consumo.",
        "",
        "[Files]"
    ];

    catalogLines.push("File_0=NewOS/Update/CHANCECHATALOGY.ini");
    files.forEach((f, idx) => {
        catalogLines.push(`File_${idx + 1}=${f.relPath}`);
    });
    catalogLines.push("");

    const catalogContent = catalogLines.join("\r\n");
    const catalogPath = path.join(sourceDir, 'CHANCECHATALOGY.ini');
    // Padding NUL a 4096 byte (1 cluster exFAT): la scrittura in-place sul disco
    // non accorcia il file, quindi il padding cancella eventuali residui precedenti.
    const CATALOG_SLOT = 4096;
    const catalogRaw = Buffer.from(catalogContent, 'utf-8');
    if (catalogRaw.length > CATALOG_SLOT) {
        throw new Error(`CHANCECHATALOGY.ini supera ${CATALOG_SLOT} byte (${catalogRaw.length})`);
    }
    const catalogBuf = Buffer.alloc(CATALOG_SLOT, 0);
    catalogRaw.copy(catalogBuf);
    fs.writeFileSync(catalogPath, catalogBuf);

    // Inserisce CHANCECHATALOGY.ini come primo file in testa alla lista
    files.unshift({
        fullPath: catalogPath,
        relPath: 'NewOS/Update/CHANCECHATALOGY.ini',
        size: CATALOG_SLOT
    });
    console.log(`  [CATALOG] Generato ed inserito come 1° file: NewOS/Update/CHANCECHATALOGY.ini (${files.length} file totali)`);

    console.log(`[INFO] Totale file validati per l'aggiornamento: ${files.length}`);

    // Calcolo dimensione payload decompresso
    let uncompressedTotal = 0;
    for (const f of files) {
        uncompressedTotal += (2 + Buffer.byteLength(f.relPath, 'utf-8') + 4 + 4 + 32 + f.size);
    }

    const plainBuf = Buffer.alloc(uncompressedTotal);
    let offset = 0;

    for (const f of files) {
        const fileData = fs.readFileSync(f.fullPath);
        const pathBuf = Buffer.from(f.relPath, 'utf-8');
        const fileCrc = crc32Buffer(fileData);
        const fileSha = crypto.createHash('sha256').update(fileData).digest();

        // Record formato v2: path_len (uint16), path, size (uint32), crc32 (uint32), sha256 (32 byte), data
        plainBuf.writeUInt16LE(pathBuf.length, offset);
        offset += 2;

        pathBuf.copy(plainBuf, offset);
        offset += pathBuf.length;

        plainBuf.writeUInt32LE(fileData.length, offset);
        offset += 4;

        plainBuf.writeUInt32LE(fileCrc, offset);
        offset += 4;

        fileSha.copy(plainBuf, offset, 0, 32);
        offset += 32;

        fileData.copy(plainBuf, offset);
        offset += fileData.length;
    }

    // Padding PKCS#7 a 16 byte per AES
    const padLen = 16 - (plainBuf.length % 16);
    const padBuf = Buffer.alloc(padLen, padLen);
    const fullPlain = Buffer.concat([plainBuf, padBuf]);

    // Genera IV crittografico a 128 bit (16 byte)
    const iv = crypto.randomBytes(16);

    // Cifratura simmetrica AES-256-CBC
    const cipher = crypto.createCipheriv('aes-256-cbc', NEWOS_AES256_KEY, iv);
    cipher.setAutoPadding(false);
    const encryptedPayload = Buffer.concat([cipher.update(fullPlain), cipher.final()]);
    const payloadCrc = crc32Buffer(encryptedPayload);

    // Schema "Encrypt-then-MAC": Calcolo HMAC-SHA256 su IV + Ciphertext
    const hmac = crypto.createHmac('sha256', NEWOS_HMAC_KEY);
    hmac.update(iv);
    hmac.update(encryptedPayload);
    const authTag = hmac.digest(); // 32 byte

    // Header del contenitore protetto NOSPAK20 (96 byte)
    const headerBuf = Buffer.alloc(96, 0);
    headerBuf.write("NOSPAK20", 0, 8, 'ascii');             // Magic (8 byte)
    headerBuf.writeUInt32LE(96, 8);                          // header_size
    headerBuf.writeUInt32LE(files.length, 12);               // total_files
    headerBuf.writeUInt32LE(uncompressedTotal, 16);          // total_uncompressed
    headerBuf.writeUInt32LE(encryptedPayload.length, 20);    // encrypted_size
    iv.copy(headerBuf, 24, 0, 16);                           // aes_iv (16 byte, offset 24..39)
    authTag.copy(headerBuf, 40, 0, 32);                      // hmac_sha256 AuthTag (32 byte, offset 40..71)
    headerBuf.writeUInt32LE(payloadCrc, 72);                 // checksum_crc32 del ciphertext (offset 72..75)
    headerBuf.writeUInt32LE(1, 76);                          // flags (bit 0 = HMAC enabled, bit 1 = double-buffered)
    // byte 80..95 riservati a zero per espansioni future

    const finalPak = Buffer.concat([headerBuf, encryptedPayload]);

    const outDir = path.dirname(outPakPath);
    if (!fs.existsSync(outDir)) fs.mkdirSync(outDir, { recursive: true });
    fs.writeFileSync(outPakPath, finalPak);

    console.log(`[SUCCESSO] Archivio UPDATE.PAK generato con successo!`);
    console.log(`  -> Magic Container: NOSPAK20 (AES-256 + HMAC-SHA256)`);
    console.log(`  -> File contenuti:  ${files.length}`);
    console.log(`  -> Dimensione non compressa: ${uncompressedTotal} byte (${(uncompressedTotal/1024).toFixed(1)} KB)`);
    console.log(`  -> Dimensione cifrata:        ${finalPak.length} byte (${(finalPak.length/1024).toFixed(1)} KB)`);
    console.log(`  -> HMAC-SHA256 AuthTag:       ${authTag.toString('hex').toUpperCase().substring(0, 32)}...`);
    console.log(`  -> File di destinazione:      ${outPakPath}`);
    console.log(`===============================================================================\n`);

    return finalPak.length;
}

/**
 * Verifica formale e crittografica del pacchetto UPDATE.PAK (Verify-Before-Decrypt)
 */
function verifyUpdatePak(pakPath) {
    if (!fs.existsSync(pakPath)) {
        console.error(`[ERRORE] File non trovato: ${pakPath}`);
        return false;
    }

    const pakData = fs.readFileSync(pakPath);
    if (pakData.length < 96) {
        console.error(`[FAIL] Pacchetto troppo piccolo per contenere l'header NOSPAK20!`);
        return false;
    }

    const magic = pakData.toString('ascii', 0, 8);
    if (magic !== 'NOSPAK20') {
        console.error(`[FAIL] Magic errato: "${magic}" invece di "NOSPAK20"!`);
        return false;
    }

    const headerSize = pakData.readUInt32LE(8);
    const totalFiles = pakData.readUInt32LE(12);
    const uncompressedSize = pakData.readUInt32LE(16);
    const encSize = pakData.readUInt32LE(20);
    const iv = pakData.subarray(24, 40);
    const expectedAuthTag = pakData.subarray(40, 72);
    const payloadCrc = pakData.readUInt32LE(72);

    if (pakData.length < headerSize + encSize) {
        console.error(`[FAIL] Dimensione file inferiore al payload cifrato dichiarato!`);
        return false;
    }

    const encryptedPayload = pakData.subarray(headerSize, headerSize + encSize);

    // 1. Verifica HMAC-SHA256 prima della decifratura (Verify-Before-Decrypt)
    const hmac = crypto.createHmac('sha256', NEWOS_HMAC_KEY);
    hmac.update(iv);
    hmac.update(encryptedPayload);
    const calcAuthTag = hmac.digest();

    if (!crypto.timingSafeEqual(calcAuthTag, expectedAuthTag)) {
        console.error(`[FAIL CRITTOGRAFICO] AuthTag HMAC-SHA256 non valido! Rischio manomissione pacchetto.`);
        return false;
    }

    // 2. Verifica CRC32 del Ciphertext
    const calcCrc = crc32Buffer(encryptedPayload);
    if (calcCrc !== payloadCrc) {
        console.error(`[FAIL] Checksum CRC32 del ciphertext non corrispondente!`);
        return false;
    }

    console.log(`[VERIFICA UPDATE.PAK OK]`);
    console.log(`  -> Magic:           ${magic}`);
    console.log(`  -> File contenuti:  ${totalFiles}`);
    console.log(`  -> Dimensione dati: ${uncompressedSize} byte`);
    console.log(`  -> AuthTag HMAC:    VALIDO (Integrita' verificata prima della decifratura)`);
    return true;
}

module.exports = { packUpdate, verifyUpdatePak, NEWOS_AES256_KEY, NEWOS_HMAC_KEY };

if (require.main === module) {
    const args = process.argv.slice(2);
    if (args.length === 0 || args[0] === '--help') {
        console.log("Uso: node pack_update.js <cartella_sorgente> <output_pak> [--reg-update <path>]");
        console.log("     node pack_update.js --verify <output_pak>");
        process.exit(0);
    }

    if (args[0] === '--verify' && args[1]) {
        const ok = verifyUpdatePak(args[1]);
        process.exit(ok ? 0 : 1);
    }

    const src = args[0];
    const out = args[1] || 'build/UPDATE.PAK';
    let regUpdate = null;
    const regIdx = args.indexOf('--reg-update');
    if (regIdx !== -1 && args[regIdx + 1]) {
        regUpdate = args[regIdx + 1];
    }

    packUpdate(src, out, { registryUpdatePath: regUpdate });
}
