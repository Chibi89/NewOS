# Regole Ufficiali di Comportamento e Sviluppo — NewOS V1 (64-bit)

Questo documento definisce le regole comportamentali, i vincoli architetturali e i requisiti di qualità imperativi per lo sviluppo, la compilazione e il collaudo del sistema operativo NewOS V1.

---

## 1. Budget di Memoria e Dimensione Massima del Kernel
- **Limite Tassativo**: Il file binario piatto `kernel.bin` non deve **MAI** superare **491.520 byte** (esattamente 480 KB = 960 settori da 512 byte).
- **Controllo Obbligatorio**: Verificare la dimensione restituita da `LLD` al termine di ogni compilazione. Mantenere sempre un margine di sicurezza per evitare sforamenti.
- **Ambiente Freestanding**: Il codice del kernel deve rimanere rigorosamente `-ffreestanding`, `-fno-builtin`, `-nostdlib`, 64-bit Long Mode, con System V AMD64 ABI. È vietato includere header o librerie runtime dell'host (es. `stdio.h`, `stdlib.h` di Windows/Linux).

---

## 2. Sincronizzazione Settori Bootloader (Stage 2 Bootloader)
- **Allineamento Settori CHS**: Il registro `chs_count` in `boot/stage2.asm` e `sources/boot/stage2.asm` deve essere sempre allineato alla capacità massima del kernel:
  ```nasm
  mov word [chs_lba], 66
  mov word [chs_count], 960   ; 480 KB (960 settori)
  ```
- **Copia in Memoria Long Mode**: Il ciclo di trasferimento a `lm64_entry` deve copiare esattamente `(960 * 512) / 8` qword da `0x20000` (buffer reale) a `0x100000` (1 MB fisico).
- **Prevenzione Triple Fault (Ghost Reboot)**: Tabelle dei salti (`switch-case jump tables`), sezioni `.rodata` e simboli del kernel situati oltre i 448 KB dipendono direttamente dal corretto caricamento di tutti i 960 settori.

---

## 3. Politica di Compilazione e Qualità del Codice (Zero-Warning Policy)
- **Zero Errori, Zero Warning**: Ogni modulo C ed Assembly deve compilare senza alcun warning sotto Clang/LLVM con flag severi (`-Wall -Wextra`).
- **Integrità dei Commenti**: Preservare tutti i commenti, docstring e documentazioni storiche preesistenti nei sorgenti. Non eliminare commenti esplicativi di terze parti o del team NewSoft.
- **Modularità**: Ogni sottosistema (VFS, USB, GUI, Net, FS) deve mantenere separati i file di intestazione (`.h`) e le implementazioni (`.c`), evitando accorpamenti monolitici disordinati.

---

## 4. Flusso di Lavoro, Collaudo e Validazione Grafica (Validation Workflow)
- **Compilazione Completa del Progetto**: Al termine di qualsiasi modifica, eseguire il build completo con `node tools/build.js`, verificando che:
  - `kernel.bin` sia generato e linkato correttamente.
  - Tutti i moduli `.MOD` esterni (`BOOTSCR.MOD`, `LOGIN.MOD`, `LOGOUT.MOD`, `BOOTSETUP.MOD`) siano firmati con CRC32 valido.
  - L'archivio cifrato AES-256 `SYSTEM.PAK` sia rigenerato.
  - Il volume exFAT a LBA 2048 e le immagini `NewOS_V1.img` e `NewOS_V1.iso` siano aggiornate con codice di uscita 0.
- **Validazione Visiva in QEMU**: Ogni modifica alla GUI (Explorer, Window Manager, dialoghi modali, menu) deve essere collaudata avviando QEMU ed acquisendo screenshot reali con gli script in `tools/` (convertiti da PPM a BMP e PNG). Non dichiarare completata una feature senza riscontro visivo oggettivo.
- **Fedeltà Grafica**: Nessun placeholder. Utilizzare la palette ufficiale Classic Windows 2000 (`COLOR_BTNFACE = 0xD4D0C8`), bordi 3D bevel/sunken realistici, font bitmap nitidi e icone ad alta risoluzione.

---

