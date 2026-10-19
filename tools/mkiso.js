const fs = require('fs');
const path = require('path');

const SECTOR_SIZE = 2048;

function writeBothEndian16(buf, offset, val) {
    buf.writeUInt16LE(val, offset);
    buf.writeUInt16BE(val, offset + 2);
}

function writeBothEndian32(buf, offset, val) {
    buf.writeUInt32LE(val, offset);
    buf.writeUInt32BE(val, offset + 4);
}

function toIsoName(name, isDir) {
    // Standard ISO 9660 Level 2 / uppercase pulito
    let base = name.toUpperCase().replace(/[^A-Z0-9_.]/g, '_');
    if (isDir) {
        return base.replace(/\./g, '_').substring(0, 31);
    } else {
        return base.substring(0, 30) + ';1';
    }
}

function encodeJolietString(str, length) {
    const u16 = Buffer.from(str, 'utf16le').swap16();
    const out = Buffer.alloc(length, 0);
    u16.copy(out, 0, 0, Math.min(u16.length, length));
    return out;
}

function getRecordLength(name, isJoliet) {
    let nameLen;
    if (name === '\x00' || name === '\x01') {
        nameLen = 1;
    } else if (isJoliet) {
        nameLen = Buffer.from(name, 'utf16le').length;
    } else {
        nameLen = Buffer.from(name, 'ascii').length;
    }
    const pad = ((33 + nameLen) % 2 !== 0) ? 1 : 0;
    return 33 + nameLen + pad;
}

function createDirectoryRecord(extentSector, dataLength, isDir, name, isJoliet) {
    let nameBuf;
    if (name === '\x00' || name === '\x01') {
        nameBuf = Buffer.from([name === '\x00' ? 0x00 : 0x01]);
    } else if (isJoliet) {
        nameBuf = Buffer.from(name, 'utf16le').swap16();
    } else {
        nameBuf = Buffer.from(name, 'ascii');
    }

    const lenFi = nameBuf.length;
    const pad = ((33 + lenFi) % 2 !== 0) ? 1 : 0;
    const lenDr = 33 + lenFi + pad;

    const rec = Buffer.alloc(lenDr, 0);
    rec[0] = lenDr;
    rec[1] = 0; // ext attr
    writeBothEndian32(rec, 2, extentSector);
    writeBothEndian32(rec, 10, dataLength);
    // Data creazione ISO: 2026-10-18 12:00:00 (126, 10, 18, 12, 0, 0, 0)
    rec[18] = 126; rec[19] = 10; rec[20] = 18;
    rec[21] = 12; rec[22] = 0; rec[23] = 0; rec[24] = 0;
    rec[25] = isDir ? 0x02 : 0x00;
    rec[26] = 0; // unit
    rec[27] = 0; // gap
    writeBothEndian16(rec, 28, 1); // vol seq
    rec[32] = lenFi;
    nameBuf.copy(rec, 33);
    if (pad) {
        rec[33 + lenFi] = 0;
    }
    return rec;
}

function calculateDirectorySectorCount(records) {
    let sectorCount = 1;
    let curOffset = 0;

    for (const r of records) {
        if (curOffset + r.length > SECTOR_SIZE) {
            sectorCount++;
            curOffset = 0;
        }
        curOffset += r.length;
    }
    return sectorCount;
}

function buildDirectorySectorBuffers(records) {
    const sectors = [];
    let curSector = Buffer.alloc(SECTOR_SIZE, 0);
    let curOffset = 0;

    for (const r of records) {
        if (curOffset + r.length > SECTOR_SIZE) {
            sectors.push(curSector);
            curSector = Buffer.alloc(SECTOR_SIZE, 0);
            curOffset = 0;
        }
        r.copy(curSector, curOffset);
        curOffset += r.length;
    }

    if (curOffset > 0 || sectors.length === 0) {
        sectors.push(curSector);
    }
    return sectors;
}

