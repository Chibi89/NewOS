/**
 * tools/mkexfat.js
 * Generatore e formattatore per File System Microsoft exFAT 1.00
 * Specifiche ufficiali Microsoft (2019)
 */

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

// Calcolo del checksum standard a 32-bit exFAT per Boot Region
function exfatBootChecksum(buf, bytesToHash) {
    let checksum = 0;
    for (let i = 0; i < bytesToHash; i++) {
        // Salta VolumeFlags (offset 106, 107) e PercentInUse (offset 112) nel settore 0
        if (i === 106 || i === 107 || i === 112) continue;
        // Checksum rotation a 31 bit: (checksum >>> 1) | (checksum << 31)
        checksum = (((checksum >>> 1) | ((checksum & 1) << 31)) + buf[i]) >>> 0;
    }
    return checksum;
}

// Calcolo checksum per Directory Entry Set (16-bit)
function exfatEntrySetChecksum(entriesBuffer, numEntries) {
    let checksum = 0;
    for (let e = 0; e < numEntries; e++) {
        for (let b = 0; b < 32; b++) {
            // Salta SetChecksum stesso (offset 2 e 3 della prima entry 0x85)
            if (e === 0 && (b === 2 || b === 3)) continue;
            const byte = entriesBuffer[e * 32 + b];
            checksum = (((checksum >>> 1) | ((checksum & 1) << 15)) + byte) & 0xFFFF;
        }
    }
    return checksum;
}

// Hash del nome file a 16-bit
function exfatNameHash(upcaseName) {
    let hash = 0;
    const buf = Buffer.from(upcaseName, 'utf16le');
    for (let i = 0; i < buf.length; i++) {
        hash = (((hash >>> 1) | ((hash & 1) << 15)) + buf[i]) & 0xFFFF;
    }
    return hash;
}

// Costruzione tabella Up-Case standard compressa (oppure identità ASCII A-Z)
function createUpcaseTable() {
    // Generiamo una tabella standard completa di 512 byte (sufficiente per caratteri standard e ASCII maiuscolo)
    // Nelle specifiche exFAT: tabella completa ha 128 record o formato compresso.
    // Il formato compresso: se un range mappa 1:1, si scrive 0xFFFF, count.
    // Un modo universale compatibile al 100%: tabella uncompressed diretta per i caratteri BMP base (512 byte)
    // Oppure tabella compressa ufficiale:
    // [0..a-1]: identità (0xFFFF, 0x0061) -> mappa 0x0000..0x0060 a se stessi
    // [a..z]: mappa 'a'..'z' (0x61..0x7A) a 'A'..'Z' (0x41..0x5A)
    // [z+1..0xFFFF]: identità (0xFFFF, 0xFF85)
    const buf = Buffer.alloc(512, 0);
    // Tabella per i primi 256 caratteri UTF-16:
    for (let i = 0; i < 256; i++) {
        let up = i;
        if (i >= 0x61 && i <= 0x7A) up = i - 0x20; // 'a'..'z' -> 'A'..'Z'
        buf.writeUInt16LE(up, i * 2);
    }
    return buf;
}

function calculateTableChecksum(tableBuf) {
    let checksum = 0;
    for (let i = 0; i < tableBuf.length; i++) {
        checksum = (((checksum >>> 1) | ((checksum & 1) << 31)) + tableBuf[i]) >>> 0;
    }
    return checksum;
}

class ExFatBuilder {
    constructor(totalVolumeSectors, partitionLbaOffset = 2048) {
        this.totalSectors = totalVolumeSectors;
        this.partitionLba = partitionLbaOffset;
        this.bytesPerSector = 512;
        this.sectorShift = 9; // 1 << 9 = 512
        this.sectorsPerCluster = 8; // 4096 bytes per cluster
        this.clusterShift = 3; // 1 << 3 = 8
        this.clusterSize = this.bytesPerSector * this.sectorsPerCluster; // 4096

        this.fatOffset = 24; // dopo Main (12) e Backup (12) Boot sectors
        // Calcola lunghezza FAT in settori
        // Ogni cluster ha 4 byte nella FAT
        const maxPossibleClusters = Math.floor((this.totalSectors - 256) / this.sectorsPerCluster);
        const fatBytes = (maxPossibleClusters + 2) * 4;
        this.fatLength = Math.ceil(fatBytes / this.bytesPerSector);
        if (this.fatLength < 8) this.fatLength = 8;

        // Cluster heap allineato a cluster (es. settore 64 o 128)
        this.clusterHeapOffset = Math.ceil((this.fatOffset + this.fatLength) / this.sectorsPerCluster) * this.sectorsPerCluster;
        if (this.clusterHeapOffset < 64) this.clusterHeapOffset = 64;

        this.clusterCount = Math.floor((this.totalSectors - this.clusterHeapOffset) / this.sectorsPerCluster);

        // Allocazione buffer volume intero
        this.volumeBuffer = Buffer.alloc(this.totalSectors * this.bytesPerSector, 0);

        // Strutture logiche
        this.nextCluster = 2; // I cluster partono da 2
        this.bitmapCluster = 0;
        this.upcaseCluster = 0;
        this.rootDirCluster = 0;

        this.allocatedClusters = new Set();
        this.fatEntries = new Map(); // cluster -> nextCluster (o 0xFFFFFFFF)

        // Inizializza FAT e Directory
        this.initStructure();
    }

    allocCluster(count = 1) {
        const start = this.nextCluster;
        for (let i = 0; i < count; i++) {
            const cl = start + i;
            this.allocatedClusters.add(cl);
            if (i < count - 1) {
                this.fatEntries.set(cl, cl + 1);
            } else {
                this.fatEntries.set(cl, 0xFFFFFFFF);
            }
        }
        this.nextCluster += count;
        return start;
    }

    clusterToSector(cluster) {
        return this.clusterHeapOffset + (cluster - 2) * this.sectorsPerCluster;
    }

    writeClusterData(cluster, dataBuffer, offsetInCluster = 0) {
        const sector = this.clusterToSector(cluster);
        const byteOffset = sector * this.bytesPerSector + offsetInCluster;
        dataBuffer.copy(this.volumeBuffer, byteOffset);
    }

    initStructure() {
        // 1. Alloca cluster per Allocation Bitmap (Cluster 2)
        const bitmapBytes = Math.ceil(this.clusterCount / 8);
        const bitmapClusters = Math.ceil(bitmapBytes / this.clusterSize);
        this.bitmapCluster = this.allocCluster(bitmapClusters);

        // 2. Alloca cluster per Up-case table (Cluster 3)
        this.upcaseTableBuf = createUpcaseTable();
        const upcaseClusters = Math.ceil(this.upcaseTableBuf.length / this.clusterSize);
        this.upcaseCluster = this.allocCluster(upcaseClusters);
        this.writeClusterData(this.upcaseCluster, this.upcaseTableBuf);

        // 3. Alloca Root Directory (Cluster 4, iniziale 1 cluster espandibile)
        this.rootDirCluster = this.allocCluster(1);
    }

    // Costruisce il set di directory entries per un file o una directory
    createEntrySet(name, isDirectory, firstCluster, fileLength) {
        const upcaseName = name.toUpperCase();
        const nameLen = name.length;
        const nameEntriesCount = Math.ceil(nameLen / 15);
        const totalEntries = 1 + 1 + nameEntriesCount; // File + StreamExt + Name*N
        const setBuf = Buffer.alloc(totalEntries * 32, 0);

        // 1. File Directory Entry (0x85)
        setBuf[0] = 0x85;
        setBuf[1] = totalEntries - 1; // SecondaryCount
        // Offset 2..3: SetChecksum (calcolato dopo)
        const attr = isDirectory ? 0x0010 : 0x0020; // 0x10=Directory, 0x20=Archive
        setBuf.writeUInt16LE(attr, 4);
        // Timestamp DOS-like
        const now = new Date();
        const dosTime = ((now.getHours() << 11) | (now.getMinutes() << 5) | (now.getSeconds() >> 1)) & 0xFFFF;
        const dosDate = (((now.getFullYear() - 1980) << 9) | ((now.getMonth() + 1) << 5) | now.getDate()) & 0xFFFF;
        const dosTimestamp = (dosDate << 16) | dosTime;
        setBuf.writeUInt32LE(dosTimestamp, 8);  // Create
        setBuf.writeUInt32LE(dosTimestamp, 12); // Modified
        setBuf.writeUInt32LE(dosTimestamp, 16); // Accessed

        // 2. Stream Extension Entry (0xC0)
        const streamOff = 32;
        setBuf[streamOff + 0] = 0xC0;
        setBuf[streamOff + 1] = 0x03; // AllocationPossible(1) | NoFatChain(2) -> Contiguo!
        setBuf[streamOff + 3] = nameLen;
        setBuf.writeUInt16LE(exfatNameHash(upcaseName), streamOff + 4);
        setBuf.writeUInt32LE(firstCluster, streamOff + 20); // FirstCluster
        setBuf.writeBigUInt64LE(BigInt(fileLength), streamOff + 24); // ValidDataLength
        setBuf.writeBigUInt64LE(BigInt(fileLength), streamOff + 32); // DataLength (Wait: offset 24..31 Valid, 32..39 Data)
        // Correggiamo offset ufficiali exFAT Stream Extension:
        // Offset 0x14 (20): FirstCluster (4B)
        // Offset 0x18 (24): ValidDataLength (8B)
        // Offset 0x20 (32) nell'entry da 32 byte? Wait, entry è 32 byte!
        // In exFAT Stream Extension:
        // 0x00 (1B): Type (0xC0)
        // 0x01 (1B): GeneralFlags
        // 0x02 (1B): Reserved1
        // 0x03 (1B): NameLength
        // 0x04 (2B): NameHash
        // 0x06 (2B): Reserved2
        // 0x08 (8B): ValidDataLength
        // 0x10 (4B): Reserved3
        // 0x14 (4B): FirstCluster
        // 0x18 (8B): DataLength
        // Riscrittura precisa degli offset di Stream Extension:
        setBuf.writeBigUInt64LE(BigInt(fileLength), streamOff + 8);  // ValidDataLength
        setBuf.writeUInt32LE(firstCluster, streamOff + 20);          // FirstCluster
        setBuf.writeBigUInt64LE(BigInt(fileLength), streamOff + 24); // DataLength

        // 3. File Name Entries (0xC1)
        for (let i = 0; i < nameEntriesCount; i++) {
            const entryOff = (2 + i) * 32;
            setBuf[entryOff + 0] = 0xC1;
            setBuf[entryOff + 1] = 0x00; // GeneralFlags
            const subStr = name.substr(i * 15, 15);
            for (let c = 0; c < 15; c++) {
                const charCode = (c < subStr.length) ? subStr.charCodeAt(c) : 0x0000;
                setBuf.writeUInt16LE(charCode, entryOff + 2 + c * 2);
            }
        }

        // Calcolo e inserimento del SetChecksum a offset 2
        const checksum = exfatEntrySetChecksum(setBuf, totalEntries);
        setBuf.writeUInt16LE(checksum, 2);

        return setBuf;
    }

