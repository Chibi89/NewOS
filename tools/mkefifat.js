// ==============================================================================
// NewOS V1 64-bit - Generatore Immagine ESP FAT12 per Avvio UEFI El Torito
// (C) Newspoint Software - NewSoft - NewOS V1
// Crea una struttura FAT12 valida contenente \EFI\BOOT\BOOTX64.EFI e \KERNEL.BIN
// ==============================================================================

const fs = require('fs');
const path = require('path');

function createEfiFatImage(bootx64Buffer, kernelBuffer) {
    const SECTOR_SIZE = 512;
    const TOTAL_SECTORS = 2880; // 1.44 MB Floppy/Image standard per El Torito EFI
    const img = Buffer.alloc(TOTAL_SECTORS * SECTOR_SIZE, 0);

    // 1. BPB FAT12 a Sector 0
    img[0] = 0xEB; img[1] = 0x3C; img[2] = 0x90; // JMP SHORT 0x3E, NOP
    img.write('MSWIN4.1', 3, 8, 'ascii'); // OEM ID
    img.writeUInt16LE(512, 11);          // Bytes per sector
    img[13] = 1;                         // Sectors per cluster
    img.writeUInt16LE(1, 14);            // Reserved sectors (Boot sector)
    img[16] = 2;                         // Number of FATs
    img.writeUInt16LE(224, 17);          // Root directory entries (224 * 32 = 14 settori)
    img.writeUInt16LE(TOTAL_SECTORS, 19);// Total sectors
    img[21] = 0xF8;                      // Media descriptor (Fixed disk / generic)
    img.writeUInt16LE(9, 22);            // Sectors per FAT
    img.writeUInt16LE(18, 24);           // Sectors per track
    img.writeUInt16LE(2, 26);            // Heads
    img.writeUInt32LE(0, 28);            // Hidden sectors
    img.writeUInt32LE(0, 32);            // Total sectors 32-bit (0 if 16-bit used)
    img[36] = 0x80;                      // Drive number
    img[38] = 0x29;                      // Extended boot signature
    img.writeUInt32LE(0x12345678, 39);   // Volume serial
    img.write('NEWOS_ESP  ', 43, 11, 'ascii'); // Volume label
    img.write('FAT12   ', 54, 8, 'ascii');     // FS type
    img[510] = 0x55; img[511] = 0xAA;    // Boot signature

    // 2. Tabelle FAT (Sectors 1..9 e 10..18)
    // Cluster 0: Media byte (0xF8) + 0xF00
    // Cluster 1: 0xFFF
    const fat1Offset = 1 * SECTOR_SIZE;
    const fat2Offset = 10 * SECTOR_SIZE;

    function setFat12Entry(cluster, value) {
        for (const fatBase of [fat1Offset, fat2Offset]) {
            const byteOffset = fatBase + Math.floor((cluster * 3) / 2);
            if (cluster % 2 === 0) {
                img[byteOffset] = value & 0xFF;
                img[byteOffset + 1] = (img[byteOffset + 1] & 0xF0) | ((value >> 8) & 0x0F);
            } else {
                img[byteOffset] = (img[byteOffset] & 0x0F) | ((value & 0x0F) << 4);
                img[byteOffset + 1] = (value >> 4) & 0xFF;
            }
        }
    }

    setFat12Entry(0, 0xFF8);
    setFat12Entry(1, 0xFFF);

    // Settori fissi:
    // Sector 0: Boot
    // Sectors 1..18: FATs (2 * 9 = 18 settori)
    // Sectors 19..32: Root Directory (14 settori)
    // Sector 33 in poi: Data cluster (Cluster 2 inizia a settore 33)
    const ROOT_DIR_SECTOR = 19;
    const ROOT_DIR_OFFSET = ROOT_DIR_SECTOR * SECTOR_SIZE;
    const DATA_START_SECTOR = 33;

    let nextCluster = 2;

    function allocateDataClusters(buf) {
        if (!buf || buf.length === 0) return 0;
        const count = Math.ceil(buf.length / SECTOR_SIZE);
        const startClust = nextCluster;

        for (let i = 0; i < count; i++) {
            const curClust = nextCluster++;
            const isLast = (i === count - 1);
            setFat12Entry(curClust, isLast ? 0xFFF : curClust + 1);

            const secOffset = (DATA_START_SECTOR + (curClust - 2)) * SECTOR_SIZE;
            const srcStart = i * SECTOR_SIZE;
            const srcEnd = Math.min(srcStart + SECTOR_SIZE, buf.length);
            buf.copy(img, secOffset, srcStart, srcEnd);
        }

        return startClust;
    }

    // Alloca cluster per KERNEL.BIN
    const kernelCluster = allocateDataClusters(kernelBuffer);

    // Alloca cluster per BOOTX64.EFI
    const bootx64Cluster = allocateDataClusters(bootx64Buffer);

    // Alloca cluster per directory \EFI\BOOT\
    // Directory BOOT contiene le entry per BOOTX64.EFI
    const bootDirBuf = Buffer.alloc(SECTOR_SIZE, 0);
    // . e ..
    // Entry 0: .
    bootDirBuf.write('.          ', 0, 11, 'ascii');
    bootDirBuf[11] = 0x10; // Directory attribute
    // Entry 1: ..
    bootDirBuf.write('..         ', 32, 11, 'ascii');
    bootDirBuf[32 + 11] = 0x10;
    // Entry 2: BOOTX64.EFI
    bootDirBuf.write('BOOTX64 EFI', 64, 11, 'ascii');
    bootDirBuf[64 + 11] = 0x20; // Archive
    bootDirBuf.writeUInt16LE(bootx64Cluster, 64 + 26);
    bootDirBuf.writeUInt32LE(bootx64Buffer.length, 64 + 28);

    const bootDirCluster = allocateDataClusters(bootDirBuf);

    // Alloca cluster per directory \EFI\
    const efiDirBuf = Buffer.alloc(SECTOR_SIZE, 0);
    efiDirBuf.write('.          ', 0, 11, 'ascii');
    efiDirBuf[11] = 0x10;
    efiDirBuf.write('..         ', 32, 11, 'ascii');
    efiDirBuf[32 + 11] = 0x10;
    // Subdirectory BOOT
    efiDirBuf.write('BOOT       ', 64, 11, 'ascii');
    efiDirBuf[64 + 11] = 0x10;
    efiDirBuf.writeUInt16LE(bootDirCluster, 64 + 26);

    const efiDirCluster = allocateDataClusters(efiDirBuf);

    // Popola Root Directory
    // Entry 0: Volume Label
    let rootPos = ROOT_DIR_OFFSET;
    img.write('NEWOS_ESP  ', rootPos, 11, 'ascii');
    img[rootPos + 11] = 0x08; // Volume ID
    rootPos += 32;

    // Entry 1: Directory EFI
    img.write('EFI        ', rootPos, 11, 'ascii');
    img[rootPos + 11] = 0x10; // Directory
    img.writeUInt16LE(efiDirCluster, rootPos + 26);
    rootPos += 32;

    // Entry 2: KERNEL.BIN (nella radice per accesso immediato)
    if (kernelBuffer && kernelBuffer.length > 0) {
        img.write('KERNEL  BIN', rootPos, 11, 'ascii');
        img[rootPos + 11] = 0x20; // Archive
        img.writeUInt16LE(kernelCluster, rootPos + 26);
        img.writeUInt32LE(kernelBuffer.length, rootPos + 28);
        rootPos += 32;
    }

    return img;
}

module.exports = { createEfiFatImage };