function scanTree(binDir) {
    let nextId = 1;

    function scanDir(dirPath, dirName, parent) {
        const node = {
            id: nextId++,
            name: dirName,
            isoName: dirName ? toIsoName(dirName, true) : '',
            jolietName: dirName,
            parent: parent,
            subdirs: [],
            files: [],
            isoExtentSector: 0,
            isoSectorCount: 0,
            jolietExtentSector: 0,
            jolietSectorCount: 0,
            isoSectors: null,
            jolietSectors: null
        };

        const entries = fs.readdirSync(dirPath).sort((a, b) => a.localeCompare(b));
        for (const e of entries) {
            const full = path.join(dirPath, e);
            const stat = fs.statSync(full);
            if (stat.isDirectory()) {
                node.subdirs.push(scanDir(full, e, node));
            } else {
                node.files.push({
                    name: e,
                    isoName: toIsoName(e, false),
                    jolietName: e,
                    fullPath: full,
                    size: stat.size,
                    extentSector: 0,
                    sectorCount: Math.ceil(stat.size / SECTOR_SIZE)
                });
            }
        }
        return node;
    }

    const rootNode = scanDir(binDir, '', null);
    rootNode.parent = rootNode; // In ISO root, parent of root is root

    const allDirs = [];
    function collectDirs(node) {
        allDirs.push(node);
        for (const sub of node.subdirs) {
            collectDirs(sub);
        }
    }
    collectDirs(rootNode);

    const allFiles = [];
    function collectFiles(node) {
        for (const f of node.files) {
            allFiles.push(f);
        }
        for (const sub of node.subdirs) {
            collectFiles(sub);
        }
    }
    collectFiles(rootNode);

    return { rootNode, allDirs, allFiles };
}

function buildPathTable(allDirs, isJoliet, isBigEndian) {
    const bufs = [];
    for (const d of allDirs) {
        let nameBuf;
        if (d.id === 1) {
            nameBuf = Buffer.from([0x00]);
        } else if (isJoliet) {
            nameBuf = Buffer.from(d.jolietName, 'utf16le').swap16();
        } else {
            nameBuf = Buffer.from(d.isoName, 'ascii');
        }

        const lenDi = nameBuf.length;
        const pad = (lenDi % 2 !== 0) ? 1 : 0;
        const entryLen = 8 + lenDi + pad;
        const b = Buffer.alloc(entryLen, 0);

        b[0] = lenDi;
        b[1] = 0; // ext attr
        const extent = isJoliet ? d.jolietExtentSector : d.isoExtentSector;
        const parentId = d.parent.id;

        if (isBigEndian) {
            b.writeUInt32BE(extent, 2);
            b.writeUInt16BE(parentId, 6);
        } else {
            b.writeUInt32LE(extent, 2);
            b.writeUInt16LE(parentId, 6);
        }

        nameBuf.copy(b, 8);
        if (pad) b[8 + lenDi] = 0;
        bufs.push(b);
    }

    const totalDataLen = bufs.reduce((acc, cur) => acc + cur.length, 0);
    const numSectors = Math.ceil(totalDataLen / SECTOR_SIZE) || 1;
    const tableBuf = Buffer.alloc(numSectors * SECTOR_SIZE, 0);
    let off = 0;
    for (const b of bufs) {
        b.copy(tableBuf, off);
        off += b.length;
    }
    return { buffer: tableBuf, dataLength: totalDataLen, sectorCount: numSectors };
}