## 5. Stile di Comunicazione e Trasparenza
- **Lingua Ufficiale**: Tutte le risposte, report e spiegazioni all'utente devono essere formulate in lingua italiana.
- **Formattazione e Link**: Creare link cliccabili conformi a GitHub Markdown con schema `file:///` e barre normalizzate in avanti (`/`) per tutti i file e simboli menzionati (es. `[explorer.c](file:///c:/Users/morga/Desktop/MyOS/kernel/apps/explorer.c)`).
- **Metriche in Evidenza**: Includere sempre dimensione del kernel in byte, stato del build e riferimenti agli screenshot di prova salvati nella cartella degli artefatti.

---

## 6. Architettura del File System e Gerarchia Ufficiale
- **File di Sistema**: Collocati tassativamente in `C:\NewOS\` (`Font\`, `Help\`, `Diagnostic\`, `Resources\`, `Themes\`, `SystemProgram\`, `Dev\`).
- **Cartella Riservata Programmi Utente**: `C:\ProgramFile` è riservata **esclusivamente** ai software installati dall'utente finale tramite installer terze parti (come prescritto da Regola 01 e Regola 03). Nessun programma di sistema deve essere inserito in `C:\ProgramFile`.
- **Layout Supporto Fisico**:
  - LBA 0: MBR (Master Boot Record)
  - LBA 1..64: Stage 2 Bootloader
  - LBA 66: Kernel piatto 64-bit (`kernel.bin`)
  - LBA 1024: Repository icone di fallback (`icons.bin`)
  - LBA 2048: Inizio Partizione Volume exFAT (`C:\`)

---

## 7. Sicurezza del Registro e Moduli Protetti (.NOR e .MOD)
- **Registro di Sistema a Due Fasi**:
  - *Fase Pre-Desktop (Login/OOBE)*: Registro accessibile esclusivamente in modalità **Sola Lettura** (`registry_set_read_only(true)`). Qualsiasi tentativo di scrittura deve essere bloccato.
  - *Fase Desktop*: Scrittura abilitata solo dopo l'avvenuta autenticazione dell'utente e l'inizializzazione del Window Manager.
- **Regole Ufficiali (.NOR)**: File protetti cifrati proprietari in `C:\NewOS\Rules\`, visualizzabili solo da `RULES.EXE` su Desktop o dall'utility Windows `NewOS_Rules_Viewer.exe`.

---

## 8. Sviluppo e Compatibilità Bare-Metal su Hardware Reale (Non solo VM)
- **Destinazione d'Uso del Sistema**: Tutti i componenti, driver e sottosistemi (storage, timer, memoria, bus PCI, rete, audio, video) sia nuovi che preesistenti devono essere progettati, implementati e collaudati considerando i requisiti fisici del silicio x86_64 reale e non unicamente ambienti virtuali o emulatori (QEMU, VirtualBox, VMware).
- **Handshake Firmware e Proprietà Hardware (BIOS/OS Handoff)**: Per controller di sistema come AHCI, USB (xHCI/EHCI), ACPI o PCI, implementare sempre il protocollo di passaggio di consegne dal BIOS/SMM al sistema operativo (es. `BOHC` per AHCI, `OS_OWNED` per USB) prima di accedere ai registri operativi.
- **Tempi di Risposta Fisici e Negoziazione di Bus**: Tenere sempre conto delle latenze fisiche dei dispositivi reali (es. tempo di negoziazione PHY SATA 10..50 ms, Staggered Spin-Up dei dischi `PxCMD.SUD`, reset del bus PCI/USB, stabilizzazione PLL). Non assumere mai disponibilità immediata a latenza zero tipica delle VM.
- **Conformità e Allineamento DMA Fisico**: Rispettare rigorosamente gli allineamenti hardware dei descrittori (es. 1024B per command list, 256B per FIS, 128B per command table) ed evitare l'attraversamento di confini fisici di 4 MB o 64 KB. Fornire sempre meccanismi di rimbalzo protetto (`Bounce Buffer`) allineati per garantire la sicurezza del bus contro disallineamenti di memoria.
- **Temporizzazione Hardware Reale Calibrata**: I timeout di sicurezza non devono mai essere loop software a vuoto dipendenti dalla frequenza della CPU, bensì basati su contatori hardware fisici stabili (`rdtsc` Time Stamp Counter x86_64, PIT a 100 Hz o HPET).
- **Prevenzione di Interrupt Storm su Linee Condivise**: Gestire l'instradamento delle linee IRQ PCI tenendo conto che su macchine reali più periferiche condividono la stessa linea fisica level-triggered (IRQ sharing). Applicare mascheramento one-shot o disabilitazione degli interrupt non gestiti per impedire affamamento della CPU.

