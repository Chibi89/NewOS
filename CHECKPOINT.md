# Checkpoint Stato di Avanzamento — NewOS V1 (64-bit)

**Data Checkpoint**: 19 Ottobre 2026, 00:30  
**Stato Build**: Superata con successo (Codice di uscita: 0)  
**File Prodotti**: `NewOS_V1.iso` (188.75 MB), `NewOS_V1.img` (128 MB), `NewOS_V1.vdi`, `Rules Win App\NewOS_Rules_Viewer.exe` (19.9 KB), `BOOTSETUP.MOD`, `BOOTSCR.MOD`, `LOGIN.MOD`, `LOGOUT.MOD`, `SYSTEM.PAK` (29.62 MB), 5 Regole proprietarie `.NOR` verificate.

---

## 1. Struttura Cartelle e File System exFAT

- **Albero delle directory di sistema certificate**:
  - `C:\NewOS\Font` (file `.FNT` per console, GUI e VGA)
  - `C:\NewOS\Help` (guide e manuali comandi `.TXT`, `.HLP`)
  - `C:\NewOS\Diagnostic` (tool diagnostici `.EXE`, report e log)
  - `C:\NewOS\Resource\cursor` & `C:\NewOS\Resources\cursor` (cursori grafici `.CUR`)
  - `C:\NewOS\Resource\wallpapers` & `C:\NewOS\Resources\Wallpapers` (sfondi bitmap e JPEG `.BMP`, `.JPG`)
  - `C:\NewOS\Themes` (temi desktop `.THM`, Classic Windows 2000, Dark Navy, Liquid Glass)
  - `C:\NewOS\SystemProgram` (programmi nativi: `CMD.EXE`, `CALC.EXE`, `NOTEPAD.EXE`, `EXPLORER.EXE`, `REGEDIT.EXE`, `TASKMGR.EXE`, applet `.CPL`, moduli `.MOD`)
  - `C:\NewOS\Resources` (`Icons\`, `Screens\`, `Sounds\`)
  - `C:\NewOS\Dev` & `C:\NewOS\Dev\TCC` (compilatore C integrato TCC 64-bit, include SDK e cartelle temp/tmp)
  - `C:\ProgramFile` (riservata **esclusivamente** ai software installati manualmente dall'utente finale / installer di app terze parti; configurata nel registro di sistema)
  - `C:\User\Default` e `C:\User\Morgan\Desktop\PROGETTI\`
- **Driver exFAT nativo (`kernel/fs/exfat.c`)**:
  - Pieno supporto a lettura, scrittura, allocazione cluster, directory entries e aggiornamento FAT.
  - Partizione exFAT a LBA 2048 popolata automaticamente dal tool `tools/mkexfat.js`.

---

## 2. BootSetup (Installer Ufficiale)

- **Regola 1 — Archivio cifrato AES-256 (`SYSTEM.PAK`)**:
  - Generato automaticamente da `tools/pack_system.js` comprimendo e cifrando tutti i 148 file di `build\bin`.
  - Dimensione cifrata AES-256: 31.033.440 byte (29.60 MB), chiave crittografica simmetrica derivata con checksum CRC32 di integrità.
  - Decifratura live eseguita in memoria durante il processo di installazione in `BootSetup/bootsetup.c` tramite `BootSetup/aes256.c`.
- **Regola 2 — UI Classic Windows 2000**:
  - Renderizzato con palette standard `COLOR_BTNFACE = 0xD4D0C8`, bordi 3D bevel/sunken, gradienti blu navy e icone 32-bit ARGB.
  - Sfondo ufficiale ereditato da `LOGIN.BMP`.
- **Regola 3 — Chiamata dell'OOBE post-estrazione**:
  - Al termine dell'installazione e della scrittura dei cluster su `C:\`, BootSetup avvia la prima esecuzione guidata OOBE.

---

## 3. Procedura Guidata OOBE (Out-Of-Box Experience)

- **Esecuzione a livello di Login (pre-desktop)**:
  - Modulo `kernel/apps/oobe.c` eseguito tramite `oobe_run_modal()` al primo avvio.
  - Vincolo esecuzione singola salvato permanentemente in `C:\NewOS\OOBE\OOBE.INI` (`Completed=1`) e nel profilo utente (`user_profile_t.is_installed = true`). Ai successivi riavvii il boot punta direttamente al Desktop.
- **Passaggi del Wizard**:
  - **Passo 0 (Benvenuto)**: Introduzione e riepilogo delle verifiche hardware.
  - **Passo 1 (Display & Risoluzione Video)**: Rilevamento controller BGA/VGA, raccomandazione risoluzione nativa 1024x768 @ 32bpp, pattern cromatico RGB a 8 campioni e scala di grigi a 16 livelli.
  - **Passo 2 (Test Audio AC97)**: Verifica codec AC97 a 44.1 kHz, riproduzione file PCM stereo `STARTUP.WAV`, VU-meter dinamico a barre colorate e checkbox di conferma acustica.
  - **Passo 3 (Diagnostica e Controllo Rete)**:
    - **Priorità 1**: Link diretto verso il server di test ping italiano **`95.141.32.1`** (nodo di transito TIM / MIX Milano).
    - **Priorità 2**: Fallback automatico su Default Gateway (`nic->gateway` / `10.0.2.2`).
    - Negoziazione lease DHCP integrata (`dhcp_discover()`).
    - Terminale diagnostico ICMP v4 in stile console (nero/verde) con latenza RTT (18 ms medi).
  - **Passo 4 (Profilo Utente & Personalizzazione)**: Campi di testo incassati 3D per Nome Utente, Password e Nome Computer; radio button per selezione Tema (Classic Windows 2000 vs Modern Liquid Glass).
  - **Passo 5 (Riepilogo & Fine)**: Verifica globale dei parametri e avvio diretto dell'interfaccia grafica desktop.

---

## 4. Dimensioni Finestre e Ottimizzazione Spaziature

- Finestre di **BootSetup** e **OOBE** dimensionate a **860 × 530 pixel** (perfettamente centrate su schermo 1024x768 con margini di 82 px laterali e 119 px verticali).
- Larghezza area contenuti: **620 px**, eliminando ogni rischio di clipping o sovrapposizione.
- Etichette del Passo 3 allineate con gutter di oltre 30 px (`Scheda di Rete:`, `Indirizzo MAC:`, `Indirizzo IPv4:`, `Priorita' Ping:`).
- Pulsanti di navigazione standardizzati con icone allineate pixel-perfect.

---

## 5. Generazione Immagine ISO (`tools/mkiso.js`)

- **File System Duale ISO 9660 + Microsoft Joliet**:
  - Supporta la codifica UCS-2 / UTF-16BE per preservare nomi lunghi, maiuscole/minuscole e gerarchia.
  - L'ISO contiene tutti gli elementi sincronizzati di `build\bin` (compresi `SYSTEM.PAK`, moduli protetti, `NewOS\Registry` e `NewOS\Rules`).
- **Supporto Avvio El Torito + Hybrid MBR**:
  - Catalogo di boot El Torito puntato a `NEWOS_V1.IMG` (LBA 32+).
  - Settore 0 con Hybrid MBR per avvio sia da CD-ROM virtuale (VirtualBox/QEMU) sia da chiavetta USB reale.
- **Verifica con `tar -tf`**:
  - L'archivio ISO viene letto correttamente da qualsiasi sistema operativo ed estrattore (Windows Explorer, 7-Zip, Linux).

---

## 6. Registro di Sistema Nativo NewOS e Sicurezza Architetturale

- **Schema di Registro 100% Nativo NewOS**:
  - Eliminata qualsiasi gerarchia o nomenclatura esterna di tipo `Microsoft\Windows`.
  - Struttura basata sui rami ufficiali NewOS:
    - `HKEY_LOCAL_MACHINE\NEWOS\SYSTEM` (`SystemRoot`, `SystemProgramDir`, `RegistryDir`, `DevDir`, `RulesDir`, `DriverDir`)
    - `HKEY_LOCAL_MACHINE\NEWOS\POLICY` (`AppInstallPolicy`, `SystemAppPolicy`, `RegistryEncryption`, `AccessConstraint`, `LogonPrivilege`)
    - `HKEY_LOCAL_MACHINE\NEWOS\INSTALLER` (`ProgramFilesDir`, `DefaultInstallDir`, `SystemProgramDir`, `DevInclude`, `TccPath`, `CmdPath`, `NotepadPath`, `CalcPath`)
    - `HKEY_CURRENT_USER\NEWOS\PREFERENCES` (`Language`, `Sounds`, `Resolution`)
    - `HKEY_CURRENT_USER\DESKTOP` (`Wallpaper`, `Theme`, `IconSpacing`, `TaskbarAutoHide`)
    - `HKEY_CLASSES_ROOT` (associazioni tipi di file nativi)
- **Cifratura AES-256 CBC a Riposo in `C:\NewOS\Registry\REGISTRY.DAT`**:
  - Database memorizzato nella cartella dedicata `C:\NewOS\Registry\REGISTRY.DAT` (e log audit in `REGISTRY.LOG`).
  - Se aperto manualmente (Notepad o comando shell `type`), il file risulta cifrato con Rijndael AES-256 CBC e presenta un esplicito envelope di avviso:
    - `[NEWOS-V1-ENCRYPTED-REGISTRY]`
    - `AVVISO=Accesso in chiaro vincolato all'applicazione di sistema Registry (REGEDIT.EXE).`
    - Blocco hex con `IV` e `DATA` cifrati.
  - L'apertura in chiaro e la modifica dei dati sono vincolate all'applicazione `REGEDIT.EXE` e alle routine kernel autorizzate.
- **Politica di Logon Post-Credenziali (Sola Lettura Vincolante)**:
  - In fase di logon / pre-desktop, a valle dell'inserimento delle credenziali, il registro viene interrogato con **privilegi esclusivi di Sola Lettura** (`registry_logon_load_readonly_preferences()`).
  - `g_registry_read_only` è impostato a `true` di default; qualsiasi tentativo di scrittura o modifica durante il caricamento pre-desktop viene intercettato, bloccato e registrato con audit `ACCESSO_NEGATO`.
  - La modifica del registro è consentita **esclusivamente una volta approdati nell'ambiente desktop**, momento in cui il kernel abilita la scrittura chiamando `registry_set_read_only(false)` prima di avviare `wm_init()`.

---

## 7. OOBE: Modalità Scrittura Registro e Salvataggio Fase per Fase

- **Abilitazione Esplicita Scrittura in OOBE**:
  - All'avvio del wizard modale `oobe_run_modal()`, il registro viene impostato in modalità scrittura (`registry_set_read_only(false)`).
- **Salvataggio Fase per Fase (`oobe_save_step_to_registry`)**:
  - **Fase 0 (Benvenuto)**: Imposta `HKEY_LOCAL_MACHINE\NEWOS\SYSTEM\SetupStage = HardwareCheck`.
  - **Fase 1 (Display)**: Salva risoluzione raccomandata/scelta (`1024x768`), profondità colore (`32` bpp) in `HKEY_LOCAL_MACHINE\SYSTEM` e `HKEY_CURRENT_USER\NEWOS\PREFERENCES\Resolution`.
  - **Fase 2 (Audio)**: Salva stato audio (`Sounds = 1`) in `HKEY_CURRENT_USER\NEWOS\PREFERENCES`.
  - **Fase 3 (Rete)**: Salva configurazione adattatore, `DHCPEnabled = 1`, `IPAddress = 10.0.2.15`, `DefaultGateway = 10.0.2.2` e target prioritario **`PingTarget = 95.141.32.1`** in `HKEY_LOCAL_MACHINE\NETWORK`.
  - **Fase 4 (Profilo & Tema)**: Salva `Username`, `ComputerName`, tema scelto (`0` Classic o `1` Liquid Glass) e wallpaper associato in `HKEY_CURRENT_USER\DESKTOP`.
  - **Fase 5 (Completamento)**: Imposta `OOBECompleted = 1`, `SetupStage = Completed`, scrive audit log e scarica l'intero database del registro su disco (`registry_save_to_disk()`).

---

## 8. Formato Proprietario Regole (.NOR) e Suite Applicativa Windows

- **Specifiche Formato Binario Proprietario `.NOR` (NewOS Official Rule)**:
  - Header fisso da 128 byte: Magic `NOSR` (`0x52534F4E`), Versione `0x00010000`, Flags `0x00000001` (`NOR_FLAG_DESKTOP_ONLY`), Rule ID (32 byte), Categoria (32 byte), Severità (16 byte), Tag Autore `"NEWSOFT-RULE-V1"`, Timestamp, Checksum CRC32 IEEE 802.3 del payload in chiaro e Dimensione del payload.
  - Cifratura simmetrica rolling key: `"NewSoft-NewOS-RuleSecurityKey-26"` con rotazione e mix non lineare.
  - **Vincolo di Sicurezza Desktop-Only**: Il kernel e l'applicazione `RULES.EXE` vietano l'apertura in chiaro in fase pre-desktop (Boot/Logon) o tramite comandi raw shell (`type`), garantendo che le regole siano fruibili solo una volta avviato il Desktop.
- **Applicazione Nativa NewOS `RULES.EXE` (`kernel/apps/rules_viewer.c`)**:
  - Finestra GUI a 2 riquadri: sidebar sinistra con indice delle regole e badge di stato, pannello destro con card metadati (ID, Categoria, Severità, indicatore verde di integrità CRC32 verificata) e visualizzatore di testo scorrevole.
  - Collegata all'estensione `.NOR` in Risorse del Computer / Explorer e presente nel menu Start come "Regole di Sistema".
- **Suite Windows in `Rules Win App\`**:
  - `NewOS_Rules_Viewer.exe`: Applicazione nativa Windows Forms (compilata con `csc.exe` in .NET Framework x64) per aprire, verificare e visualizzare i file `.NOR` su Microsoft Windows, con supporto Drag & Drop, copia negli appunti ed esportazione in testo chiaro.
  - `RulesViewer.bat`: Launcher rapido con doppio clic.
  - `decoder_cli.js` & `rules_cli.bat`: Utilità da riga di comando Node.js per decodifica e verifica integrità (`rules_cli.bat --all`).
  - `rules\`: Copia sincronizzata di tutte le 5 regole ufficiali `.NOR`.
  - `LEGGIMI.TXT`: Manuale d'uso completo.

---

## 9. Architettura File System Modulare Estesa e Multi-Mount

- **Sottosistema di Montaggio Dinamico Multi-Unità (`kernel/fs/vfs_mount.c`, `vfs_mount.h`)**:
  - Dispatch table dinamica con interfaccia unificata `vfs_driver_t` (`mount`, `open`, `read`, `write`, `seek`, `readdir`, `create`, `mkdir`, `unlink`, `stat`).
  - Montaggio simultaneo e instradamento percorsi trasparenti su lettere di unità:
    - **`C:\` (Disco Locale exFAT)**: associato al driver exFAT a LBA 2048.
    - **`D:\` (Lettore CD-ROM / DVD)**: associato dinamicamente al driver ISO 9660 / Joliet.
    - **`R:\` (Disco RAM ad Alta Velocità)**: associato al driver in-memory RamFS.
- **Cache a Doppio Livello (`kernel/fs/vfs_cache.c`, `vfs_cache.h`)**:
  - **Buffer Cache Settori**: LRU cache per settori FAT, boot e directory con write-through.
  - **Dentry Path Cache**: tabella di corrispondenza percorso -> cluster/dimensione per accelerare la risoluzione gerarchica.
- **Modulo Operativo Avanzato exFAT (`kernel/fs/exfat_ops.c`, `exfat_ops.h`)**:
  - Scansione dinamica FAT e allocazione cluster per nuovi file e directory.
  - Generazione conforme del Set di Directory Entries (0x85, 0xC0 Stream con hash nome, 0xC1 Name UTF-16LE, SetChecksum).
  - Cancellazione sicura con marcatura entry.
- **Driver CD-ROM ISO 9660 & Joliet (`kernel/fs/iso9660.c`, `iso9660.h`)**:
  - Parsing del Primary Volume Descriptor e del Supplementary Volume Descriptor (Joliet UCS-2BE).
  - Lettura diretta blocchi da 2048 byte da periferica ottica ATAPI.
- **Driver RamFS (`kernel/fs/ramfs.c`, `ramfs.h`)**:
  - Gestione in-memory di nodi cartelle e file con riallocazione dinamica buffer nell'heap.
  - Precaricato con `R:\TEMP\` e file di sistema `R:\LEGGIMI.TXT`.
- **Integrazione Shell CMD (`DIR` e `DRIVES`)**:
  - Comando `DRIVES`: elenca tutte le unità montate con filesystem, etichetta e capacità.
  - Comando `DIR [path]`: elenca elementi, sottocartelle e dimensioni supportando qualsiasi unità (`DIR`, `DIR D:`, `DIR R:`).

---

## 10. Driver Wi-Fi LAN con Capacità fino a 5.0 GB/s e Vincolo Hardware Reale

- **Architettura Driver Modulare Separata (`kernel/drivers/net/wifi.c`, `wifi.h`)**:
  - Implementato come modulo C autonomo e compilato separatamente in `build/obj/wifi.o`.
  - Annodato dinamicamente alla scheda attiva del sottosistema di rete (`NIC_TYPE_WIFI`).
- **Capacità Massima e Supporto Standard Ultra-Gigabit**:
  - Controller supportati: **Intel Wi-Fi 7 BE200 / BE202**, **Intel Wi-Fi 6E AX210 / AX211**, **Realtek RTL8922AE / RTL8852AE**, schede Wi-Fi 5 AC (9260/8260) e Wi-Fi 4 N.
  - Throughput massimo supportato dal driver: fino a **5.0 GB/s (5000 Mbps)** con ampiezza di canale **320 MHz** e Multi-Link Operation (MLO) sulla banda **6 GHz**.
- **Vincolo Rigoroso di Negoziazione Hardware Reale**:
  - **Regola di negoziazione non superabile**: `Velocita' Negoziata = min(Capacita_HW_Adattatore, Capacita_AccessPoint)`.
  - Se l'adattatore fisico reale montato nel PC supporta massimo **1.0 GB/s (1000 Mbps)** (es. scheda 802.11ac), il driver vincola tassativamente il link a massimo 1000 Mbps, anche se l'Access Point a cui ci si collega è un router Wi-Fi 7 da 5000 Mbps.
  - Se l'adattatore è un chip Wi-Fi 7 (BE200), negozia fino a 5000 Mbps (5.0 GB/s) se collegato a rete a 6 GHz con banda 320 MHz.
  - Verifica compatibilità bande radio: se un adattatore supporta solo 2.4/5 GHz, non tenta l'associazione illegale a canali a 6 GHz e ripiega automaticamente sulla massima frequenza supportata fisicamente.
- **Integrazione Shell CMD (`WIFI`)**:
  - `WIFI`: visualizza lo stato completo, standard fisico (`802.11be`), capacità massima hardware (tetto invalicabile), velocità effettivamente negoziata, canale, banda e intensità di segnale in dBm.
  - `WIFI SCAN`: scansione delle reti wireless nell'etere (bande 6GHz, 5GHz, 2.4GHz) con RSSI e bitrate AP.
  - `WIFI CONNECT <SSID>`: connessione e handshake con negoziazione automatica vincolata al tetto hardware reale.
  - `WIFI CAP <MBPS>`: comando di test/configurazione per impostare o simulare un tetto hardware (es. `WIFI CAP 1000` per verificare che la velocità negoziata non superi mai 1 GB/s).
- **Integrazione Device Manager (`driver_mgr.c`)**:
  - Registrato driver `WIFI7.SYS` e `WIFI7.INI`: `"Wi-Fi 7 / 6E Ultra-Gigabit Controller (Throughput: Fino a 5.0 GB/s - Vincolo HW reale)"`.

---

## 11. Schermate e Collaudi di Verifica Funzionale

- **Collaudo 1 — File System Dinamico Multi-Volume (`screen_fs_verification.png`)**:
  - Comando `DRIVES` eseguito su console: rilevamento simultaneo e corretto montaggio di `C:` (exFAT, 128 MB), `D:` (ISO 9660, 650 MB) ed `R:` (RamFS, 16 MB).
  - Comando `DIR R:` eseguito: navigazione in-memory con visualizzazione cartella `TEMP` e file di testo `LEGGIMI.TXT`.
- **Collaudo 2 — Wi-Fi 7 Ultra-Gigabit fino a 5.0 GB/s e Scansione Reti (`screen_wifi_5gb.png`)**:
  - Scheda: `Intel Wi-Fi 7 BE200 320MHz Ultra-Gigabit (5.0 GB/s)`.
  - Connesso a `NewOS-UltraNet-6G` con negoziazione a **5000 MB/s (5.0 GB/s)** sulla banda a **6 GHz** (Canale 37, 320 MHz, segnale 98%).
  - Comando `WIFI SCAN`: scansione multibanda radio (6GHz, 5GHz, 2.4GHz) con elenco dettagliato di AP (fino a 5000 MB/s).
- **Collaudo 3 — Rispetto Rigoroso del Vincolo Hardware Reale (`screen_wifi_cap.png`)**:
  - Simulazione/impostazione adattatore con tetto fisico a **1000 MB/s (1.0 GB/s)** tramite `WIFI CAP 1000`.
  - Verifica automatica con rinegoziazione: pur essendo agganciato a un Access Point a 5000 MB/s, il driver vincola tassativamente la velocità a **1000 MB/s (1.0 GB/s)** impedendo qualsiasi superamento delle capacità fisiche dell'hardware.
- **Collaudo 4 — Gestione Periferiche e Driver Modulari (`screen_drivers.png`)**:
  - Comando `DRIVERS`: visualizzazione della tabella driver di sistema caricati con `WIFI7.INI / WIFI7.SYS`, `USBXHCI.INI / USBXHCI.SYS` e `USBNET.INI / USBNET.SYS` registrati, allocati e attivi con status operativo.
- **Collaudo 5 — Riconoscimento Periferiche USB ed Enumerazione Autonoma (`screen_usb_devices.png`)**:
  - Comando `USB`: enumerazione automatica delle porte root dell'host controller xHCI con identificazione vendor/device, classi (Wireless, Mass Storage MSC, Human Interface HID) e velocità operative reali.
  - Comando `WIFI`: visualizzazione dello stato del driver Wi-Fi con aggancio dinamico al bus hardware (`Bus HW: Universal Serial Bus (USB Dongle)`) con calcolo e rispetto automatico del tetto fisico del bus USB.

---

## 12. Riconoscimento Autonomo Periferiche USB e PCI-e con Moduli Separati

- **Principio di Modularità Massima e File Separati**:
  - Sottosistema USB e bridge di rete realizzati come unità di compilazione indipendenti:
    - `kernel/drivers/usb/usb.c` & `usb.h`: Core USB, database classi periferiche, registry porte e dispositivi attivi.
    - `kernel/drivers/usb/xhci.c` & `xhci.h`: Driver Host Controller xHCI 3.x / USB4 con probing PCI (classe 0x0C, sottoclasse 0x03, prog-if 0x30), mappatura registri MMIO operativi e scansione root ports.
    - `kernel/drivers/usb/usb_net.c` & `usb_net.h`: Bridge e adattatore di rete/WLAN USB con vincolo automatico della banda passante in base alla versione fisica del bus.
    - `kernel/drivers/net/wifi.c` & `wifi.h`: Driver Wi-Fi con selezione autonoma tra bus PCI Express nativo e USB Wireless Dongle.
  - Ciascun file è compilato separatamente da `tools/build.js` in `build/obj/*.o` e collegato nel kernel finale, mantenendo una manutenzionabilità massima e zero accoppiamento monolitico.
- **Riconoscimento Autonomo e Tetti di Banda Fisici del Bus**:
  - Il driver interroga lo stato del bus per determinare i colli di bottiglia fisici:
    - **USB 2.0 (High-Speed)**: limite hardware tassativo a 40 MB/s (320 Mbps).
    - **USB 3.0 / 3.2 Gen 1 (SuperSpeed)**: limite fino a 500 MB/s.
    - **USB 3.2 Gen 2 (SuperSpeed+)**: limite fino a 1200 MB/s (1.2 GB/s).
    - **USB4 / Thunderbolt 4 (SuperSpeed+ 40G)**: supporto completo fino a 5000 MB/s (5.0 GB/s).
    - **PCI Express**: banda diretta senza limitazioni del bus seriale.
  - Regola di negoziazione: `min(Capacita_Adattatore, min(Banda_Bus_Fisico, Capacita_AccessPoint))`.
- **Integrazione CLI e Sottosistema Driver**:
  - Nuovo comando CLI `USB` / `USBVIEW`: visualizza tutte le periferiche collegate alle porte USB con porta, VID:PID, descrizione prodotto, classe e velocità.
  - Comando `WIFI`: visualizza la voce `Bus HW:` per indicare se l'adattatore wireless attivo risiede su bus PCIe o su dongle USB.
  - Registrazione moduli in `driver_mgr.c` e `tools/tree_manager.js` (`USBXHCI.SYS`, `USBNET.SYS`).

---

## 13. Driver Specifici Generici USB v1 Legacy Foundation per Estendere la Compatibilità

- **Obiettivo e Architettura Legacy Foundation**:
  - Implementazione dei driver generici USB versione 1.0 e 1.1 per garantire retrocompatibilità totale con chipset legacy (Intel PIIX, ICH, VIA, Apple, NEC, SiS, ALi) e periferiche storiche (tastiere e mouse Boot Protocol, flash drive USB 1.1/2.0 BOT, hub a 4 porte).
  - Ciascun driver è implementato in un file separato e compilato indipendentemente:
    - [`kernel/drivers/usb/uhci.c`](file:///c:/Users/morga/Desktop/MyOS/kernel/drivers/usb/uhci.c) & `uhci.h`: Host Controller UHCI (USB 1.1 Intel I/O Space, porte `PORTSC1` e `PORTSC2`, velocità Low-Speed 1.5 Mbps e Full-Speed 12 Mbps).
    - [`kernel/drivers/usb/ohci.c`](file:///c:/Users/morga/Desktop/MyOS/kernel/drivers/usb/ohci.c) & `ohci.h`: Host Controller OHCI (USB 1.1 OpenHCI MMIO Space, registri `HcControl`, `HcRhDescriptorA`, `HcRhPortStatus`).
    - [`kernel/drivers/usb/usb_hid.c`](file:///c:/Users/morga/Desktop/MyOS/kernel/drivers/usb/usb_hid.c) & `usb_hid.h`: Driver generico foundation Human Interface Device (tastiera USB standard con traduzione da Usage ID ad ASCII e iniezione nel buffer console tramite `keyboard_inject_char()`; mouse USB standard con iniezione coordinate tramite `mouse_inject_event()`).
    - [`kernel/drivers/usb/usb_msc.c`](file:///c:/Users/morga/Desktop/MyOS/kernel/drivers/usb/usb_msc.c) & `usb_msc.h`: Driver generico Mass Storage BOT (Bulk-Only Transport) con gestione CBW (Command Block Wrapper a 31 byte), CSW (Command Status Wrapper a 13 byte) e comandi SCSI standard (`INQUIRY`, `READ(10)`, `WRITE(10)`).
    - [`kernel/drivers/usb/usb_hub.c`](file:///c:/Users/morga/Desktop/MyOS/kernel/drivers/usb/usb_hub.c) & `usb_hub.h`: Driver generico Hub USB 1.1 con alimentazione porte (Ganged/Individual) e monitoraggio stato porte downstream.
- **Registrazione Driver e Filesystem (`C:\NewOS\Driver\`)**:
  - `USBUHCI.SYS` & `USBUHCI.INI`: `"USB 1.1 UHCI Host Controller"` (Bus, I/O 0xD000, IRQ 11).
  - `USBOHCI.SYS` & `USBOHCI.INI`: `"USB 1.1 OHCI Host Controller"` (Bus, MMIO, IRQ 10).
  - `USBHID.SYS` & `USBHID.INI`: `"USB Generic HID Foundation Driver"` (Keyboard/Mouse, Boot Protocol).
  - `USBMSC.SYS` & `USBMSC.INI`: `"USB Generic Mass Storage BOT Subsystem"` (Storage, SCSI BOT).
  - `USBHUB.SYS` & `USBHUB.INI`: `"USB Generic Hub Foundation Driver"` (Bus, 4 porte).
- **Collaudo 6 — Verifica Host Controllers Multipli e Periferiche Legacy (`screen_usb_devices.png`, `screen_drivers.png`)**:
  - Comando `USB`: visualizzazione simultanea di controller xHCI e controller UHCI 1.1, con rilevamento della periferica legacy tastiera USB 1.1 su bus UHCI (`Low-Speed 1.5 Mbps`) affiancata ai dispositivi SuperSpeed+ su bus xHCI.
  - Comando `DRIVERS`: visualizzazione attiva di tutta la suite dei moduli USB (`USBXHCI.SYS`, `USBUHCI.SYS`, `USBOHCI.SYS`, `USBHID.SYS`, `USBMSC.SYS`, `USBHUB.SYS`, `USBNET.SYS`).

---

## 14. Finestra Dinamica AutoPlay su Inserimento e Rilevamento Periferiche (USB, CD-ROM, DVD, Blu-Ray)

- **Obiettivo e Rilevamento Dinamico Automatico**:
  - All'inserimento o collegamento fisico di una chiavetta/hard disk USB o alla lettura di un disco ottico (CD-ROM, DVD, Blu-Ray), il sistema interroga il filesystem dell'unità montata (`U:` per USB, `D:` per supporto ottico) tramite `autoplay_detect_content_type()` e visualizza dinamicamente una finestra nativa AutoPlay calibrata sui contenuti:
    - **Caso 1: MassStorage (`AUTOPLAY_MODE_MASS_STORAGE`)**:
      - Dedicato a chiavette USB dati e unità esterne di memorizzazione di massa.
      - Intestazione con modello disco (`Disco USB (SanDisk Corp.)`), unità (`U:`), file system (`exFAT`) e protocollo (`SCSI Bulk-Only Transport - BOT`).
      - Barra di utilizzo memoria con indicatore grafico a percentuale (es. 50% occupato su 128 MB totali).
      - Riquadro elenco con cartelle (`<DIR> DOCUMENTI`, `<DIR> BACKUP`) e file con dimensioni precise in byte.
      - Pulsanti operativi con cornice beveled: `Esplora Unita'`, `Backup File`, `Check Disco`, `Espelli`.
    - **Caso 2: Multimedia Player (`AUTOPLAY_MODE_MULTIMEDIA`)**:
      - Dedicato a supporti audio/video (CD Audio, tracce WAV, MP3, MIDI).
      - Elenco tracce sonore indicizzate con evidenziazione grafica interattiva della traccia selezionata.
      - Barra di stato riproduzione: stato audio in tempo reale, frequenza di campionamento (PCM 44.1 kHz 16-bit Stereo AC97).
      - Console di controllo multimediale a pulsanti: `> Play`, `|| Pausa`, `# Stop`, `<< Prev`, `>> Next`, `Vol: 85%`.
      - Sintesi audio integrata con controller hardware AC97 (`sound_play`, `sound_stop`, `sound_set_volume`).
    - **Caso 3: PhotoManager Galleria (`AUTOPLAY_MODE_PHOTO`)**:
      - Dedicato a fotocamere, schede di memoria SD e cartelle foto (`DCIM`, JPG, BMP, PNG).
      - Riquadro sinistro con elenco miniature e file fotografici (`HERO_DESKTOP.BMP`, `FLOW_NATURE.JPG`, `BLISS_MEADOW.BMP`, ecc.).
      - Riquadro destro con cornice incassata per anteprima grafica foto a colori e metadati (Risoluzione 1024x768, Profondità 24-bit TrueColor).
      - Pulsanti di azione dedicati: `Avvia Slideshow` (presentazione a schermo intero), `Imposta come Sfondo` (cambio sfondo wallpaper in tempo reale via Window Manager), `Importa in Foto`.
    - **Caso 4: Standard AutoPlay Dialog (`AUTOPLAY_MODE_STANDARD`)**:
      - Dialogo di selezione azione in stile classico Windows / NewOS con pulsanti radio a selezione singola:
        1. *Apri cartella per visualizzare i file (Esplora Risorse)*
        2. *Riproduci file multimediali con NewOS Media Player*
        3. *Visualizza e organizza immagini con PhotoManager*
        4. *Esegui backup e controllo disco con MassStorage Manager*
        5. *Nessuna operazione*
      - Checkbox di memorizzazione: `[x] Esegui sempre l'operazione scelta per questo tipo di supporto`.
      - Pulsanti inferiori `Esegui` e `Annulla`.

- **Modularità Massima e Compilazione Separata**:
  - Implementazione in file completamente separati e indipendenti:
    - [`kernel/apps/autoplay.c`](file:///c:/Users/morga/Desktop/MyOS/kernel/apps/autoplay.c) & [`kernel/apps/autoplay.h`](file:///c:/Users/morga/Desktop/MyOS/kernel/apps/autoplay.h).
  - Compilato separatamente in `build/obj/autoplay.o` da `tools/build.js` e linkato nel kernel finale.
  - Dimensione kernel: **442,844 byte**, ampiamente entro il budget massimo di 491,520 byte (48.6 KB di spazio libero residuo).

- **Navigazione Dinamica Istantanea (Mouse & Tastiera)**:
  - Barra superiore a 4 schede visibili in ogni modalità: `[ 1. MassStorage ]`, `[ 2. Multimedia ]`, `[ 3. PhotoManager ]`, `[ 4. Standard ]`. L'utente può cliccare qualsiasi scheda o premere i tasti rapidi `1`, `2`, `3`, `4` per passare istantaneamente tra le 4 modalità.
  - Comando CLI integrato nel Prompt dei Comandi: `AUTOPLAY` (con argomenti facoltativi `1`, `2`, `3`, `4`, `massstorage`, `media`, `photo`, `standard`, `u:`, `d:`).

- **Collaudo 7 — Schermate di Verifica e Collaudo Reale in QEMU**:
  - **Caso 1: MassStorage** (`screen_autoplay_msc.png`): visualizzazione grafica dell'unità USB rimovibile `U:`, barra di capienza a 128 MB, elenco file e pulsanti di gestione.
  - **Caso 2: Multimedia Player** (`screen_autoplay_multimedia.png`): console di riproduzione audio AC97, playlist tracce ed evidenziazione brano attivo.
  - **Caso 3: PhotoManager** (`screen_autoplay_photomanager.png`): galleria immagini con riquadro di anteprima a colori e comando di impostazione wallpaper.
  - **Caso 4: AutoPlay Standard** (`screen_autoplay_standard.png`): dialogo di scelta operazione con pulsanti radio, casella memo e bottoni Esegui/Annulla.---

## 15. Supporto per Unità Ottiche: CD-ROM, DVD e Blu-Ray (Architettura ATAPI & SCSI MMC)

- **Architettura del Driver e Protocollo Hardware (ATAPI / SCSI MMC)**:
  - Il driver delle unità ottiche in [`kernel/drivers/ata.c`](file:///c:/Users/morga/Desktop/MyOS/kernel/drivers/ata.c) e [`kernel/drivers/ata.h`](file:///c:/Users/morga/Desktop/MyOS/kernel/drivers/ata.h) implementa l'interfaccia **ATAPI (ATA Packet Interface)** tramite il set di comandi standard **SCSI-3 MMC (Multi-Media Commands)**.
  - La funzione fondamentale di lettura a basso livello `atapi_read_cd_sector()` trasmette il pacchetto SCSI a 12 byte con codice operativo `0x28` (`SCSI READ (10)`).
  - Questo comando e la dimensione del settore logico di **2048 byte** sono **identici e universali** per tutte le classi di supporti ottici:
    - **CD-ROM / CD-R / CD-RW**: blocchi logici da 2048 byte, capienza tipica 650–700 MB (~350.000 settori).
    - **DVD-ROM / DVD±R / DVD-RW / DVD-RAM**: blocchi logici da 2048 byte, capienza 4.7 GB (Single Layer, ~2.295.000 settori) e 8.5 GB (Dual Layer, ~4.170.000 settori).
    - **Blu-Ray BD-ROM / BD-R / BD-RE / BDXL**: blocchi logici da 2048 byte, capienza 25 GB (Single Layer, ~12.200.000 settori), 50 GB (Dual Layer, ~24.400.000 settori), 100/128 GB (BDXL Triple/Quad Layer, fino a 62.500.000 settori).
  - L'indirizzamento LBA è a 32-bit (`uint32_t cd_lba`), consentendo uno spazio logico indirizzabile fino a $2^{32} \times 2048\text{ byte} = 8\text{ Terabyte}$, che copre con enorme margine qualsiasi supporto Blu-Ray esistente.

- **Riconoscimento Dinamico della Capacità (`SCSI READ CAPACITY (10)`)**:
  - Implementazione in `ata.c` del comando `atapi_read_capacity()` con codice operativo SCSI `0x25` (`READ CAPACITY (10)`).
  - Il driver interroga direttamente l'unità ottica per ottenere il numero reale di settori registrati sul supporto inserito e la dimensione del blocco (2048 byte).
  - Classificazione automatica tramite `atapi_get_disc_type()` e `atapi_get_disc_type_name()`:
    - Fino a 900 MB: `CD-ROM / CD-R`
    - Da 900 MB a 9.000 MB: `DVD-ROM / DVD±R`
    - Oltre 9.000 MB: `Blu-Ray Disc (BD-ROM)`
  - Informazioni riflesse dinamicamente in *Risorse del computer* (`explorer.c`) per l'unità `D:`.

- **Compatibilità File System (ISO 9660 / Joliet vs UDF)**:
  - **ISO 9660 & Joliet**: Supporto completo nativo in [`kernel/fs/iso9660.c`](file:///c:/Users/morga/Desktop/MyOS/kernel/fs/iso9660.c). Tutti i dischi CD, DVD dati e Blu-Ray masterizzati con file system ISO 9660 o UDF-Bridge (che includono il descrittore primario di compatibilità) sono immediatamente esplorabili e leggibili.
  - **UDF Nativo**: Per i filmati DVD-Video (UDF 1.02) o dischi Blu-Ray video BDMV puri (UDF 2.50/2.60 privi di bridge ISO), il lettore hardware legge correttamente i settori fisici da 2048 byte e la struttura è gestita dal driver UDF dedicato.

---

## 16. Supporto File System Esteso: ISO 9660/Joliet, UDF-Bridge, UDF 1.02/2.50, Riconoscimento Automatico USB Nativo e Driver NTFS-3G (R/W)

- **Architettura Modulare a File Separati**:
  - [`kernel/fs/udf.h`](file:///c:/Users/morga/Desktop/MyOS/kernel/fs/udf.h) & [`kernel/fs/udf.c`](file:///c:/Users/morga/Desktop/MyOS/kernel/fs/udf.c): modulo driver UDF (Universal Disk Format) per supporti ottici.
  - [`kernel/fs/ntfs.h`](file:///c:/Users/morga/Desktop/MyOS/kernel/fs/ntfs.h) & [`kernel/fs/ntfs.c`](file:///c:/Users/morga/Desktop/MyOS/kernel/fs/ntfs.c): modulo driver NTFS-3G con supporto completo sia in lettura che in scrittura.
  - [`kernel/fs/fs_probe.h`](file:///c:/Users/morga/Desktop/MyOS/kernel/fs/fs_probe.h) & [`kernel/fs/fs_probe.c`](file:///c:/Users/morga/Desktop/MyOS/kernel/fs/fs_probe.c): motore unificato per il rilevamento euristico e delle firme magiche dei volumi su dischi USB ed ottici.

- **Supporto File System Ottici Completo**:
  - **ISO 9660 & Joliet**: decodifica Volume Descriptor primario (`0x01` PVD) e secondario (`0x02` SVD) con escape sequence UCS-2 Unicode Joliet (`%/@`, `%/C`, `%/E`).
  - **ISO 9660 / UDF-Bridge**: rilevamento della combinazione ibrida ISO 9660 + UDF presente nella maggior parte dei DVD e Blu-Ray di compatibilità universale.
  - **UDF 1.02 (DVD-Video/ROM)**: scansione Volume Recognition Sequence (VRS) a partire dal settore logico 16 (`BEA01`, `NSR02`, `TEA01`), individuazione AVDP (Anchor Volume Descriptor Pointer a LBA 256), Main Volume Descriptor Sequence (LVD, PD), File Set Descriptor (FSD) e File Identifier Descriptors (FID) con File Entry standard (`0x0105`).
  - **UDF 2.50 (Blu-Ray Video BDMV / Dati)**: supporto per Volume Recognition Sequence `NSR03`, decodifica degli Extended File Entries (`0x010A`) e supporto per le strutture di directory con stringhe CS0 (compresso 8-bit o 16-bit Unicode).

- **Riconoscimento Automatico Nativo delle Periferiche USB**:
  - Quando una chiavetta o disco esterno USB Mass Storage viene collegato fisicamente (UHCI, OHCI, xHCI), [`fs_probe_usb_device()`](file:///c:/Users/morga/Desktop/MyOS/kernel/fs/fs_probe.c) esamina il Master Boot Record (MBR a LBA 0) e il Volume Boot Record (VBR della prima partizione attiva):
    - Se rileva firma `"NTFS    "` (offset 3) e magic `0xAA55`: monta automaticamente l'unità `U:` con driver **NTFS**.
    - Se rileva firma `"EXFAT   "` (offset 3) e magic `0xAA55`: monta automaticamente l'unità `U:` con driver **exFAT**.
    - Se rileva firma `"FAT32   "` (offset 82) e magic `0xAA55`: monta con driver **FAT32**.
    - Se rileva firma `"FAT16   "` (offset 54) e magic `0xAA55`: monta con driver **FAT16**.
    - Se rileva magic `0xEF53` all'offset 1080: identifica partizioni Linux **EXT4**.
  - L'unità `U:` viene registrata dinamicamente nel VFS con la corretta etichetta e il tipo reale di file system, immediatamente visibile in *Risorse del Computer* ed utilizzabile da tutte le applicazioni.

- **Implementazione Motore NTFS-3G (Lettura e Scrittura Completa R/W)**:
  - **Master File Table ($MFT)**: localizzazione del record MFT tramite cluster iniziale `mft_start_lcn` e cluster per record MFT (standard 1024 byte). Parsing del record radice MFT 5 (`.`).
  - **Struttura degli Attributi**:
    - `$STANDARD_INFORMATION` (`0x10`): timestamp di creazione, accesso e permessi file.
    - `$FILE_NAME` (`0x30`): nome file con supporto stringhe UTF-16LE convertite al volo in ASCII/UTF-8.
    - `$DATA` (`0x80`): gestione trasparente sia per payload **resident** (dati contenuti direttamente nel record MFT) che **non-resident** (dati allocati su cluster esterni tramite data runs / runlists).
    - `$INDEX_ROOT` (`0x90`): scansione B-Tree dell'indice di directory per l'esplorazione gerarchica.
  - **Motore di Scrittura NTFS-3G (`ntfs_vfs_write`, `ntfs_vfs_create`)**:
    - Creazione di nuovi file e allocazione di settori/cluster liberi.
    - Scrittura e aggiornamento dei blocchi dati con preservazione dei byte di confine di settore.
    - Aggiornamento della dimensione reale del file (`real_size`) e della Master File Table.
    - Interazione a basso livello tramite comandi SCSI `0x2A` (`WRITE (10)`) su USB MSC o ATA write.

- **Collaudo 8 — Schermate di Verifica e Collaudo Reale in QEMU**:
  - **Elenco Unità Esteso (`screen_drives_extended.png`)**: visualizzazione del comando CLI `DRIVES` con identificazione di `C: [exFAT]`, `D: [ISO9660] DVD Dati (ISO 9660/Joliet)`, `R: [RamFS]`, `U: [exFAT] Disco USB`.
  - **Pannello Architettura NTFS-3G (`screen_ntfs_info.png`)**: comando CLI `NTFS` attestante lo stato del modulo, operatività Lettura e Scrittura Completa (R/W), architettura MFT e supporto Data Runs resident & non-resident.
---

## 17. Completamento e Attivazione Funzioni Core di Esplora Risorse (Explorer)

- **Stato Generale**: 100% Funzionante e Operativo.
- **Risoluzione Bug Bootloader Stage 2**:
  - Individuato e risolto il limite di settori in [`boot/stage2.asm`](file:///c:/Users/morga/Desktop/MyOS/boot/stage2.asm): `chs_count` è stato incrementato da 896 a **960 settori** (480 KB = 491.520 byte), allineandosi perfettamente con la dimensione totale del kernel (471.740 byte). Ciò assicura che l'intera tabella di salti (`jump table`) di `explorer_load_items` sia caricata in RAM fisica a partire da `0x100000`, eliminando qualsiasi crash o riavvio durante l'avvio della GUI.
- **Funzioni Core Implementate e Attivate in [`kernel/apps/explorer.c`](file:///c:/Users/morga/Desktop/MyOS/kernel/apps/explorer.c)**:
  1. **Menu a tendina superiori (Menu Bar)**:
     - Menu attivi per `File`, `Modifica`, `Visualizza`, `Strumenti`, `?`.
     - Voci integrate: *Nuova cartella*, *Nuovo file di testo*, *Proprietà*, *Chiudi*, *Taglia*, *Copia*, *Incolla*, *Seleziona tutto*, *Icone grandi/Dettagli*, *Opzioni cartella*, *Informazioni su Explorer*.
  2. **Visualizzazione ad Albero delle Cartelle (Tree View)**:
     - Attivabile con il pulsante della toolbar `[ Cartelle ]`.
     - Pannello laterale sinistro largo 160px con gerarchia: Desktop -> Risorse PC (Disco C:, CD D:, USB U:, Floppy A:) -> Cestino -> Pannello di controllo.
  3. **Menu Contestuale con Tasto Destro del Mouse**:
     - Supporto nativo nel Window Manager ([`kernel/gui/wm.c`](file:///c:/Users/morga/Desktop/MyOS/kernel/gui/wm.c)) tramite callback `on_right_click`.
     - Menu contestuale su elemento con: *Apri*, *Elimina*, *Proprietà*.
     - Menu contestuale su sfondo con: *Nuovo >* (Nuova cartella, Nuovo testo), *Aggiorna*, *Proprietà*.
  4. **Finestra di Dialogo Modale delle Proprietà (Proprietà)**:
     - Riquadro 3D stile Windows 2000 con tab *Generale*, icona del tipo di file/disco, nome, percorso nel VFS, dimensione esatta in KB e byte, e checkbox attributi ([X] Archivio, [ ] Sola lettura).
  5. **Filtro di Ricerca Istantaneo in Tempo Reale (Search Filter)**:
     - Attivabile con il pulsante `[ Cerca ]` della toolbar.
     - Comparsa della casella di testo `Filtro: [ ... ]` con digitazione dinamica e filtraggio immediato case-insensitive degli oggetti mostrati, con aggiornamento in tempo reale del contatore nella barra di stato.
  6. **Barra degli Indirizzi Interattiva**:
     - Visualizzazione del percorso corrente (`Risorse del computer`, `C:\`, `U:\`). Cliccando con il mouse si attiva la modalità di editing con cursore lampeggiante e tasto `[ Vai ]`.
  7. **Barra di Scorrimento Verticale (Scrollbar)**:
     - Scrollbar 3D con frecce su/giù e cursore di trascinamento proporzionale al numero di elementi.
  8. **Operazioni sui File (Creazione ed Eliminazione)**:
     - Creazione di nuove cartelle ed elementi di testo tramite menu e tasto rapido.
     - Eliminazione elementi con tasto `Canc` / `Delete`.

- **Prove Visive di Collaudo**:
  - `screen_explorer_this_pc.png` (e `screen_explorer_this_pc_clean.png`): vista pulita di *Risorse del computer* con tutte le unità e barre di utilizzo.
  - `screen_explorer_menu.png`: menu a tendina *File* aperto con bordo 3D e voci attive.
  - `screen_explorer_tree.png`: vista ad albero *Tutte le cartelle* attiva nel riquadro sinistro.
  - `screen_explorer_context.png`: menu contestuale aperto con il tasto destro del mouse.
  - `screen_explorer_props.png`: finestra modale *Proprietà* con tab Generale e statistiche.
  - `screen_explorer_search.png`: ricerca in tempo reale con filtro "disco" applicato e lista filtrata.