    addFileToDir(dirEntriesArray, name, dataBuffer) {
        const fileLen = dataBuffer ? dataBuffer.length : 0;
        let firstCl = 0;
        if (fileLen > 0) {
            const clCount = Math.ceil(fileLen / this.clusterSize);
            firstCl = this.allocCluster(clCount);
            // Scrivi cluster per cluster
            for (let i = 0; i < clCount; i++) {
                const chunk = dataBuffer.subarray(i * this.clusterSize, Math.min(fileLen, (i + 1) * this.clusterSize));
                this.writeClusterData(firstCl + i, chunk);
            }
        }
        const entrySet = this.createEntrySet(name, false, firstCl, fileLen);
        dirEntriesArray.push(entrySet);
    }

    addDirToDir(dirEntriesArray, name, subDirEntriesArray) {
        // Calcola dimensione entries della sotto-directory
        let totalBytes = 0;
        for (const e of subDirEntriesArray) totalBytes += e.length;
        const clCount = Math.max(1, Math.ceil(totalBytes / this.clusterSize));
        const dirCl = this.allocCluster(clCount);

        // Assembla e scrivi dati directory
        const dirBuf = Buffer.concat(subDirEntriesArray);
        this.writeClusterData(dirCl, dirBuf);

        // Aggiungi entry nella cartella padre
        const entrySet = this.createEntrySet(name, true, dirCl, totalBytes);
        dirEntriesArray.push(entrySet);
    }

    finalize() {
        // 1. Scrivi Allocation Bitmap nel Cluster 2
        const bitmapBytes = Math.ceil(this.clusterCount / 8);
        const bitmapBuf = Buffer.alloc(bitmapBytes, 0);
        for (const cl of this.allocatedClusters) {
            const idx = cl - 2; // bit 0 = cluster 2
            if (idx >= 0 && idx < this.clusterCount) {
                bitmapBuf[Math.floor(idx / 8)] |= (1 << (idx % 8));
            }
        }
        this.writeClusterData(this.bitmapCluster, bitmapBuf);

        // 2. Scrivi FAT Table
        const fatBuf = Buffer.alloc(this.fatLength * this.bytesPerSector, 0);
        fatBuf.writeUInt32LE(0xFFFFFFF8, 0); // Media descriptor
        fatBuf.writeUInt32LE(0xFFFFFFFF, 4); // End marker
        for (const [cl, next] of this.fatEntries.entries()) {
            if (cl * 4 + 4 <= fatBuf.length) {
                fatBuf.writeUInt32LE(next, cl * 4);
            }
        }
        fatBuf.copy(this.volumeBuffer, this.fatOffset * this.bytesPerSector);

        // 3. Root Directory: Aggiungi le 3 entry speciali standard prima dei file
        const rootEntries = [];

        // Entry 0x81: Allocation Bitmap Directory Entry
        const bmEntry = Buffer.alloc(32, 0);
        bmEntry[0] = 0x81;
        bmEntry[1] = 0; // BitmapFlags
        bmEntry.writeUInt32LE(this.bitmapCluster, 20); // FirstCluster
        bmEntry.writeBigUInt64LE(BigInt(bitmapBytes), 24); // DataLength
        rootEntries.push(bmEntry);

        // Entry 0x82: Up-case Table Directory Entry
        const upEntry = Buffer.alloc(32, 0);
        upEntry[0] = 0x82;
        upEntry.writeUInt32LE(calculateTableChecksum(this.upcaseTableBuf), 4); // TableChecksum
        upEntry.writeUInt32LE(this.upcaseCluster, 20); // FirstCluster
        upEntry.writeBigUInt64LE(BigInt(this.upcaseTableBuf.length), 24); // DataLength
        rootEntries.push(upEntry);

        // Entry 0x83: Volume Label
        const volEntry = Buffer.alloc(32, 0);
        volEntry[0] = 0x83;
        const label = "NEWOS_V1";
        volEntry[1] = label.length;
        for (let i = 0; i < label.length; i++) {
            volEntry.writeUInt16LE(label.charCodeAt(i), 2 + i * 2);
        }
        rootEntries.push(volEntry);

        return rootEntries;
    }

    writeBootSectors() {
        const vbr = Buffer.alloc(512, 0);

        // JumpBoot: EB 76 90
        vbr[0] = 0xEB; vbr[1] = 0x76; vbr[2] = 0x90;
        // FileSystemName: "EXFAT   "
        vbr.write("EXFAT   ", 3, 8, 'ascii');
        // MustBeZero (offset 11..63) già zero

        // PartitionOffset: uint64 (LBA offset della partizione su disco)
        vbr.writeBigUInt64LE(BigInt(this.partitionLba), 64);
        // VolumeLength: uint64 (numero settori del volume)
        vbr.writeBigUInt64LE(BigInt(this.totalSectors), 72);
        // FatOffset: uint32
        vbr.writeUInt32LE(this.fatOffset, 80);
        // FatLength: uint32
        vbr.writeUInt32LE(this.fatLength, 84);
        // ClusterHeapOffset: uint32
        vbr.writeUInt32LE(this.clusterHeapOffset, 88);
        // ClusterCount: uint32
        vbr.writeUInt32LE(this.clusterCount, 92);
        // FirstClusterOfRootDirectory: uint32
        vbr.writeUInt32LE(this.rootDirCluster, 96);
        // VolumeSerialNumber: uint32
        vbr.writeUInt32LE(0x20260926, 100);
        // FileSystemRevision: uint16 (0x0100)
        vbr.writeUInt16LE(0x0100, 104);
        // VolumeFlags: uint16 (0)
        vbr.writeUInt16LE(0x0000, 106);
        // BytesPerSectorShift: uint8 (9)
        vbr[108] = this.sectorShift;
        // SectorsPerClusterShift: uint8 (3)
        vbr[109] = this.clusterShift;
        // NumberOfFats: uint8 (1)
        vbr[110] = 1;
        // DriveSelect: uint8 (0x80)
        vbr[111] = 0x80;
        // PercentInUse: uint8
        vbr[112] = Math.min(100, Math.floor((this.allocatedClusters.size * 100) / this.clusterCount));

        // BootSignature (0xAA55)
        vbr[510] = 0x55;
        vbr[511] = 0xAA;

        // Copia VBR a settore 0
        vbr.copy(this.volumeBuffer, 0);

        // Extended Boot Sectors (settori 1..8): terminano con 0xAA55
        for (let s = 1; s <= 8; s++) {
            this.volumeBuffer[s * 512 + 510] = 0x55;
            this.volumeBuffer[s * 512 + 511] = 0xAA;
        }

        // Settori 9 (OEM Parameter) e 10 (Reserved): terminano con 0xAA55
        this.volumeBuffer[9 * 512 + 510] = 0x55;
        this.volumeBuffer[9 * 512 + 511] = 0xAA;
        this.volumeBuffer[10 * 512 + 510] = 0x55;
        this.volumeBuffer[10 * 512 + 511] = 0xAA;

        // Settore 11: Boot Checksum Sector
        const checksum = exfatBootChecksum(this.volumeBuffer, 11 * 512);
        const checkSector = Buffer.alloc(512);
        for (let i = 0; i < 128; i++) {
            checkSector.writeUInt32LE(checksum, i * 4);
        }
        checkSector.copy(this.volumeBuffer, 11 * 512);

        // Backup Boot Region (settori 12..23): replica esatta dei settori 0..11
        this.volumeBuffer.copy(this.volumeBuffer, 12 * 512, 0, 12 * 512);

        console.log(`[exFAT] Boot Region scritta. Checksum: 0x${checksum.toString(16).toUpperCase()}`);
    }
}