function createFullIso(binDir, diskImgPath, outIsoPath) {
    console.log(`\n[MKISO] Creazione ISO 9660 + Joliet con albero completo da: ${binDir}`);
    const { rootNode, allDirs, allFiles } = scanTree(binDir);
    console.log(`[MKISO] Rilevate ${allDirs.length} cartelle e ${allFiles.length} file in build/bin.`);

    // Aggiunta file descrittivi di sistema nella root del CD
    const leggimiText = `==============================================================================
NewOS V1 64-bit Operating System
Copyright (C) @ Newspoint Software - NewSoft - NewOS V1
==============================================================================

Benvenuto nel disco di installazione e live di NewOS V1 64-bit!
Questo CD contiene l'albero completo del sistema NewOS V1 (build\\bin\\),
i moduli protetti di setup (.MOD), il kernel 64-bit e il volume live exFAT.

TUTTO IL CONTENUTO DI BUILD\\BIN E' VISIBILE DIRETTAMENTE SU QUESTO SUPPORTO.
`;
    const autorunText = `[AutoRun]\r\nlabel=NewOS V1 64-bit Live & Install Disc\r\n`;
    const sorgentiText = `NewOS V1 Sources and Distribution Disc.\r\n`;

    const extraFiles = [
        { name: 'AUTORUN.INF', isoName: 'AUTORUN.INF;1', jolietName: 'AUTORUN.INF', buffer: Buffer.from(autorunText, 'utf8') },
        { name: 'LEGGIMI.TXT', isoName: 'LEGGIMI.TXT;1', jolietName: 'LEGGIMI.TXT', buffer: Buffer.from(leggimiText, 'utf8') },
        { name: 'SORGENTI.TXT', isoName: 'SORGENTI.TXT;1', jolietName: 'SORGENTI.TXT', buffer: Buffer.from(sorgentiText, 'utf8') }
    ];

    for (const ef of extraFiles) {
        rootNode.files.push({
            name: ef.name,
            isoName: ef.isoName,
            jolietName: ef.jolietName,
            buffer: ef.buffer,
            size: ef.buffer.length,
            extentSector: 0,
            sectorCount: Math.ceil(ef.buffer.length / SECTOR_SIZE)
        });
    }

    // Disk Image (NEWOS_V1.IMG)
    const diskImgBuf = fs.readFileSync(diskImgPath);
    const diskImgCdSectors = Math.ceil(diskImgBuf.length / SECTOR_SIZE);

    // Inserisci anche NEWOS_V1.IMG nella root del CD
    rootNode.files.push({
        name: 'NEWOS_V1.IMG',
        isoName: 'NEWOS_V1.IMG;1',
        jolietName: 'NEWOS_V1.IMG',
        buffer: diskImgBuf,
        size: diskImgBuf.length,
        extentSector: 0,
        sectorCount: diskImgCdSectors
    });

    // ─── 1. LAYOUT DEI SETTORI METADATA ───
    let curSector = 16;
    const PVD_SECTOR = curSector++;          // 16
    const BOOT_REC_SECTOR = curSector++;     // 17
    const SVD_SECTOR = curSector++;          // 18 (Joliet)
    const TERM_SECTOR = curSector++;         // 19
    const BOOT_CATALOG_SECTOR = curSector++; // 20

    // Allocazione Path Tables (1 settore ciascuna)
    const PTL_SECTOR = curSector++;
    const PTM_SECTOR = curSector++;
    const JPTL_SECTOR = curSector++;
    const JPTM_SECTOR = curSector++;

    // ─── 2. CALCOLO DETERMINISTICO DIMENSIONI SETTORI DIRECTORY ───
    // Directory ISO 9660
    for (const d of allDirs) {
        const dummyRecords = [];
        dummyRecords.push({ length: getRecordLength('\x00', false) });
        dummyRecords.push({ length: getRecordLength('\x01', false) });
        for (const sub of d.subdirs) dummyRecords.push({ length: getRecordLength(sub.isoName, false) });
        for (const f of d.files) dummyRecords.push({ length: getRecordLength(f.isoName, false) });

        d.isoSectorCount = calculateDirectorySectorCount(dummyRecords);
        d.isoExtentSector = curSector;
        curSector += d.isoSectorCount;
    }

    // Directory Joliet
    for (const d of allDirs) {
        const dummyRecords = [];
        dummyRecords.push({ length: getRecordLength('\x00', true) });
        dummyRecords.push({ length: getRecordLength('\x01', true) });
        for (const sub of d.subdirs) dummyRecords.push({ length: getRecordLength(sub.jolietName, true) });
        for (const f of d.files) dummyRecords.push({ length: getRecordLength(f.jolietName, true) });

        d.jolietSectorCount = calculateDirectorySectorCount(dummyRecords);
        d.jolietExtentSector = curSector;
        curSector += d.jolietSectorCount;
    }

    // ─── 3. ALLOCAZIONE SETTORI CONTENUTI FILE (build/bin) ───
    for (const d of allDirs) {
        for (const f of d.files) {
            if (f.name === 'NEWOS_V1.IMG') continue; // Allocato a BOOT_LBA
            f.extentSector = curSector;
            curSector += f.sectorCount;
        }
    }

    // ─── 4. ALLOCAZIONE BOOT_LBA PER DISK IMAGE EL TORITO ───
    const BOOT_LBA = curSector;
    const diskImgEntry = rootNode.files.find(f => f.name === 'NEWOS_V1.IMG');
    diskImgEntry.extentSector = BOOT_LBA;
    curSector += diskImgCdSectors;

    const totalCdSectors = curSector;

    // ─── 5. GENERAZIONE BUFFER DIRECTORY DEFINITIVI ───
    // Directory ISO 9660
    for (const d of allDirs) {
        const records = [];
        records.push(createDirectoryRecord(d.isoExtentSector, d.isoSectorCount * SECTOR_SIZE, true, '\x00', false));
        records.push(createDirectoryRecord(d.parent.isoExtentSector, d.parent.isoSectorCount * SECTOR_SIZE, true, '\x01', false));
        for (const sub of d.subdirs) {
            records.push(createDirectoryRecord(sub.isoExtentSector, sub.isoSectorCount * SECTOR_SIZE, true, sub.isoName, false));
        }
        for (const f of d.files) {
            records.push(createDirectoryRecord(f.extentSector, f.size, false, f.isoName, false));
        }
        d.isoSectors = buildDirectorySectorBuffers(records);
    }

    // Directory Joliet
    for (const d of allDirs) {
        const records = [];
        records.push(createDirectoryRecord(d.jolietExtentSector, d.jolietSectorCount * SECTOR_SIZE, true, '\x00', true));
        records.push(createDirectoryRecord(d.parent.jolietExtentSector, d.parent.jolietSectorCount * SECTOR_SIZE, true, '\x01', true));
        for (const sub of d.subdirs) {
            records.push(createDirectoryRecord(sub.jolietExtentSector, sub.jolietSectorCount * SECTOR_SIZE, true, sub.jolietName, true));
        }
        for (const f of d.files) {
            records.push(createDirectoryRecord(f.extentSector, f.size, false, f.jolietName, true));
        }
        d.jolietSectors = buildDirectorySectorBuffers(records);
    }

    // Generazione Path Tables
    const ptl = buildPathTable(allDirs, false, false);
    const ptm = buildPathTable(allDirs, false, true);
    const jptl = buildPathTable(allDirs, true, false);
    const jptm = buildPathTable(allDirs, true, true);

    // ─── 6. COSTRUZIONE VOLUME ISO TOTALE ───
    const isoBuf = Buffer.alloc(totalCdSectors * SECTOR_SIZE, 0);

    // Hybrid MBR al settore 0
    diskImgBuf.copy(isoBuf, 0, 0, 512);

    // PVD (Sector 16)
    const pvd = Buffer.alloc(SECTOR_SIZE, 0);
    pvd[0] = 0x01; // PVD
    pvd.write('CD001', 1, 5, 'ascii');
    pvd[6] = 0x01;
    pvd.write('NEWOS V1 64-BIT'.padEnd(32, ' '), 8, 32, 'ascii');
    pvd.write('NEWOS_V1'.padEnd(32, ' '), 40, 32, 'ascii');
    writeBothEndian32(pvd, 80, totalCdSectors);
    writeBothEndian16(pvd, 120, 1);
    writeBothEndian16(pvd, 124, 1);
    writeBothEndian16(pvd, 128, SECTOR_SIZE);
    writeBothEndian32(pvd, 132, ptl.dataLength);
    pvd.writeUInt32LE(PTL_SECTOR, 140);
    pvd.writeUInt32BE(PTM_SECTOR, 148);

    // Root record in PVD
    const rootIsoRec = createDirectoryRecord(rootNode.isoExtentSector, rootNode.isoSectorCount * SECTOR_SIZE, true, '\x00', false);
    rootIsoRec.copy(pvd, 156);

    pvd.write('NEWOS_V1'.padEnd(128, ' '), 190, 128, 'ascii');
    pvd.write('NEWPOINT SOFTWARE'.padEnd(128, ' '), 318, 128, 'ascii');
    pvd.write('NEWSOFT'.padEnd(128, ' '), 446, 128, 'ascii');
    pvd.write('NEWOS V1'.padEnd(128, ' '), 574, 128, 'ascii');

    const dateAscii = '2026101812000000\0';
    pvd.write(dateAscii, 813, 17, 'ascii');
    pvd.write(dateAscii, 830, 17, 'ascii');
    pvd.write('0000000000000000\0', 847, 17, 'ascii');
    pvd.write(dateAscii, 864, 17, 'ascii');
    pvd[881] = 0x01;
    pvd.copy(isoBuf, PVD_SECTOR * SECTOR_SIZE);

    // Boot Record Volume Descriptor (Sector 17)
    const brvd = Buffer.alloc(SECTOR_SIZE, 0);
    brvd[0] = 0x00;
    brvd.write('CD001', 1, 5, 'ascii');
    brvd[6] = 0x01;
    brvd.write('EL TORITO SPECIFICATION'.padEnd(24, '\0'), 7, 24, 'ascii');
    brvd.writeUInt32LE(BOOT_CATALOG_SECTOR, 71);
    brvd.copy(isoBuf, BOOT_REC_SECTOR * SECTOR_SIZE);

    // SVD - Joliet (Sector 18)
    const svd = Buffer.alloc(SECTOR_SIZE, 0);
    svd[0] = 0x02; // SVD
    svd.write('CD001', 1, 5, 'ascii');
    svd[6] = 0x01;
    svd[7] = 0x00;
    encodeJolietString('NEWOS V1 64-BIT', 32).copy(svd, 8);
    encodeJolietString('NEWOS_V1', 32).copy(svd, 40);
    writeBothEndian32(svd, 80, totalCdSectors);
    // Escape sequence UCS-2 Level 3 (%/E)
    svd[88] = 0x25; svd[89] = 0x2F; svd[90] = 0x45;
    writeBothEndian16(svd, 120, 1);
    writeBothEndian16(svd, 124, 1);
    writeBothEndian16(svd, 128, SECTOR_SIZE);
    writeBothEndian32(svd, 132, jptl.dataLength);
    svd.writeUInt32LE(JPTL_SECTOR, 140);
    svd.writeUInt32BE(JPTM_SECTOR, 148);

    // Root record in SVD (Joliet)
    const rootJolietRec = createDirectoryRecord(rootNode.jolietExtentSector, rootNode.jolietSectorCount * SECTOR_SIZE, true, '\x00', true);
    rootJolietRec.copy(svd, 156);

    encodeJolietString('NEWOS_V1', 128).copy(svd, 190);
    encodeJolietString('NEWPOINT SOFTWARE', 128).copy(svd, 318);
    encodeJolietString('NEWSOFT', 128).copy(svd, 446);
    encodeJolietString('NEWOS V1', 128).copy(svd, 574);
    svd.write(dateAscii, 813, 17, 'ascii');
    svd.write(dateAscii, 830, 17, 'ascii');
    svd.write('0000000000000000\0', 847, 17, 'ascii');
    svd.write(dateAscii, 864, 17, 'ascii');
    svd[881] = 0x01;
    svd.copy(isoBuf, SVD_SECTOR * SECTOR_SIZE);

    // Terminator (Sector 19)
    const term = Buffer.alloc(SECTOR_SIZE, 0);
    term[0] = 0xFF;
    term.write('CD001', 1, 5, 'ascii');
    term[6] = 0x01;
    term.copy(isoBuf, TERM_SECTOR * SECTOR_SIZE);

    // El Torito Boot Catalog (Sector 20)
    const catalog = Buffer.alloc(SECTOR_SIZE, 0);
    catalog[0] = 0x01; // Header ID
    catalog[1] = 0x00; // x86
    catalog.write('NewOS V1 Bootloader'.padEnd(24, '\0'), 4, 24, 'ascii');
    catalog[30] = 0x55;
    catalog[31] = 0xAA;
    let csum = 0;
    for (let i = 0; i < 32; i += 2) {
        if (i !== 28) csum = (csum + catalog.readUInt16LE(i)) & 0xFFFF;
    }
    catalog.writeUInt16LE((0x10000 - csum) & 0xFFFF, 28);

    // Initial / Default Entry (offset 32)
    catalog[32] = 0x88; // Bootable
    catalog[33] = 0x04; // Hard Disk Emulation
    catalog.writeUInt16LE(0, 34);
    catalog.writeUInt16LE(1, 38); // 1 sector
    catalog.writeUInt32LE(BOOT_LBA, 40); // Sector where myos.img starts
    catalog.copy(isoBuf, BOOT_CATALOG_SECTOR * SECTOR_SIZE);

    // Scrittura Path Tables
    ptl.buffer.copy(isoBuf, PTL_SECTOR * SECTOR_SIZE);
    ptm.buffer.copy(isoBuf, PTM_SECTOR * SECTOR_SIZE);
    jptl.buffer.copy(isoBuf, JPTL_SECTOR * SECTOR_SIZE);
    jptm.buffer.copy(isoBuf, JPTM_SECTOR * SECTOR_SIZE);

    // Scrittura settori Directory ISO 9660
    for (const d of allDirs) {
        for (let s = 0; s < d.isoSectors.length; s++) {
            d.isoSectors[s].copy(isoBuf, (d.isoExtentSector + s) * SECTOR_SIZE);
        }
    }

    // Scrittura settori Directory Joliet
    for (const d of allDirs) {
        for (let s = 0; s < d.jolietSectors.length; s++) {
            d.jolietSectors[s].copy(isoBuf, (d.jolietExtentSector + s) * SECTOR_SIZE);
        }
    }

    // Scrittura file data da build/bin
    for (const d of allDirs) {
        for (const f of d.files) {
            if (f.name === 'NEWOS_V1.IMG') continue;
            let fileBuf = f.buffer;
            if (!fileBuf && f.fullPath) {
                fileBuf = fs.readFileSync(f.fullPath);
            }
            if (fileBuf && fileBuf.length > 0) {
                fileBuf.copy(isoBuf, f.extentSector * SECTOR_SIZE);
            }
        }
    }

    // Scrittura Disk Image a BOOT_LBA
    diskImgBuf.copy(isoBuf, BOOT_LBA * SECTOR_SIZE);

    const tempOut = outIsoPath + '.tmp';
    fs.writeFileSync(tempOut, isoBuf);
    try {
        if (fs.existsSync(outIsoPath)) fs.unlinkSync(outIsoPath);
    } catch (e) {}
    try {
        fs.renameSync(tempOut, outIsoPath);
    } catch (e) {
        fs.copyFileSync(tempOut, outIsoPath);
        try { fs.unlinkSync(tempOut); } catch (e2) {}
    }
    console.log(`[ISO] Generata immagine ISO avviabile con successo: ${outIsoPath}`);
    console.log(`[ISO] Dimensione: ${isoBuf.length} byte (${(isoBuf.length / (1024 * 1024)).toFixed(2)} MB), Settori CD: ${totalCdSectors}`);
    return isoBuf.length;
}

function createIsoFromDiskImage(diskImgPath, outIsoPath, binDir) {
    if (!binDir) {
        const candidate1 = path.join(path.dirname(diskImgPath), 'bin');
        const candidate2 = path.join(path.dirname(path.dirname(diskImgPath)), 'build', 'bin');
        binDir = fs.existsSync(candidate1) ? candidate1 : candidate2;
    }
    return createFullIso(binDir, diskImgPath, outIsoPath);
}

module.exports = { createIsoFromDiskImage, createFullIso };

if (require.main === module) {
    const ROOT = path.resolve(__dirname, '..');
    const binDir = path.join(ROOT, 'build', 'bin');
    const diskImg = path.join(ROOT, 'build', 'NewOS_V1.img');
    const outIso = path.join(ROOT, 'build', 'NewOS_V1.iso');
    createFullIso(binDir, diskImg, outIso);
    fs.copyFileSync(outIso, path.join(ROOT, 'NewOS_V1.iso'));
}
