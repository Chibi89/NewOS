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
    createRootSystemEntries() {
        const rootEntries = [];
        const bitmapBytes = Math.ceil(this.clusterCount / 8);

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

    finalize(rootEntries) {
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

        // 3. Scrivi dati della Root Directory concatenata
        const rootDataBuf = Buffer.concat(rootEntries);
        const rootClCount = Math.max(1, Math.ceil(rootDataBuf.length / this.clusterSize));
        if (rootClCount > 1) {
            for (let i = 1; i < rootClCount; i++) {
                const nextCl = this.allocCluster(1);
                this.fatEntries.set(this.rootDirCluster + i - 1, nextCl);
                this.fatEntries.set(nextCl, 0xFFFFFFFF);
            }
        }
        for (let i = 0; i < rootClCount; i++) {
            const chunk = rootDataBuf.subarray(i * this.clusterSize, Math.min(rootDataBuf.length, (i + 1) * this.clusterSize));
            this.writeClusterData(this.rootDirCluster + i, chunk);
        }

        // 4. Scrivi Boot Sectors e Checksum
        this.writeBootSectors();
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

function addDirectoryRecursive(builder, parentEntries, localDirPath, isRoot = false) {
    if (!fs.existsSync(localDirPath)) return;
    const items = fs.readdirSync(localDirPath);
    for (const item of items) {
        // Salta immagini disco e file oggetto intermedi
        if (item === 'myos.img' || item === 'NewOS_V1.img' || item === 'myos.iso' || item === 'NewOS_V1.iso' ||
            item === 'NewOS_V1.vdi' || item === 'myos.vdi' || item.endsWith('.o') || item.endsWith('.d')) {
            continue;
        }

        // Se siamo nella ROOT del disco (C:\), salta i file di avvio sciolti (devono risiedere in NewOS\Boot\)
        if (isRoot) {
            const up = item.toUpperCase();
            if (up === 'KERNEL.BIN' || up === 'KERNEL.ELF' || up === 'STAGE2.BIN' || up === 'BOOTSECT.BIN' ||
                up === 'BOOTSCR.MOD' || up === 'BOOTSCR.INI' || up === 'BOOTSETUP.MOD' || up === 'BOOTSETUP.INI' ||
                up === 'LOGIN.MOD' || up === 'LOGIN.INI' || up === 'LOGOUT.MOD' || up === 'LOGOUT.INI' ||
                up === 'BOOTX64.EFI' || up === 'BOOTX64.BIN') {
                continue;
            }
        }
        const fullItemPath = path.join(localDirPath, item);
        const stat = fs.statSync(fullItemPath);
        if (stat.isDirectory()) {
            const subEntries = [];
            addDirectoryRecursive(builder, subEntries, fullItemPath);
            builder.addDirToDir(parentEntries, item, subEntries);
        } else if (stat.isFile()) {
            const data = fs.readFileSync(fullItemPath);
            builder.addFileToDir(parentEntries, item, data);
        }
    }
}

function buildMyOsFileSystem(rootDir, totalSectors = 260096, partitionLba = 2048) {
    const builder = new ExFatBuilder(totalSectors, partitionLba);

    // Inizializza entries della Root Directory con le 3 entry standard
    const rootEntries = builder.createRootSystemEntries();

    const binDir = path.join(rootDir, 'build', 'bin');
    console.log(`[exFAT] Scansione dinamica ad autorilevamento da: ${binDir}`);

    if (fs.existsSync(binDir)) {
        addDirectoryRecursive(builder, rootEntries, binDir, true);
    } else {
        console.warn(`[exFAT ATTENZIONE] Cartella ${binDir} non trovata!`);
    }

    // Verifica se SYSTEM.PAK e' presente in build/bin/BootSetup o se occorre iniettarlo da build/
    const sysPakPath = path.join(binDir, 'BootSetup', 'SYSTEM.PAK');
    const altPakPath = path.join(rootDir, 'build', 'SYSTEM.PAK');
    if (!fs.existsSync(sysPakPath) && fs.existsSync(altPakPath)) {
        const bootSetupEntries = [];
        const bsMod = path.join(binDir, 'BOOTSETUP.MOD');
        if (fs.existsSync(bsMod)) builder.addFileToDir(bootSetupEntries, 'BOOTSETUP.MOD', fs.readFileSync(bsMod));
        const bsIni = path.join(rootDir, 'BootSetup', 'bootsetup.ini');
        if (fs.existsSync(bsIni)) builder.addFileToDir(bootSetupEntries, 'BOOTSETUP.INI', fs.readFileSync(bsIni));
        builder.addFileToDir(bootSetupEntries, 'SYSTEM.PAK', fs.readFileSync(altPakPath));
        builder.addDirToDir(rootEntries, 'BootSetup', bootSetupEntries);
    }

    // Ora che tutti i file, cartelle e cluster sono stati allocati, finalizza volume (Root, FAT, Bitmap, Boot)
    builder.finalize(rootEntries);

    console.log(`[exFAT] Volume C:\\ generato con successo: ${builder.allocatedClusters.size} cluster allocati (${builder.allocatedClusters.size * 4} KB usati su ${builder.clusterCount * 4} KB totali).`);
    return builder.volumeBuffer;
}

module.exports = { ExFatBuilder, buildMyOsFileSystem };
