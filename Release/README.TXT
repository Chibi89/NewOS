================================================================================
NewOS V1 64-bit - Pacchetto di Distribuzione e Installazione (Release)
(C) Newspoint Software - NewSoft
================================================================================
Questo pacchetto contiene tutti i componenti necessari per creare un supporto
avviabile (Live USB / ISO / Disco Fisico) o eseguire l'installazione guidata
di NewOS V1 su silicio reale bare-metal x86_64 o macchina virtuale.

STRUTTURA DELLE CARTELLE:
1. \EFI\BOOT\BOOTX64.EFI
   -> Bootloader nativo UEFI a 64 bit compatibile con qualsiasi PC moderno.
      Configura GOP (Graphics Output Protocol), legge il kernel e avvia NewOS.

2. \BootSetup\
   -> Contiene il programma di installazione guidata BOOTSETUP.MOD e l'archivio
      crittografato SYSTEM.PAK (AES-256) con l'intero sistema operativo.

3. \NewOS\Boot\
   -> Cartella centralizzata dei componenti di avvio:
      - kernel.bin    : Kernel 64-bit monolitico ottimizzato (< 480 KB)
      - stage2.bin    : Bootloader Stage 2 (BIOS Legacy MBR)
      - bootsect.bin  : Settore MBR primario a 512 byte (LBA 0)
      - BOOTSCR.MOD   : Modulo protetto dello Splash Screen di avvio
      - LOGIN.MOD     : Modulo protetto di autenticazione e logon utente
      - LOGOUT.MOD    : Modulo protetto di disconnessione e arresto sistema

GUIDA ALL'AVVIO SU CHIAVETTA USB (UEFI):
Copiare l'intero contenuto di questa cartella nella radice di una chiavetta USB
formattata in FAT32 per avviare istantaneamente NewOS V1 su qualsiasi computer.
================================================================================