function buildMyOsFileSystem(rootDir, totalSectors = 63488, partitionLba = 2048) {
    const builder = new ExFatBuilder(totalSectors, partitionLba);
    const rootEntries = builder.finalize();

    // 1. Directory SYSTEM
    const systemEntries = [];

    // Icon Resources: ICONRESOURCE.DLL e alias ICONRESOURCE.DL (sostituisce ICONS.BIN)
    const iconsPath = path.join(rootDir, 'resources', 'icons.bin');
    if (fs.existsSync(iconsPath)) {
        const iconBuf = fs.readFileSync(iconsPath);
        builder.addFileToDir(systemEntries, 'ICONRESOURCE.DLL', iconBuf);
        builder.addFileToDir(systemEntries, 'ICONRESOURCE.DL', iconBuf);
        builder.addFileToDir(systemEntries, 'ICONS.BIN', iconBuf);
    }

    // WALLPAPERS (Formati standard .BMP e .JPG a 1024x768)
    const wallEntries = [];
    const wpDir = path.join(rootDir, 'resources', 'wallpaper');
    const wpList = [
        'HERO.BMP', 'HERO.JPG',
        'FLOW.BMP', 'FLOW.JPG',
        'BLISS.BMP', 'BLISS.JPG',
        'THEME_A.BMP', 'THEME_A.JPG'
    ];
    for (const f of wpList) {
        const fp = path.join(wpDir, f);
        if (fs.existsSync(fp)) {
            builder.addFileToDir(wallEntries, f, fs.readFileSync(fp));
        }
    }
    builder.addDirToDir(systemEntries, 'WALLPAPERS', wallEntries);

    // SOUNDS
    const soundEntries = [];
    const startupWav = path.join(rootDir, 'resources', 'sounds', 'Windows Startup.wav');
    if (fs.existsSync(startupWav)) {
        builder.addFileToDir(soundEntries, 'STARTUP.WAV', fs.readFileSync(startupWav));
    }
    const recycleWav = path.join(rootDir, 'resources', 'sounds', 'Windows Recycle.wav');
    if (fs.existsSync(recycleWav)) {
        builder.addFileToDir(soundEntries, 'RECYCLE.WAV', fs.readFileSync(recycleWav));
    }
    const dingWav = path.join(rootDir, 'resources', 'sounds', 'ding.wav');
    if (fs.existsSync(dingWav)) {
        builder.addFileToDir(soundEntries, 'DING.WAV', fs.readFileSync(dingWav));
    }
    const chordWav = path.join(rootDir, 'resources', 'sounds', 'chord.wav');
    if (fs.existsSync(chordWav)) {
        builder.addFileToDir(soundEntries, 'CHORD.WAV', fs.readFileSync(chordWav));
    }
    const tadaWav = path.join(rootDir, 'resources', 'sounds', 'tada.wav');
    if (fs.existsSync(tadaWav)) {
        builder.addFileToDir(soundEntries, 'TADA.WAV', fs.readFileSync(tadaWav));
    }
    builder.addDirToDir(systemEntries, 'SOUNDS', soundEntries);

    // Eseguibili .EXE e applet .CPL di Sistema in C:\SYSTEM\
    const peStub = Buffer.alloc(1024, 0);
    peStub[0] = 0x4D; peStub[1] = 0x5A; // 'MZ'
    peStub.writeUInt32LE(0x80, 0x3C);   // e_lfanew
    peStub[0x80] = 0x50; peStub[0x81] = 0x45; // 'PE\0\0'
    peStub.writeUInt16LE(0x8664, 0x84); // Machine x86_64
    peStub[0x100] = 0xC3; // ret instruction

    // Eseguibili e Tool Core di Sistema
    builder.addFileToDir(systemEntries, 'EXPLORER.EXE', peStub);
    builder.addFileToDir(systemEntries, 'NOTEPAD.EXE', peStub);
    builder.addFileToDir(systemEntries, 'CALC.EXE', peStub);
    builder.addFileToDir(systemEntries, 'REGEDIT.EXE', peStub);
    builder.addFileToDir(systemEntries, 'TASKMGR.EXE', peStub);
    builder.addFileToDir(systemEntries, 'DISKMGMT.EXE', peStub);
    builder.addFileToDir(systemEntries, 'CHKDSK.EXE', peStub);
    builder.addFileToDir(systemEntries, 'EVENTVWR.EXE', peStub);

    // Applet del Pannello di Controllo (.CPL) di Sistema
    builder.addFileToDir(systemEntries, 'DISPLAY.CPL', peStub);
    builder.addFileToDir(systemEntries, 'POWER.CPL', peStub);
    builder.addFileToDir(systemEntries, 'NETWORK.CPL', peStub);
    builder.addFileToDir(systemEntries, 'DEVMGMT.CPL', peStub);
    builder.addFileToDir(systemEntries, 'SOUND.CPL', peStub);
    builder.addFileToDir(systemEntries, 'DATEANDTIME.CPL', peStub);
    builder.addFileToDir(systemEntries, 'PROGRAMS.CPL', peStub);
    builder.addFileToDir(systemEntries, 'ACCESSIBILITY.CPL', peStub);
    builder.addFileToDir(systemEntries, 'REGEDIT.CPL', peStub);

    // Database del Registro di Sistema (C:\SYSTEM\REGISTRY.DAT e C:\NewOS\Registry\REGISTRY.DAT)
    const NEWOS_REGISTRY_AES_KEY = Buffer.from("NewOS-V1SETUP-256BIT-AUTHENTIC26", "ascii");

    function encryptRegistryEnvelope(plainIniText) {
        const iv = crypto.randomBytes(16);
        const cipher = crypto.createCipheriv('aes-256-cbc', NEWOS_REGISTRY_AES_KEY, iv);
        let enc = cipher.update(plainIniText, 'utf8');
        enc = Buffer.concat([enc, cipher.final()]);

        const banner = 
`[NEWOS-V1-ENCRYPTED-REGISTRY]
AVVISO=Accesso in chiaro vincolato all'applicazione di sistema Registry (REGEDIT.EXE).
Algoritmo=AES-256-CBC-PKCS7
Status=Encrypted
KeyHash=0x3951E7A2
--- INIZIO BLOCCO CRITTOGRAFATO (AES-256) ---
IV=${iv.toString('hex').toUpperCase()}
DATA=${enc.toString('hex').toUpperCase()}
--- FINE BLOCCO CRITTOGRAFATO ---
`;
        return banner;
    }

    function createNorFile(ruleId, category, severity, text) {
        const NOR_VERSION = 0x00010000;
        const NOR_HEADER_SIZE = 128;
        const NOR_FLAG_DESKTOP_ONLY = 0x00000001;
        const NOR_CIPHER_KEY = Buffer.from('NewSoft-NewOS-RuleSecurityKey-26', 'ascii');

        const plainBuf = Buffer.from(text, 'utf8');
        const payloadSize = plainBuf.length;

        let crc = 0xFFFFFFFF;
        for (let i = 0; i < plainBuf.length; i++) {
            crc ^= plainBuf[i];
            for (let j = 0; j < 8; j++) {
                crc = (crc >>> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
            }
        }
        const payloadCrc32 = (crc ^ 0xFFFFFFFF) >>> 0;

        const header = Buffer.alloc(128, 0);
        header.write('NOSR', 0, 4, 'ascii');
        header.writeUInt32LE(NOR_VERSION, 4);
        header.writeUInt32LE(NOR_HEADER_SIZE, 8);
        header.writeUInt32LE(NOR_FLAG_DESKTOP_ONLY, 12);
        header.write(ruleId.padEnd(32, '\0'), 16, 32, 'ascii');
        header.write(category.padEnd(32, '\0'), 48, 32, 'ascii');
        header.write(severity.padEnd(16, '\0'), 80, 16, 'ascii');
        header.write('NEWSOFT-RULE-V1\0', 96, 16, 'ascii');
        header.writeUInt32LE(20261018, 112);
        header.writeUInt32LE(payloadCrc32, 116);
        header.writeUInt32LE(payloadSize, 120);
        header.writeUInt32LE(0, 124);

        const cipherBuf = Buffer.alloc(payloadSize);
        for (let i = 0; i < payloadSize; i++) {
            cipherBuf[i] = plainBuf[i] ^ NOR_CIPHER_KEY[i % 32] ^ ((i * 11 + 0x6A) & 0xFF);
        }

        return Buffer.concat([header, cipherBuf]);
    }

    const registryDatContent = 
`; NewOS V1 Registry Database (C:\\NewOS\\Registry\\REGISTRY.DAT)
; (C) Newspoint Software - NewSoft - NewOS V1

[HKEY_LOCAL_MACHINE\\SYSTEM]
(Predefinito)=REG_SZ:(valore non impostato)
ProductName=REG_SZ:NewOS V1 64-bit
Version=REG_SZ:1.0.0
BuildNumber=REG_DWORD:2026
CurrentResolution=REG_SZ:1024x768
Bpp=REG_DWORD:32
CpuVendor=REG_SZ:AuthenticAMD
MemTotalMB=REG_DWORD:128
DebugSerial=REG_DWORD:1

[HKEY_LOCAL_MACHINE\\DRIVERS]
(Predefinito)=REG_SZ:(valore non impostato)
VGA=REG_SZ:C:\\NewOS\\Driver\\VGA.SYS
AC97=REG_SZ:C:\\NewOS\\Driver\\AC97.SYS
E1000=REG_SZ:C:\\NewOS\\Driver\\E1000.SYS
PS2MOUSE=REG_SZ:C:\\NewOS\\Driver\\PS2MOUSE.SYS
PS2KBD=REG_SZ:C:\\NewOS\\Driver\\PS2KBD.SYS
RTC=REG_SZ:C:\\NewOS\\Driver\\RTC.SYS
POWER=REG_SZ:C:\\NewOS\\Driver\\POWER.SYS

[HKEY_LOCAL_MACHINE\\NETWORK]
(Predefinito)=REG_SZ:(valore non impostato)
Adapter=REG_SZ:Intel PRO/1000 MT Desktop (82540EM)
DHCPEnabled=REG_DWORD:1
IPAddress=REG_SZ:10.0.2.15
SubnetMask=REG_SZ:255.255.255.0
DefaultGateway=REG_SZ:10.0.2.2
NameServer=REG_SZ:10.0.2.3
PingTarget=REG_SZ:95.141.32.1

[HKEY_LOCAL_MACHINE\\NEWOS\\SYSTEM]
(Predefinito)=REG_SZ:(valore non impostato)
SystemRoot=REG_SZ:C:\\NewOS
SystemProgramDir=REG_SZ:C:\\NewOS\\SystemProgram
RegistryDir=REG_SZ:C:\\NewOS\\Registry
DevDir=REG_SZ:C:\\NewOS\\Dev
RulesDir=REG_SZ:C:\\NewOS\\Rules
DriverDir=REG_SZ:C:\\NewOS\\Driver

[HKEY_LOCAL_MACHINE\\NEWOS\\POLICY]
(Predefinito)=REG_SZ:(valore non impostato)
AppInstallPolicy=REG_SZ:UserAppsMustInstallToProgramFile
SystemAppPolicy=REG_SZ:SystemToolsMustResideInSystemProgram
RegistryEncryption=REG_SZ:AES-256-CBC-Encrypted
AccessConstraint=REG_SZ:ClearTextRestrictedToRegedit
LogonPrivilege=REG_SZ:ReadOnlyPreDesktop
RulesAccessPolicy=REG_SZ:DesktopOnlyOrWinApp

[HKEY_LOCAL_MACHINE\\NEWOS\\INSTALLER]
(Predefinito)=REG_SZ:(valore non impostato)
ProgramFilesDir=REG_SZ:C:\\ProgramFile
DefaultInstallDir=REG_SZ:C:\\ProgramFile
SystemProgramDir=REG_SZ:C:\\NewOS\\SystemProgram
RulesPath=REG_SZ:C:\\NewOS\\SystemProgram\\RULES.EXE
DevInclude=REG_SZ:C:\\NewOS\\Dev\\TCC\\INCLUDE\\
TccPath=REG_SZ:C:\\NewOS\\Dev\\TCC\\TCC.EXE
CmdPath=REG_SZ:C:\\NewOS\\SystemProgram\\CMD.EXE
NotepadPath=REG_SZ:C:\\NewOS\\SystemProgram\\NOTEPAD.EXE
CalcPath=REG_SZ:C:\\NewOS\\SystemProgram\\CALC.EXE

[HKEY_CURRENT_USER\\DESKTOP]
(Predefinito)=REG_SZ:(valore non impostato)
Wallpaper=REG_SZ:C:\\NewOS\\Resources\\Wallpapers\\BLISS.BMP
Theme=REG_DWORD:0
IconSpacing=REG_DWORD:72
TaskbarAutoHide=REG_DWORD:0

[HKEY_CURRENT_USER\\NEWOS\\PREFERENCES]
(Predefinito)=REG_SZ:(valore non impostato)
Language=REG_SZ:IT_IT
Sounds=REG_DWORD:1
Resolution=REG_SZ:1024x768

[HKEY_CLASSES_ROOT]
(Predefinito)=REG_SZ:(valore non impostato)
.TXT=REG_SZ:txtfile (NOTEPAD.EXE)
.BMP=REG_SZ:bmpfile (VIEWER.EXE)
.JPG=REG_SZ:jpegfile (VIEWER.EXE)
.WAV=REG_SZ:wavfile (SNDPLAYER.EXE)
.EXE=REG_SZ:exefile (Applicazione)
.NOR=REG_SZ:norfile (RULES.EXE)
.INI=REG_SZ:inifile (Configurazione)
.SYS=REG_SZ:sysfile (Driver di Sistema)
.DAT=REG_SZ:datfile (Database Registro)
`;

    const encryptedRegistryDat = encryptRegistryEnvelope(registryDatContent);
    builder.addFileToDir(systemEntries, 'REGISTRY.DAT', Buffer.from(encryptedRegistryDat, 'utf8'));

    const registryLogContent =
`================================================================================
NewOS V1 - Registro di Sistema: Registro Operazioni e Audit Trail
(C) Newspoint Software - NewSoft - NewOS V1
File: C:\\SYSTEM\\REGISTRY.LOG
================================================================================
[2026-09-29 12:00:00] [SISTEMA] Inizializzazione database di registro riuscita.
[2026-09-29 12:00:00] [AUDIT] Caricati 5 rami radice (HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER, HKEY_CLASSES_ROOT).
`;
    builder.addFileToDir(systemEntries, 'REGISTRY.LOG', Buffer.from(registryLogContent, 'utf8'));

    // Sottocartella Driver Modulari: C:\SYSTEM\DRIVERS\ ([driver].SYS + [driver].INI)
    const driverEntries = [];

    function createDriverSys(name, typeId, ioBase, irq) {
        const buf = Buffer.alloc(1024, 0);
        // MZ Header
        buf[0] = 0x4D; buf[1] = 0x5A;
        buf.writeUInt32LE(0x80, 0x3C);
        // PE Header
        buf[0x80] = 0x50; buf[0x81] = 0x45;
        buf.writeUInt16LE(0x8664, 0x84); // Machine: x86_64
        buf.writeUInt16LE(2, 0x86);      // 2 sections (.text, .data)
        buf.writeUInt32LE(0x20260929, 0x88);
        buf.writeUInt16LE(0x00F0, 0x94);
        buf.writeUInt16LE(0x2000, 0x96); // Characteristics: DLL / Driver

        // Optional Header (PE32+ 64-bit)
        buf.writeUInt16LE(0x020B, 0x98); // PE32+
        buf.writeUInt32LE(0x00000200, 0x98 + 16); // EntryPoint
        buf.writeBigUInt64LE(0x0000000140000000n, 0x98 + 24);
        buf.writeUInt16LE(0x0001, 0x98 + 68); // Subsystem: IMAGE_SUBSYSTEM_NATIVE (Device Driver)

        // Section .text
        buf.write('.text\0\0\0', 0x180, 8, 'ascii');
        buf.writeUInt32LE(0x0200, 0x188);
        buf.writeUInt32LE(0x1000, 0x18C);
        buf.writeUInt32LE(0x0200, 0x190);
        buf.writeUInt32LE(0x0200, 0x194);
        buf.writeUInt32LE(0x60000020, 0x19C);

        // Section .data
        buf.write('.data\0\0\0', 0x1A8, 8, 'ascii');
        buf.writeUInt32LE(0x0200, 0x1B0);
        buf.writeUInt32LE(0x2000, 0x1B4);
        buf.writeUInt32LE(0x0200, 0x1B8);
        buf.writeUInt32LE(0x0400, 0x1BC);
        buf.writeUInt32LE(0xC0000040, 0x1C4);

        // Driver Header Signature: "SYS1" at 0x200
        buf.writeUInt32LE(0x53595331, 0x200);
        buf.write(name.padEnd(32, '\0'), 0x204, 32, 'ascii');
        buf.writeUInt32LE(typeId, 0x224);
        buf.writeUInt16LE(ioBase, 0x228);
        buf.writeUInt8(irq, 0x22A);
        buf.writeUInt8(1, 0x22B); // Status: Active

        // Entry point machine code (xor eax, eax; ret)
        buf[0x240] = 0x31; buf[0x241] = 0xC0; buf[0x242] = 0xC3;
        return buf;
    }

    const driversList = [
        {
            file: 'PCI',
            typeId: 0,
            io: 0x0CF8,
            irq: 0,
            dma: 0,
            name: 'NewSoft PCI Bus Enumerator',
            type: 'Bus',
            vendor: '0x0000',
            device: '0x0000',
            settings: 'ScanMode=Fast\nMaxBuses=256\nDeviceCount=32'
        },
        {
            file: 'ATA',
            typeId: 1,
            io: 0x01F0,
            irq: 14,
            dma: 0,
            name: 'NewSoft IDE/ATA Storage Controller',
            type: 'Storage',
            vendor: '0x8086',
            device: '0x7010',
            settings: 'TransferMode=PIO4\nDriveCount=2\nSectorSize=512\nCacheEnabled=1'
        },
        {
            file: 'VGA',
            typeId: 2,
            io: 0x01CE,
            irq: 0,
            dma: 0,
            name: 'NewSoft Advanced VGA & BGA Dynamic Video Driver',
            type: 'Display',
            vendor: '0x1234',
            device: '0x1111',
            settings: 'DefaultResolution=1024x768\nSupportedModes=800x600,1024x768,1280x720,1280x1024,1440x900,1920x1080\nBPP=32\nAcceleration=BGA\nDoubleBuffer=1'
        },
        {
            file: 'AC97',
            typeId: 3,
            io: 0x1000,
            irq: 11,
            dma: 1,
            name: 'NewSoft AC97 Audio Subsystem',
            type: 'Audio',
            vendor: '0x8086',
            device: '0x2415',
            settings: 'SampleRate=48000\nChannels=2\nVolume=85\nMute=0'
        },
        {
            file: 'PS2MOUSE',
            typeId: 4,
            io: 0x0060,
            irq: 12,
            dma: 0,
            name: 'NewSoft PS/2 Mouse Driver',
            type: 'Mouse',
            vendor: '0x0000',
            device: '0x0002',
            settings: 'Sensitivity=3\nAcceleration=Normal\nScrollWheel=1'
        },
        {
            file: 'PS2KBD',
            typeId: 5,
            io: 0x0060,
            irq: 1,
            dma: 0,
            name: 'NewSoft PS/2 Keyboard Driver',
            type: 'Keyboard',
            vendor: '0x0000',
            device: '0x0001',
            settings: 'Layout=IT\nRepeatDelay=500\nRepeatRate=30'
        },
        {
            file: 'RTC',
            typeId: 6,
            io: 0x0070,
            irq: 8,
            dma: 0,
            name: 'NewSoft Real-Time Clock & CMOS Subsystem',
            type: 'Timer',
            vendor: '0x0000',
            device: '0x0004',
            settings: 'Format=24h\nTimeZone=Europe/Rome\nNTP=0'
        },
        {
            file: 'POWER',
            typeId: 7,
            io: 0x0604,
            irq: 9,
            dma: 0,
            name: 'NewSoft Power Management Subsystem',
            type: 'Power',
            vendor: '0x0000',
            device: '0x0005',
            settings: 'APM=Enabled\nACPI=Enabled\nSleepTimeout=15\nDisplayStandby=10'
        },
        {
            file: 'E1000',
            ext: 'SYS',
            typeId: 8,
            io: 0x0000,
            irq: 11,
            dma: 0,
            name: 'Intel PRO/1000 MT Gigabit Network Adapter',
            type: 'Network',
            vendor: '0x8086',
            device: '0x100E',
            settings: 'LinkSpeed=1000Mbps\nDuplex=Full\nFlowControl=1\nRxRing=32\nTxRing=16'
        },
        {
            file: 'RTL8139',
            ext: 'SYS',
            typeId: 8,
            io: 0xC000,
            irq: 10,
            dma: 0,
            name: 'Realtek RTL8139 Fast Ethernet Subsystem',
            type: 'Network',
            vendor: '0x10EC',
            device: '0x8139',
            settings: 'LinkSpeed=100Mbps\nDuplex=Full\nRxBufferSize=8192'
        },
        {
            file: 'ETH',
            ext: 'SYS.NET',
            typeId: 8,
            io: 0x0000,
            irq: 0,
            dma: 0,
            name: 'Ethernet II & ARP Framing Engine',
            type: 'Network',
            vendor: '0x0000',
            device: '0x0010',
            settings: 'MTU=1500\nFraming=DIX_II\nArpCacheSize=16\nArpTimeout=300'
        },
        {
            file: 'IP',
            ext: 'SYS.NET',
            typeId: 8,
            io: 0x0000,
            irq: 0,
            dma: 0,
            name: 'IPv4 & ICMP Network Layer Subsystem',
            type: 'Network',
            vendor: '0x0000',
            device: '0x0011',
            settings: 'Version=4\nTTL=64\nFragmentation=Disabled\nIcmpEcho=Enabled'
        },
        {
            file: 'UDP',
            ext: 'SYS.NET',
            typeId: 8,
            io: 0x0000,
            irq: 0,
            dma: 0,
            name: 'UDP Transport Protocol Subsystem',
            type: 'Network',
            vendor: '0x0000',
            device: '0x0012',
            settings: 'MaxSockets=16\nChecksum=RFC768\nEphemeralStart=49152'
        },
        {
            file: 'TCP',
            ext: 'SYS.NET',
            typeId: 8,
            io: 0x0000,
            irq: 0,
            dma: 0,
            name: 'TCP Reliable Stream Protocol Engine',
            type: 'Network',
            vendor: '0x0000',
            device: '0x0013',
            settings: 'WindowSize=4096\nHandshake=3Way\nCongestionControl=Reno\nKeepAlive=60'
        },
        {
            file: 'DHCP',
            ext: 'SYS.NET',
            typeId: 8,
            io: 0x0000,
            irq: 0,
            dma: 0,
            name: 'Dynamic Host Configuration Client',
            type: 'Network',
            vendor: '0x0000',
            device: '0x0014',
            settings: 'ClientPort=68\nServerPort=67\nBroadcastFlag=1\nTimeout=3'
        },
        {
            file: 'TLS',
            ext: 'SYS.NET',
            typeId: 8,
            io: 0x0000,
            irq: 0,
            dma: 0,
            name: 'TLS Security & Cryptographic Substrate',
            type: 'Network',
            vendor: '0x0000',
            device: '0x0015',
            settings: 'Cipher=SHA256_HMAC\nTLS_Version=1.3\nEntropy=Hardware'
        }
    ];

    const netDriverEntries = [];

    for (const d of driversList) {
        const fileExt = d.ext || 'SYS';
        const fullFilename = `${d.file}.${fileExt}`;
        const sysBuf = createDriverSys(fullFilename, d.typeId, d.io, d.irq);
        builder.addFileToDir(driverEntries, fullFilename, sysBuf);

        // Descrittore .INI
        const iniContent =
`[Driver]
Name=${d.name}
Version=1.0.0
Author=Newspoint Software - NewSoft
Type=${d.type}
Filename=${fullFilename}
VendorID=${d.vendor}
DeviceID=${d.device}
AutoStart=1
Status=Active

[Hardware]
IRQ=${d.irq}
IO_Base=0x${d.io.toString(16).toUpperCase().padStart(4, '0')}
DMA=${d.dma}

[Settings]
${d.settings}
`;
        builder.addFileToDir(driverEntries, `${d.file}.INI`, Buffer.from(iniContent, 'utf8'));

        // Se è un driver di rete, inseriscilo anche nella sottocartella NET
        if (d.type === 'Network') {
            builder.addFileToDir(netDriverEntries, fullFilename, sysBuf);
            builder.addFileToDir(netDriverEntries, `${d.file}.INI`, Buffer.from(iniContent, 'utf8'));
        }
    }

    const netReadme =
`================================================================
NewOS V1 - Sottosistema Driver e Protocolli di Rete (NET\\)
(C) Newspoint Software - NewSoft - NewOS V1
================================================================

Driver Scheda di Rete Fisico:
- E1000.SYS / E1000.INI      : Intel PRO/1000 MT Gigabit Ethernet
- RTL8139.SYS / RTL8139.INI  : Realtek RTL8139 Fast Ethernet

Moduli Protocollo Dedicati:
- ETH.SYS.NET / ETH.INI      : Framing Ethernet II e ARP
- IP.SYS.NET / IP.INI        : IPv4 e risposta ICMP Echo (Ping)
- UDP.SYS.NET / UDP.INI      : Datagrammi UDP
- TCP.SYS.NET / TCP.INI      : Stream affidabile TCP e handshake a 3 vie
- DHCP.SYS.NET / DHCP.INI    : Negoziazione automatica con il router
- TLS.SYS.NET / TLS.INI      : Crittografia SHA256/HMAC e record sicuri
`;
    builder.addFileToDir(netDriverEntries, 'LEGGIMI.TXT', Buffer.from(netReadme, 'utf8'));
    builder.addDirToDir(driverEntries, 'NET', netDriverEntries);

    const driversReadme =
`================================================================
NewOS V1 - Sottosistema Modulare dei Driver (C:\\SYSTEM\\DRIVERS\\)
(C) Newspoint Software - NewSoft - NewOS V1
================================================================

In NewOS V1 ogni componente hardware e software e' gestito da una coppia:
1. [driver].SYS : Modulo binario compilato x86_64 PE (Native Subsystem).
2. [driver].INI : File di configurazione e risorse (IRQ, I/O Base, parametri).

Driver caricati e attivi:
- PCI.SYS / PCI.INI          : Bus PCI ed enumerazione periferiche
- ATA.SYS / ATA.INI          : Controller Hard Disk e CD-ROM IDE/ATA
- VGA.SYS / VGA.INI          : Sottosistema video VESA/VBE e BGA
- AC97.SYS / AC97.INI        : Driver audio Intel AC'97 e mixer
- PS2MOUSE.SYS / PS2MOUSE.INI: Mouse PS/2 e puntatore grafico
- PS2KBD.SYS / PS2KBD.INI    : Tastiera PS/2 e codici di scansione
- RTC.SYS / RTC.INI          : Orologio CMOS e data/ora di sistema
- POWER.SYS / POWER.INI      : Gestione alimentazione ACPI/APM e risparmio
- NET\\                       : Sottosistema di rete e protocolli (E1000, RTL8139, ETH, IP, UDP, TCP, DHCP, TLS)
`;
    builder.addFileToDir(driverEntries, 'LEGGIMI.TXT', Buffer.from(driversReadme, 'utf8'));
    builder.addDirToDir(systemEntries, 'DRIVERS', driverEntries);

    // Sottocartella Schermate di Sistema e Moduli Protetti: C:\SYSTEM\SCREENS\
    const screenEntries = [];
    const binDir = path.join(rootDir, 'build', 'bin');

    const screensList = [
        { mod: 'BOOTSCR.MOD', ini: 'BOOTSCR.INI', srcDir: 'bootscreen' },
        { mod: 'LOGIN.MOD', ini: 'LOGIN.INI', srcDir: 'loginscreen' },
        { mod: 'LOGOUT.MOD', ini: 'LOGOUT.INI', srcDir: 'logoutscreen' }
    ];

    for (const s of screensList) {
        const binModPath = path.join(binDir, s.mod);
        if (fs.existsSync(binModPath)) {
            builder.addFileToDir(screenEntries, s.mod, fs.readFileSync(binModPath));
        }
        const iniPath = path.join(binDir, s.ini);
        if (fs.existsSync(iniPath)) {
            builder.addFileToDir(screenEntries, s.ini, fs.readFileSync(iniPath));
        } else {
            const rootIni = path.join(rootDir, s.srcDir, `${s.srcDir}.ini`);
            if (fs.existsSync(rootIni)) {
                builder.addFileToDir(screenEntries, s.ini, fs.readFileSync(rootIni));
            }
        }
    }

    // Aggiungi l'immagine grafica ufficiale inserita dall'utente (bootScreen.jpeg / 1024 crop)
    const bootJpg = path.join(rootDir, 'bootscreen', 'bootScreen_1024.jpg');
    if (fs.existsSync(bootJpg)) {
        builder.addFileToDir(screenEntries, 'BOOTSCR.JPG', fs.readFileSync(bootJpg));
    }
    const bootBmp = path.join(rootDir, 'bootscreen', 'bootScreen_1024.bmp');
    if (fs.existsSync(bootBmp)) {
        builder.addFileToDir(screenEntries, 'BOOTSCR.BMP', fs.readFileSync(bootBmp));
    }
    const origJpeg = path.join(rootDir, 'bootscreen', 'bootScreen.jpeg');
    if (fs.existsSync(origJpeg)) {
        builder.addFileToDir(screenEntries, 'BOOTSCR.ORIG.JPG', fs.readFileSync(origJpeg));
    }

    // Aggiungi immagini ufficiali per Loginscreen e Logoutscreen inserite dall'utente
    const loginJpg = path.join(rootDir, 'loginscreen', 'loginscreen_1024.jpg');
    if (fs.existsSync(loginJpg)) {
        builder.addFileToDir(screenEntries, 'LOGIN.JPG', fs.readFileSync(loginJpg));
    }
    const loginBmp = path.join(rootDir, 'loginscreen', 'loginscreen_1024.bmp');
    if (fs.existsSync(loginBmp)) {
        builder.addFileToDir(screenEntries, 'LOGIN.BMP', fs.readFileSync(loginBmp));
    }

    const logoutJpg = path.join(rootDir, 'logoutscreen', 'logoutscreen_1024.jpg');
    if (fs.existsSync(logoutJpg)) {
        builder.addFileToDir(screenEntries, 'LOGOUT.JPG', fs.readFileSync(logoutJpg));
    }
    const logoutBmp = path.join(rootDir, 'logoutscreen', 'logoutscreen_1024.bmp');
    if (fs.existsSync(logoutBmp)) {
        builder.addFileToDir(screenEntries, 'LOGOUT.BMP', fs.readFileSync(logoutBmp));
    }

    const screensReadme =
`================================================================
NewOS V1 - Moduli Schermata Protetti di Sistema (C:\\SYSTEM\\SCREENS\\)
(C) Newspoint Software - NewSoft - NewOS V1
================================================================

I moduli in questa cartella sono binari protetti x86_64 a 64-bit:
- BOOTSCR.MOD / BOOTSCR.INI  : Schermata di avvio e caricamento con animazione
- LOGIN.MOD / LOGIN.INI      : Schermata di autenticazione e accesso utente
- LOGOUT.MOD / LOGOUT.INI    : Schermata di disconnessione e arresto sistema
- BOOTSCR.JPG / BOOTSCR.BMP  : Logo e grafica ufficiale NewOS V1

Ogni modulo e' firmato digitalmente e protetto da verifica CRC32
prima dell'esecuzione in Ring 0.
`;
    builder.addFileToDir(screenEntries, 'LEGGIMI.TXT', Buffer.from(screensReadme, 'utf8'));
    builder.addDirToDir(systemEntries, 'SCREENS', screenEntries);

    // Sottocartella Ambiente di Sviluppo: C:\NewOS\Dev\TCC\, CONFIG\, TEMP\ e TMP\
    const devEntries = [];
    const tccEntries = [];
    const tccConfigEntries = [];

    const tccCfgContent =
`[Compiler]
Target=x86_64-newos
Optimization=O2
Warnings=All
SysInclude=C:\\NewOS\\Dev\\TCC\\INCLUDE\\
SysLib=C:\\NewOS\\Dev\\TCC\\LIB\\
TempDir=C:\\NewOS\\Dev\\TCC\\TEMP\\
TmpDir=C:\\NewOS\\Dev\\TCC\\TMP\\
DefaultOutput=C:\\APPS\\
`;
    builder.addFileToDir(tccConfigEntries, 'TCC.CFG', Buffer.from(tccCfgContent, 'utf8'));
    builder.addDirToDir(tccEntries, 'CONFIG', tccConfigEntries);

    // Cartelle temporanee per file intermedi (.o, .s, preprocessed)
    const tccTempEntries = [];
    builder.addFileToDir(tccTempEntries, 'TEMP.TXT', Buffer.from("Cartella per file temporanei e oggetti intermedi di compilazione TCC.\n", 'utf8'));
    builder.addDirToDir(tccEntries, 'TEMP', tccTempEntries);

    const tccTmpEntries = [];
    builder.addFileToDir(tccTmpEntries, 'TMP.TXT', Buffer.from("Directory temporanea TMP per il compilatore TCC.\n", 'utf8'));
    builder.addDirToDir(tccEntries, 'TMP', tccTmpEntries);

    // Stub eseguibile TCC.EXE
    builder.addFileToDir(tccEntries, 'TCC.EXE', peStub);

    const tccIncludeEntries = [];
    const newosHStub =
`/**
 * newos.h - NewOS V1 Software Development Kit (SDK)
 * Copyright (C) Newspoint Software - NewSoft
 */
#ifndef _NEWOS_H_
#define _NEWOS_H_

typedef unsigned long long uint64_t;
typedef unsigned int       uint32_t;
typedef unsigned short     uint16_t;
typedef unsigned char      uint8_t;

// API System Call & GUI Entrypoints
void sys_print(const char* str);
void sys_exit(int code);

#endif // _NEWOS_H_
`;
    builder.addFileToDir(tccIncludeEntries, 'NEWOS.H', Buffer.from(newosHStub, 'utf8'));
    builder.addDirToDir(tccEntries, 'INCLUDE', tccIncludeEntries);

    const devReadme = "Ambiente di sviluppo nativo Tiny C Compiler (TCC 64-bit) per NewOS.\nConfigurazione: C:\\NewOS\\Dev\\TCC\\CONFIG\\TCC.CFG\nDirectory temporanee: C:\\NewOS\\Dev\\TCC\\TEMP\\ e TMP\\\n";
    builder.addFileToDir(tccEntries, 'LEGGIMI.TXT', Buffer.from(devReadme, 'utf8'));

    builder.addDirToDir(devEntries, 'TCC', tccEntries);
    builder.addFileToDir(devEntries, 'TCC.EXE', peStub);
    builder.addDirToDir(devEntries, 'CONFIG', tccConfigEntries);
    builder.addDirToDir(devEntries, 'INCLUDE', tccIncludeEntries);
    builder.addDirToDir(devEntries, 'TEMP', tccTempEntries);
    builder.addDirToDir(devEntries, 'TMP', tccTmpEntries);
    builder.addFileToDir(devEntries, 'LEGGIMI.TXT', Buffer.from("Compilatore C nativo Tiny C Compiler (TCC 64-bit) per NewOS V1 (C:\\NewOS\\Dev\\)\n", 'utf8'));
    builder.addDirToDir(systemEntries, 'DEV', devEntries);

    builder.addDirToDir(rootEntries, 'SYSTEM', systemEntries);

    // ==============================================================================
    // STRUTTURA UFFICIALE NewOS V1 (C:\NewOS, C:\User, C:\ProgramFile)
    // ==============================================================================
    const newosEntries = [];

    // 1. C:\NewOS\Driver (con sottocartella NET)
    builder.addDirToDir(newosEntries, 'Driver', driverEntries);

    // 2. C:\NewOS\Resources e alias C:\NewOS\Resource
    const resourceEntries = [];
    builder.addDirToDir(resourceEntries, 'Wallpapers', wallEntries);
    builder.addDirToDir(resourceEntries, 'wallpapers', wallEntries);

    const cursorEntries = [];
    builder.addFileToDir(cursorEntries, 'ARROW.CUR', Buffer.from('NewOS Cursor Data\n'));
    builder.addFileToDir(cursorEntries, 'BEAM.CUR', Buffer.from('NewOS Cursor Data\n'));
    builder.addFileToDir(cursorEntries, 'WAIT.CUR', Buffer.from('NewOS Cursor Data\n'));
    builder.addFileToDir(cursorEntries, 'HAND.CUR', Buffer.from('NewOS Cursor Data\n'));
    builder.addDirToDir(resourceEntries, 'cursor', cursorEntries);

    builder.addDirToDir(resourceEntries, 'Sounds', soundEntries);
    builder.addDirToDir(resourceEntries, 'Screens', screenEntries);

    const iconResEntries = [];
    if (iconsPath && fs.existsSync(iconsPath)) {
        builder.addFileToDir(iconResEntries, 'ICONS.BIN', fs.readFileSync(iconsPath));
    }
    builder.addDirToDir(resourceEntries, 'Icons', iconResEntries);

    builder.addDirToDir(newosEntries, 'Resources', resourceEntries);
    builder.addDirToDir(newosEntries, 'Resource', resourceEntries);

    // 3. C:\NewOS\Font
    const fontEntries = [];
    builder.addFileToDir(fontEntries, 'SYSTEM.FNT', Buffer.from('NewOS V1 System Bitmap Font 8x16\n'));
    builder.addFileToDir(fontEntries, 'VGA.FNT', Buffer.from('NewOS V1 Standard VGA Font 8x8\n'));
    builder.addFileToDir(fontEntries, 'CONSOLE.FNT', Buffer.from('NewOS V1 Terminal Console Font 8x16\n'));
    builder.addFileToDir(fontEntries, 'LEGGIMI.TXT', Buffer.from('Font di sistema NewOS V1 (C:\\NewOS\\Font\\)\n'));
    builder.addDirToDir(newosEntries, 'Font', fontEntries);

    // 4. C:\NewOS\Help
    const helpEntries = [];
    builder.addFileToDir(helpEntries, 'MANUALE.TXT', Buffer.from('Manuale Utente NewOS V1 64-bit\n'));
    builder.addFileToDir(helpEntries, 'COMANDI.TXT', Buffer.from('Comandi Shell: help, dir, cd, type, cls, ver, echo, reboot, shutdown, regedit, tcc\n'));
    builder.addFileToDir(helpEntries, 'GUIDA.HLP', Buffer.from('Guida e Aiuto Rapido\n'));
    builder.addFileToDir(helpEntries, 'FAQ.TXT', Buffer.from('Domande frequenti e risoluzione problemi\n'));
    builder.addFileToDir(helpEntries, 'LEGGIMI.TXT', Buffer.from('Documentazione e aiuto (C:\\NewOS\\Help\\)\n'));
    builder.addDirToDir(newosEntries, 'Help', helpEntries);

    // 5. C:\NewOS\Diagnostic
    const diagEntries = [];
    builder.addFileToDir(diagEntries, 'MEMTEST.EXE', peStub);
    builder.addFileToDir(diagEntries, 'NETDIAG.EXE', peStub);
    builder.addFileToDir(diagEntries, 'SYSINFO.TXT', Buffer.from('NewOS V1 Telemetry: CPU x86_64, VBE 1024x768x32bpp, RAM 256MB+\n'));
    builder.addFileToDir(diagEntries, 'DIAG.LOG', Buffer.from('[DIAGNOSTIC] Verifica hardware completata con successo.\n'));
    builder.addDirToDir(newosEntries, 'Diagnostic', diagEntries);

    // 6. C:\NewOS\Themes
    const themeEntries = [];
    const themesIni = `[Themes]\nCurrent=Classic\nAvailable=Classic,LiquidGlass,DarkNavy\n\n[Classic]\nName=Windows 2000 Classic\nDesktopColor=0x008080\nActiveTitle=0x0A246A\n\n[LiquidGlass]\nName=Liquid Glass Modern\nDesktopColor=0x0B192C\nActiveTitle=0x1E3E62\n\n[DarkNavy]\nName=Dark Navy Blue\nDesktopColor=0x050C1A\nActiveTitle=0x18345C\n`;
    builder.addFileToDir(themeEntries, 'THEMES.INI', Buffer.from(themesIni, 'utf8'));
    builder.addFileToDir(themeEntries, 'CLASSIC.THM', Buffer.from('Theme=Classic\nStyle=Win2000\n'));
    builder.addFileToDir(themeEntries, 'LIQUID_GLASS.THM', Buffer.from('Theme=LiquidGlass\nStyle=GlassModern\n'));
    builder.addFileToDir(themeEntries, 'DARK_NAVY.THM', Buffer.from('Theme=DarkNavy\nStyle=DeepNavy\n'));
    builder.addDirToDir(newosEntries, 'Themes', themeEntries);

    // 7. C:\NewOS\Language
    const langEntries = [];
    builder.addFileToDir(langEntries, 'LANGUAGE.INI', Buffer.from('[Language]\nActive=IT_IT\nDefault=IT_IT\nAvailable=IT_IT,EN_US\n'));
    builder.addFileToDir(langEntries, 'IT_IT.LNG', Buffer.from('[Strings]\nAppName=NewOS V1 Edizione 64-bit\nLoginTitle=Accesso a NewOS V1\n'));
    builder.addFileToDir(langEntries, 'EN_US.LNG', Buffer.from('[Strings]\nAppName=NewOS V1 64-bit Edition\nLoginTitle=Logon to NewOS V1\n'));
    builder.addDirToDir(newosEntries, 'Language', langEntries);

    // 8. C:\NewOS\SystemProgram
    const sysProgEntries = [];
    for (const e of screenEntries) {
        sysProgEntries.push(e);
    }
    builder.addFileToDir(sysProgEntries, 'REGEDIT.EXE', peStub);
    builder.addFileToDir(sysProgEntries, 'TASKMGR.EXE', peStub);
    builder.addFileToDir(sysProgEntries, 'EXPLORER.EXE', peStub);
    builder.addFileToDir(sysProgEntries, 'CMD.EXE', peStub);
    builder.addFileToDir(sysProgEntries, 'CALC.EXE', peStub);
    builder.addFileToDir(sysProgEntries, 'NOTEPAD.EXE', peStub);
    builder.addFileToDir(sysProgEntries, 'RULES.EXE', peStub);
    builder.addFileToDir(sysProgEntries, 'DEVMGMT.CPL', peStub);
    builder.addFileToDir(sysProgEntries, 'POWER.CPL', peStub);
    builder.addFileToDir(sysProgEntries, 'SOUND.CPL', peStub);
    builder.addFileToDir(sysProgEntries, 'DISPLAY.CPL', peStub);
    builder.addDirToDir(newosEntries, 'SystemProgram', sysProgEntries);

    // 9. C:\NewOS\OOBE
    const oobeEntries = [];
    builder.addFileToDir(oobeEntries, 'OOBE.INI', Buffer.from('[OOBE]\nCompleted=0\nLanguage=IT_IT\nDefaultUser=Morgan\nAllowSkip=1\n'));
    builder.addFileToDir(oobeEntries, 'SETUP.EXE', peStub);
    builder.addFileToDir(oobeEntries, 'WELCOME.TXT', Buffer.from('Benvenuto in NewOS V1 OOBE Setup\n'));
    builder.addDirToDir(newosEntries, 'OOBE', oobeEntries);

    // 9b. C:\NewOS\Registry (Database cifrato AES-256 REGISTRY.DAT e REGISTRY.LOG)
    const regEntries = [];
    builder.addFileToDir(regEntries, 'REGISTRY.DAT', Buffer.from(encryptedRegistryDat, 'utf8'));
    builder.addFileToDir(regEntries, 'REGISTRY.LOG', Buffer.from(
`[2026-10-18 12:00:00] [AUDIT] Inizializzazione registro NewOS V1 (C:\\NewOS\\Registry\\REGISTRY.DAT) completata.
[2026-10-18 12:00:00] [SICUREZZA] Crittografia AES-256 CBC attiva. Accesso in chiaro vincolato all'app REGEDIT.EXE.
[2026-10-18 12:00:00] [SICUREZZA] Permessi Logon pre-desktop: SOLA LETTURA. Modifiche consentite solo in ambiente Desktop.
`, 'utf8'));
    builder.addDirToDir(newosEntries, 'Registry', regEntries);

    // 9c. C:\NewOS\Rules (Regole Ufficiali di Sistema NewOS V1)
    const rulesEntries = [];
    const rule01Text = `================================================================================
REGOLE DI SISTEMA NEWOS V1 - 01: GERARCHIA DEL FILE SYSTEM
================================================================================
1. RADICE C:\\:
   - C:\\NewOS             : Directory principale del sistema operativo.
   - C:\\ProgramFile       : Riservata rigorosamente alle applicazioni terze parti.
   - C:\\User              : Profili utente e cartelle Desktop/Documenti.
   - C:\\BootSetup         : Moduli di installazione e recovery hardware.

2. STRUTTURA C:\\NewOS:
   - C:\\NewOS\\SystemProgram : Tutti i programmi e applet di sistema nativi:
                              CMD.EXE, CALC.EXE, NOTEPAD.EXE, EXPLORER.EXE,
                              REGEDIT.EXE, TASKMGR.EXE, applet .CPL e moduli .MOD.
   - C:\\NewOS\\Registry      : Database cifrato del registro (REGISTRY.DAT).
   - C:\\NewOS\\Rules         : Regole architetturali vincolanti per il sistema.
   - C:\\NewOS\\Dev           : Ambiente di sviluppo nativo TCC (Tiny C Compiler).
   - C:\\NewOS\\Driver        : Driver hardware (.SYS e .INI).
   - C:\\NewOS\\Resources     : Sfondi, suoni, schermate grafiche, icone.
   - C:\\NewOS\\Font          : Tipi di carattere bitmap nativi.
   - C:\\NewOS\\Help          : Manuali, FAQ e documentazione di bordo.
   - C:\\NewOS\\Diagnostic    : Utility diagnostiche RAM, disco e rete.
   - C:\\NewOS\\Themes        : Temi visivi (Classic Win2000, Liquid Glass).
   - C:\\NewOS\\Language      : Tabelle di localizzazione e pacchetti linguistici.
   - C:\\NewOS\\OOBE          : Setup interattivo Out-of-Box Experience.
`;
    const rule02Text = `================================================================================
REGOLE DI SISTEMA NEWOS V1 - 02: SICUREZZA E ARCHITETTURA DEL REGISTRO
================================================================================
1. SCHEMA INTERNO NATIVO:
   - Il registro e' al 100% nativo di NewOS V1. E' rigorosamente vietato
     l'utilizzo di rami estranei o incompatibili quali 'Microsoft\\Windows'.
   - Struttura gerarchica:
       [HKEY_LOCAL_MACHINE\\NEWOS\\SYSTEM]
       [HKEY_LOCAL_MACHINE\\NEWOS\\POLICY]
       [HKEY_LOCAL_MACHINE\\NEWOS\\INSTALLER]
       [HKEY_CURRENT_USER\\NEWOS\\PREFERENCES]
       [HKEY_CURRENT_USER\\DESKTOP]
       [HKEY_CLASSES_ROOT]

2. CIFRATURA AES-256 A RIPOSO:
   - Il file C:\\NewOS\\Registry\\REGISTRY.DAT risiede su disco protetto da
     cifratura standard Rijndael AES-256 CBC con padding PKCS#7.
   - Se aperto manualmente (ad es. da Blocco Note o tramite comando 'type' da shell),
     il file mostra esclusivamente l'envelope di sicurezza e il testo cifrato in hex:
       [NEWOS-V1-ENCRYPTED-REGISTRY]
       AVVISO=Accesso in chiaro vincolato all'applicazione di sistema Registry.
   - L'accesso in chiaro e' consentito unicamente all'applicazione REGEDIT.EXE
     e al sottosistema kernel autorizzato.

3. PRIVILEGI DI ACCESSO E TRANSIZIONE STATO:
   - Fase di Logon / Pre-Desktop (post inserimento credenziali):
       * Privilegi di SOLA LETTURA rigorosamente attivi (registry_set_read_only(true)).
       * Il sistema interroga il registro per caricare sfondo, tema, lingua e policy.
       * Qualsiasi tentativo di scrittura o modifica viene intercettato, bloccato
         e registrato con audit 'ACCESSO_NEGATO'.
   - Ambiente Desktop:
       * Una volta completato il login e avviato il Window Manager (wm_init),
         vengono sbloccati i privilegi di modifica (registry_set_read_only(false)).
       * Le modifiche sono consentite solo in ambiente desktop tramite REGEDIT.EXE
         o le applet del Pannello di Controllo.
`;
    const rule03Text = `================================================================================
REGOLE DI SISTEMA NEWOS V1 - 03: POLICY INSTALLATORI E SOFTWARE TERZE PARTI
================================================================================
1. DESTINAZIONE SOFTWARE UTENTE:
   - Tutti gli installer e i pacchetti di installazione devono installare
     i software utente in sottocartelle dedicate dentro:
       -> C:\\ProgramFile\\<NomeApplicazione>\\

2. CHIAVE DI REGISTRO PER GLI INSTALLER:
   - Gli installer devono leggere il percorso predefinito da:
       [HKEY_LOCAL_MACHINE\\NEWOS\\INSTALLER]
       ProgramFilesDir = "C:\\ProgramFile"
       DefaultInstallDir = "C:\\ProgramFile"

3. DIVIETO DI MANOMISSIONE:
   - E' vietata l'installazione di programmi utente in C:\\NewOS\\SystemProgram,
     C:\\NewOS\\Dev o nella radice C:\\.
   - Gli strumenti nativi di sistema sono immutabili e risiedono solo in SystemProgram.
`;
    const rule04Text = `================================================================================
REGOLE DI SISTEMA NEWOS V1 - 04: PRIVILEGI E MACCHINA A STATI DI SESSIONE
================================================================================
1. STATO PRE-DESKTOP (OOBE / LOGON):
   - Accesso al filesystem vincolato ai moduli firmati .MOD.
   - Registro di sistema accessibile in SOLA LETTURA.
   - Blocco totale di chiamate a registry_set_value_*.
   - Il login convalida le credenziali utente e legge la configurazione sessione.

2. STATO DESKTOP ATTIVO:
   - Window Manager e Desktop operativi.
   - Scrittura del registro abilitata per gli utenti autenticati.
   - Tracciamento di tutte le modifiche critiche nell'Audit Trail.
`;
    const rule05Text = `================================================================================
REGOLE DI SISTEMA NEWOS V1 - 05: RETE, DIAGNOSTICA E PRIORITA' SERVER
================================================================================
1. LINK PRIORITARIO TEST RETE:
   - La verifica della connettivita' internet in OOBE e nel gestore di rete
     adotta quale target prioritario il server di test italiano:
       -> IP Primario: 95.141.32.1
   - Gateway locale o DNS rappresentano percorsi di fallback secondari.

2. DRIVER DI RETE:
   - Intel PRO/1000 MT (E1000.SYS) e Realtek RTL8139 (RTL8139.SYS)
     sono gestiti con configurazioni standard in C:\\NewOS\\Driver\\NET\\.
`;
    builder.addFileToDir(rulesEntries, '01_SYSTEM_HIERARCHY.NOR', createNorFile('01_SYSTEM_HIERARCHY', 'Architettura File System', 'CRITICA', rule01Text));
    builder.addFileToDir(rulesEntries, '02_REGISTRY_SECURITY.NOR', createNorFile('02_REGISTRY_SECURITY', 'Sicurezza e Registro', 'MASSIMA', rule02Text));
    builder.addFileToDir(rulesEntries, '03_INSTALL_POLICY.NOR', createNorFile('03_INSTALL_POLICY', 'Policy Installatori', 'ALTA', rule03Text));
    builder.addFileToDir(rulesEntries, '04_USER_PRIVILEGES.NOR', createNorFile('04_USER_PRIVILEGES', 'Privilegi e Sessioni', 'SICUREZZA', rule04Text));
    builder.addFileToDir(rulesEntries, '05_NETWORK_AND_DEVICES.NOR', createNorFile('05_NETWORK_AND_DEVICES', 'Rete e Connettivita\'', 'MEDIA', rule05Text));

    const rulesReadmeText = `================================================================================
CARTELLA DELLE REGOLE UFFICIALI DI SISTEMA (C:\\NewOS\\Rules\\)
================================================================================
I file con estensione .NOR (NewOS Official Rule) sono file protetti e cifrati
proprietari del sistema operativo NewOS V1.

MODALITA' DI ACCESSO:
1. Ambiente NewOS:
   - Accessibili ESCLUSIVAMENTE all'interno dell'ambiente Desktop autenticato
     tramite l'applicazione RULES.EXE ("Regole di Sistema" nel menu Avvio o
     facendo doppio clic sul file .NOR in Risorse del Computer / Explorer).
   - In fase pre-desktop o riga di comando shell il file rifiuta l'apertura grezza.

2. Ambiente Microsoft Windows:
   - Visualizzabili e verificabili tramite l'applicazione dedicata fornita
     nella cartella del pacchetto:
     "Rules Win App\\NewOS_Rules_Viewer.exe" (o script RulesViewer.bat).
================================================================================
`;
    builder.addFileToDir(rulesEntries, 'LEGGIMI.TXT', Buffer.from(rulesReadmeText, 'utf8'));
    builder.addDirToDir(newosEntries, 'Rules', rulesEntries);

    // 10. C:\NewOS\Dev (Ambiente di Sviluppo TCC 64-bit)
    builder.addDirToDir(newosEntries, 'Dev', devEntries);

    // Aggiungi C:\NewOS al root
    builder.addDirToDir(rootEntries, 'NewOS', newosEntries);

    // C:\ProgramFile (Riservata esclusivamente alle applicazioni installate manualmente dall'utente finale)
    const progFileEntries = [];
    const installPolicyText = `================================================================================
NewOS V1 - Linee Guida e Policy di Installazione (C:\\ProgramFile)
================================================================================
Questa cartella e' riservata ESCLUSIVAMENTE alle applicazioni e ai software
installati manualmente dall'utente finale o dai programmi di setup.

STRUTTURA E REGOLE DI SISTEMA:
1. Programmi Nativi di Sistema:
   Tutti gli applicativi di sistema (CMD.EXE, CALC.EXE, NOTEPAD.EXE, EXPLORER.EXE,
   REGEDIT.EXE, TASKMGR.EXE e applet .CPL) risiedono rigorosamente in:
     -> C:\\NewOS\\SystemProgram\\

2. Ambiente di Sviluppo:
   Il compilatore e l'ambiente di sviluppo nativo risiedono in:
     -> C:\\NewOS\\Dev\\

3. Applicazioni Utente (Policy per gli Installer):
   Tutti gli installer software (.EXE / .PKG / .MSI) devono fare riferimento
   alla chiave di registro:
     [HKEY_LOCAL_MACHINE\\NEWOS\\INSTALLER]
     ProgramFilesDir = "C:\\ProgramFile"
     DefaultInstallDir = "C:\\ProgramFile"
   I file dell'applicazione devono essere installati in una sottocartella dedicata:
     -> C:\\ProgramFile\\<NomeApplicazione>\\
`;
    builder.addFileToDir(progFileEntries, 'LEGGIMI.TXT', Buffer.from(installPolicyText, 'utf8'));
    builder.addFileToDir(progFileEntries, 'INSTALL_POLICY.TXT', Buffer.from(installPolicyText, 'utf8'));
    builder.addDirToDir(rootEntries, 'ProgramFile', progFileEntries);

    // 2. Directory APPS (Riservata esclusivamente alle applicazioni utente e terze parti)
    const appEntries = [];
    const appsReadmeText = "Questa cartella e' riservata alle applicazioni e ai programmi utente.\nTutti i tool, gli eseguibili (.EXE) e i pannelli (.CPL) di sistema risiedono in C:\\SYSTEM\\.\n";
    builder.addFileToDir(appEntries, 'LEGGIMI.TXT', Buffer.from(appsReadmeText, 'utf8'));
    builder.addDirToDir(rootEntries, 'APPS', appEntries);

    // 3. Directory DOCS
    const docEntries = [];
    const leggimiText = 
`================================================================
Benvenuto in NewOS V1 (64-bit)!
(C) Newspoint Software - NewSoft - NewOS V1
Sistema Operativo con File System exFAT nativo e interfaccia GUI.
================================================================

Questo file e' memorizzato fisicamente nella partizione exFAT (C:\\).
Struttura delle cartelle di sistema:
- File di sistema, tool (.EXE) e pannelli (.CPL) in C:\\SYSTEM\\
- Sfondi fotografici (.JPG) in C:\\SYSTEM\\WALLPAPERS\\
- Suoni di sistema (.WAV) in C:\\SYSTEM\\SOUNDS\\
- Cartella software utente e terze parti in C:\\APPS\\
- Dati e profilo utente in C:\\USER\\MORGAN\\DESKTOP\\

Caratteristiche:
- Architettura a componenti indipendenti (.SYS, .INI, .CPL, .EXE)
- Tema Classic (Windows 2000) e Liquid Glass (Apple macOS/iOS)
- Driver AC97 audio nativo e altoparlante di sistema
- Gestore File Explorer con navigazione cartelle reale
- Blocco Note, Calcolatrice, Terminale
`;
    builder.addFileToDir(docEntries, 'LEGGIMI.TXT', Buffer.from(leggimiText, 'utf8'));

    const benvenutoText =
`NewOS V1 64-bit Edition
(C) Newspoint Software - NewSoft - NewOS V1
Sviluppato con architettura modulare a file ed exFAT.
Buon divertimento!
`;
    builder.addFileToDir(docEntries, 'BENVENUTO.TXT', Buffer.from(benvenutoText, 'utf8'));
    builder.addDirToDir(rootEntries, 'DOCS', docEntries);

    // 4. Directory USER (Profili Utente e Desktop Personale)
    const userRootEntries = [];
    const morganEntries = [];
    const desktopEntries = [];

    const userNoteText = "Note e appunti personali memorizzati in C:\\USER\\MORGAN\\DESKTOP\\\nFile System: exFAT Microsoft nativo.\n";
    builder.addFileToDir(desktopEntries, 'NOTE.TXT', Buffer.from(userNoteText, 'utf8'));

    const userWelcomeText = "Benvenuto nel tuo Desktop personale di NewOS V1 64-bit!\nPercorso fisico: C:\\USER\\MORGAN\\DESKTOP\\\n";
    builder.addFileToDir(desktopEntries, 'BENVENUTO.TXT', Buffer.from(userWelcomeText, 'utf8'));

    // Sfondo fotografico di esempio sul desktop dell'utente
    const blissBmp = path.join(rootDir, 'resources', 'wallpaper', 'BLISS.BMP');
    if (fs.existsSync(blissBmp)) {
        builder.addFileToDir(desktopEntries, 'SFONDO_BLISS.BMP', fs.readFileSync(blissBmp));
    }

    // Cartella Progetti dentro il desktop utente
    const progettiEntries = [];
    builder.addFileToDir(progettiEntries, 'PROGETTO1.TXT', Buffer.from("Bozza progetto software in C per NewOS V1 64-bit.\n", 'utf8'));
    builder.addDirToDir(desktopEntries, 'PROGETTI', progettiEntries);

    builder.addDirToDir(morganEntries, 'DESKTOP', desktopEntries);

    const userCfgText = "USER=Morgan\nPASS=\nHOST=NEWOS-PC\nTHEME=0\nPOWER=0\nINSTALLED=0\n";
    builder.addFileToDir(morganEntries, 'USER.CFG', Buffer.from(userCfgText, 'utf8'));

    builder.addDirToDir(userRootEntries, 'MORGAN', morganEntries);
    builder.addDirToDir(rootEntries, 'USER', userRootEntries);
    builder.addDirToDir(rootEntries, 'User', userRootEntries);

    // 5. Directory SOURCES (Sorgenti completi del sistema per portabilità assoluta)
    const sourcesEntries = [];
    const srcReadmeText = 
`================================================================
NewOS V1 - Pacchetto Sorgenti e Sviluppo (C:\\SOURCES\\)
(C) Newspoint Software - NewSoft - NewOS V1
================================================================

Questa cartella contiene i sorgenti essenziali del kernel e del bootloader:
- KERNEL.C      : Punto di ingresso principale del kernel 64-bit
- REGEDIT.C     : Tool grafico Editor del Registro di Sistema
- REGISTRY.H    : Header del sottosistema Registro
- NEWOS.H       : API di sistema e tipi fondamentali
- BOOTSECT.ASM  : Codice sorgente NASM del settore di boot MBR

L'albero sorgenti portatile completo e' inoltre disponibile sul
computer host nella cartella 'sources/'.
`;
    builder.addFileToDir(sourcesEntries, 'LEGGIMI.TXT', Buffer.from(srcReadmeText, 'utf8'));

    const kernelCSrc = path.join(rootDir, 'kernel', 'kernel.c');
    if (fs.existsSync(kernelCSrc)) builder.addFileToDir(sourcesEntries, 'KERNEL.C', fs.readFileSync(kernelCSrc));

    const regeditCSrc = path.join(rootDir, 'kernel', 'apps', 'regedit.c');
    if (fs.existsSync(regeditCSrc)) builder.addFileToDir(sourcesEntries, 'REGEDIT.C', fs.readFileSync(regeditCSrc));

    const registryHSrc = path.join(rootDir, 'kernel', 'fs', 'registry.h');
    if (fs.existsSync(registryHSrc)) builder.addFileToDir(sourcesEntries, 'REGISTRY.H', fs.readFileSync(registryHSrc));

    const bootsectAsm = path.join(rootDir, 'boot', 'bootsect.asm');
    if (fs.existsSync(bootsectAsm)) builder.addFileToDir(sourcesEntries, 'BOOTSECT.ASM', fs.readFileSync(bootsectAsm));

    builder.addDirToDir(rootEntries, 'SOURCES', sourcesEntries);

    // BootSetup (Installer ufficiale e contenitore cifrato AES-256 SYSTEM.PAK)
    const bootSetupEntries = [];
    const bsModPath = path.join(rootDir, 'build', 'bin', 'BOOTSETUP.MOD');
    if (fs.existsSync(bsModPath)) {
        builder.addFileToDir(bootSetupEntries, 'BOOTSETUP.MOD', fs.readFileSync(bsModPath));
    }
    const bsIniPath = path.join(rootDir, 'BootSetup', 'bootsetup.ini');
    if (fs.existsSync(bsIniPath)) {
        builder.addFileToDir(bootSetupEntries, 'BOOTSETUP.INI', fs.readFileSync(bsIniPath));
    }
    const sysPakPath = path.join(rootDir, 'build', 'bin', 'BootSetup', 'SYSTEM.PAK');
    const altPakPath = path.join(rootDir, 'build', 'SYSTEM.PAK');
    if (fs.existsSync(sysPakPath)) {
        builder.addFileToDir(bootSetupEntries, 'SYSTEM.PAK', fs.readFileSync(sysPakPath));
    } else if (fs.existsSync(altPakPath)) {
        builder.addFileToDir(bootSetupEntries, 'SYSTEM.PAK', fs.readFileSync(altPakPath));
    }
    builder.addDirToDir(rootEntries, 'BootSetup', bootSetupEntries);

    // Assembla e scrivi dati della Root Directory
    const rootDataBuf = Buffer.concat(rootEntries);
    builder.writeClusterData(builder.rootDirCluster, rootDataBuf);

    // Scrivi Boot Sectors e Checksum
    builder.writeBootSectors();

    console.log(`[exFAT] Volume C:\\ generato con successo: ${builder.allocatedClusters.size} cluster allocati (${builder.allocatedClusters.size * 4} KB usati su ${builder.clusterCount * 4} KB totali).`);
    return builder.volumeBuffer;
}

module.exports = { ExFatBuilder, buildMyOsFileSystem };
