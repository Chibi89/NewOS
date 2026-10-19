# Piano di Sviluppo e Architettura: NewOS V1
**Copyright (C) @ Newspoint Software - NewSoft - NewOS V1**

Questo documento definisce la roadmap completa, l'architettura tecnica e la suddivisione in fasi per la trasformazione del sistema operativo in **NewOS V1**, introducendo l'architettura a componenti e driver modulari su file, la suite di rete, il registro di sistema con sicurezza avanzata, la gestione estesa dei file, le nuove applet del Pannello di Controllo, i driver hardware primari, i servizi di sistema, gli strumenti essenziali, l'ambiente di produttività, il self-hosting, la diagnostica, la suite di amministrazione, l'accessibilità, l'ibernazione e la distribuzione software.

---

## 1. Visione Architetturale e Specifiche

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                                       NewOS V1                                         │
│                      (C) Newspoint Software - NewSoft                                  │
├────────────────────────────────────────────────────────────────────────────────────────┤
│  INTERFACCIA UTENTE & ESPERIENZA VISIVA (MODERN UX & POLISH)                           │
│  - Window Manager con Liquid Glass & Classic 2000, Snap Assist (Docking 50%/100%)       │
│  - Alt+Tab con Anteprime Miniature Grafiche in tempo reale, Desktop Virtuali Multipli   │
│  - Centro Notifiche a Scomparsa (Toast Notifications), Night Shift (Filtro Luce Blu)   │
│  - Ricerca Globale Spotlight (Win+Spazio), Storico Appunti Esteso (Win+V)              │
│  - Desktop Gadgets & Sticky Notes (Note adesive colorate, Orologio analogico svizzero)  │
│  - Gestore Temi Personalizzati .THEME e Modalità Kiosk / Fullscreen Console            │
├────────────────────────────────────────────────────────────────────────────────────────┤
│  SUITE AMMINISTRAZIONE & GESTIONE SISTEMA (ADMINISTRATOR SUITE)                        │
│  - USERMGR.EXE (Gestione Utenti, Gruppi Administrators/Power/Standard/Guest)           │
│  - User Account Control (UAC Secure Prompt) & comando sudo per elevazione privilegi    │
│  - GPEDIT.EXE (Criteri di Gruppo, Politiche di Sicurezza e Restrizioni File/Registro)  │
│  - SERVICES.EXE (Console Gestione Servizi Kernel: Avvia, Arresta, Riavvia Daemon)      │
│  - COMPMGMT.EXE (Console Unificata Gestione Computer MMC a 3 riquadri)                 │
│  - PERFMON.EXE (Monitor Prestazioni CPU, I/O Disco, Throughput Rete, Mappa RAM Heap)   │
│  - CHKDSK.EXE (Controllo Integrità File System exFAT, Riparazione Cluster Orfani)      │
│  - PROCMON.EXE (Process Tree, Librerie/Driver Agganciati, Socket di Rete Attivi)       │
├────────────────────────────────────────────────────────────────────────────────────────┤
│  APPLICAZIONI & SUITE DI SISTEMA                                                       │
│  - File Explorer con Menu Contestuale dedicato per estensione (.bmp, .wav, .exe, .sys) │
│  - REGEDIT.EXE / TOOL.EXE (Editor Registro con Alert di Sicurezza e Backup Automatico) │
│  - TASKMGR.EXE, DISKMGMT.EXE, IMGVIEW.EXE, MEDIAPLR.EXE, EVENTVWR.EXE, ZIPMGR.EXE       │
│  - NEWCLI.EXE (Terminale Scripting & Colori ANSI), WEBBROWSER.EXE (Client HTTP Web)    │
│  - NETSHARE (Mini-Server Web Locale per Drag & Drop File bidirezionale con PC reale)   │
│  - SOUNDSTUDIO.EXE (Sintetizzatore Audio con Pianoforte virtuale ed Export .WAV)       │
│  - NEWPKG.EXE / APPSTORE.EXE (Gestore Pacchetti .NPK per installare software a 1 clic)│
│  - PDFVIEW.EXE (Visualizzatore Documenti PDF & Manuali di Sistema integrati)           │
│  - ACCESSIBILITY.EXE (Lente d'ingrandimento 2x/4x/8x, Temi Alto Contrasto, Sticky Keys)│
│  - Retro Games: DOOM.EXE (Porting 1993 a 64-bit), MINESWEEPER.EXE, SOLITAIRE.EXE       │
│  - Strumento Cattura Schermo: SNIP.EXE con salvataggio PNG/BMP                         │
│  - Suite Pannello di Controllo (8 Nuovi Moduli di Gestione Avanzata)                   │
├────────────────────────────────────────────────────────────────────────────────────────┤
│  AMBIENTE DI SVILUPPO NATIVO (SELF-HOSTING)                                            │
│  - Compilatore C Nativo Integrato (TCC.EXE 64-bit Tiny C Compiler per NewOS)          │
│  - NewOS Software Development Kit (newos.h, API Export Table, Syscall Jump Table)      │
│  - Scrittura, compilazione ed esecuzione di programmi C DIRETTAMENTE dentro NewOS      │
├────────────────────────────────────────────────────────────────────────────────────────┤
│  SERVIZI PRIMARI DI SISTEMA (SYSTEM SERVICES & RESILIENCE)                             │
│  - Scheduler Preemptive Multi-Threading & Time Slicing a 100 Hz                        │
│  - Storage Mount Manager (Assegnazione dinamica C:, D:, E: USB, Z: Ramdisk)            │
│  - Logon & Security Service (Schermata Login, Win+L Lock Screen, UAC Privileges)       │
│  - System Event Logger (C:\SYSTEM\LOGS\SYSTEM.LOG) & NTP Time Sync Service             │
│  - Punti di Ripristino del Sistema (System Restore Points & Snapshots)                 │
│  - Schermata Diagnostica di Arresto Elegante ("Green Screen of Life" Crash Handler)    │
│  - HIBERNATE.SYS (Ibernazione rapida con salvataggio RAM su disco e avvio in < 1s)     │
│  - RAMDISK.SYS (Unità disco virtuale Z:\ a 2+ GB/s per compilazioni ultra-rapide)      │
├────────────────────────────────────────────────────────────────────────────────────────┤
│  SOTTOSISTEMA DRIVER MODULARE                                                          │
│  - File System exFAT: C:\SYSTEM\DRIVERS\ e C:\SYSTEM\DRIVERS\NET\                     │
│  - Coppia per ogni driver: [nomedriver].SYS (Binario/Entry Points) + [nomedriver].INI  │
│  - Driver Hardware Primari: AHCI.SYS (SATA DMA), USB_XHCI.SYS, HDAUDIO.SYS, APIC.SYS   │
│  - Driver Input Speciali: JOYSTICK.SYS (Controller USB/GamePort per DOOM ed emulatori) │
│  - Stack di Rete: [protocollo].SYS.NET (ETH, IP, TCP, UDP, DHCP, TLS) + [prot].INI     │
│  - Driver Video Avanzato: VGA.SYS (Supporto Risoluzioni Dinamiche VESA/BGA da 800x600  │
│    a 1920x1080 Full HD e cursore hardware VGA_HW.SYS)                                  │
├────────────────────────────────────────────────────────────────────────────────────────┤
│  REGOLA ARCHITETTURALE RIGOROSA DEL FILE SYSTEM (C:\SYSTEM vs C:\APPS)                 │
│  - C:\SYSTEM\ : ospita TUTTI gli eseguibili di sistema (.EXE), tutte le applet (.CPL) │
│    del Pannello di Controllo, i driver (.SYS/.INI), lo stack di rete (.SYS.NET),      │
│    i servizi (.SYS), il registro (REGISTRY.DAT), i log (.EVT/.LOG) e gli asset.        │
│  - C:\APPS\   : riservata ESCLUSIVAMENTE alle applicazioni utente e di terze parti     │
│    (software installati tramite NEWPKG, porting ludici come DOOM, tool utente).        │
│    Nessun CPL o EXE di sistema deve risiedere in C:\APPS\.                             │
├────────────────────────────────────────────────────────────────────────────────────────┤
│  CORE KERNEL 64-BIT                                                                    │
│  - Long Mode, IDT, APIC/IO-APIC, Paging, Heap Memory Manager (64 MB dinamico)          │
│  - Tabella di Export API / Syscall (Dynamic Module Loader per .SYS ed .EXE)            │
│  - Gestore File System VFS + exFAT nativo Microsoft con R/W completo cluster e entry   │
│  - Power Management & Sincronizzazione ACPI/APM                                        │
└────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Standard di Organizzazione del File System (C:\)

Al fine di garantire massima sicurezza, isolamento e coerenza con i sistemi operativi moderni, la distribuzione dei file nell'unità disco `C:\` adotta la seguente rigida convenzione:

### 1. `C:\SYSTEM\` (Riservata al Sistema Operativo)
Tutti i componenti di sistema essenziali risiedono qui:
* **Eseguibili di Sistema (.EXE)**:
  - `EXPLORER.EXE`, `REGEDIT.EXE` (o `TOOL.EXE`), `TASKMGR.EXE`, `DISKMGMT.EXE`, `CHKDSK.EXE`, `EVENTVWR.EXE`, `USERMGR.EXE`, `GPEDIT.EXE`, `SERVICES.EXE`, `COMPMGMT.EXE`, `PROCMON.EXE`, `NOTEPAD.EXE`, `CALC.EXE`, `TERMINAL.EXE` (o `NEWCLI.EXE`), `IMGVIEW.EXE`, `MEDIAPLR.EXE`, `ZIPMGR.EXE`, `SNIP.EXE`, `SOUNDSTUDIO.EXE`, `NEWPKG.EXE`, `ACCESSIBILITY.EXE`.
* **Applet del Pannello di Controllo (.CPL)**:
  - `DISPLAY.CPL`, `POWER.CPL`, `NETWORK.CPL`, `DEVMGMT.CPL`, `SECURITY.CPL`, `DATEANDTIME.CPL`, `SOUND.CPL`, `PROGRAMS.CPL`, `ACCESSIBILITY.CPL`.
* **Sottodirectory di Sistema**:
  - `DRIVERS\`: Driver modulari hardware (`.SYS` + `.INI`).
  - `NET\` (o `DRIVERS\NET\`): Stack protocolli di rete (`.SYS.NET` + `.INI`).
  - `SERVICES\`: Demoni e servizi in background (`SCHEDULER.SYS`, `MOUNTMGR.SYS`, `EVENTLOG.SYS`, `TIMESYNC.SYS`, `NETSHARE.SYS`, `SPOOLER.SYS`).
  - `CONFIG\`: Database del Registro (`REGISTRY.DAT`, `REGISTRY.BAK`), `SYSTEM.INI`, `SECURITY.INI`.
  - `LOGS\`: Registri eventi e audit (`SYSTEM.EVT`, `SECURITY.EVT`, `APPLICATION.EVT`).
  - `DEV\TCC\`: Ambiente di sviluppo C nativo (Tiny C Compiler 64-bit `TCC.EXE`, header `INCLUDE\`, librerie `LIB\`).
  - `DEV\TCC\CONFIG\`: File di configurazione del compilatore (`TCC.CFG` per percorsi include, librerie, flag, cartelle temporanee e target x86_64).
  - `DEV\TCC\TEMP\` e `DEV\TCC\TMP\`: Directory per file temporanei e oggetti intermedi (.o, .s, preprocessed).
  - `WALLPAPERS\`: Sfondi fotografici e temi (`.THEME`).
  - `SOUNDS\`: Suoni di avvio, spegnimento e notifiche (`.WAV`).
  - `FONTS\`: Font grafici di sistema.
  - `ICONRESOURCE.DLL` / `ICONRESOURCE.DL`: Libreria di risorse grafiche per le icone di sistema della GUI (ex `ICONS.BIN`).

### 2. `C:\APPS\` (Riservata ai Programmi Utente e Terze Parti)
Nessun file o componente di sistema è memorizzato in questa cartella. Essa è dedicata unicamente al software applicativo installato dall'utente:
* Pacchetti applicativi installati tramite `NEWPKG.EXE` / `APPSTORE.EXE`.
* Porting e giochi indipendenti (es. `DOOM\DOOM.EXE`, emulatori).
* Progetti software compilati dall'utente.

### 3. `C:\USER\` (Dati Personali e Profili)
* `DEFAULT\`: Profilo modello per nuovi utenti.
* `ADMIN\` e `MORGAN\`: Profili con le rispettive cartelle `DESKTOP\`, `DOCUMENTS\`, `PICTURES\`, `MUSIC\`, `DOWNLOADS\`, `USER.CFG`.

### 4. `C:\RECYCLE\` (Cestino con Ripristino)
* `INFO.DAT`: Database protetto dei metadati dei file cestinati (percorso originale, data/ora, UID proprietario). **File di sistema ad accesso ristretto: non è visibile né accessibile agli utenti standard se non si possiedono i privilegi di Amministratore o previa abilitazione/elevazione da terminale tramite `sudo`.**
* `[File eliminati temporanei]`: File cestinati e protetti con supporto al ripristino o svuotamento definitivo.

### 5. `C:\DOCS\` (Documentazione e Guide)
* `LEGGIMI.TXT`, `BENVENUTO.TXT`, `MANUALE_NEWOS.TXT`, `GUIDA_COMANDI.TXT`.

---

## 3. Fasi del Piano di Sviluppo

### FASE 1: Rebranding Ufficiale a "NewOS V1"
* **Obiettivo**: Aggiornare l'intera identità del sistema operativo a **NewOS V1** con copyright `@ Newspoint Software - NewSoft - NewOS V1`.
* **Componenti coinvolti**:
  - `boot/mbr.asm` e `boot/stage2.asm`: banner di caricamento BIOS e menu di boot.
  - `kernel/kernel.c`: banner del kernel su porta seriale `0x3F8` e prompt di inizializzazione.
  - `kernel/gui/wm.c`: banner verticale del Menu Start (`NewOS`), didascalie e taskbar.
  - `kernel/apps/about.c`: finestra "Informazioni su NewOS", con logo, versione **NewOS V1** e copyright **@ Newspoint Software - NewSoft**.
  - `tools/build.js` e `tools/mkexfat.js`: intestazioni di generazione disco e file `C:\DOCS\LEGGIMI.TXT`, `C:\DOCS\BENVENUTO.TXT`, `C:\USER\USER.CFG`.

---

### FASE 2: Architettura Modulare dei Driver (`[driver].SYS` + `[driver].INI`)
* **Obiettivo**: Ogni driver hardware e software diventa un'entità autonoma su file composta da un binario `.SYS` e un descrittore di configurazione `.INI` memorizzati in `C:\SYSTEM\DRIVERS\`.
* **Struttura dei File Driver**:
  - `PCI.SYS` / `PCI.INI`: Enumeratore del bus PCI e scansione periferiche.
  - `ATA.SYS` / `ATA.INI`: Driver del controller IDE/ATA hard disk e CD-ROM.
  - `VGA.SYS` / `VGA.INI`: Driver della scheda video, gestione modalità e accelerazione.
  - `AC97.SYS` / `AC97.INI`: Driver audio nativo per codec Intel AC'97.
  - `PS2MOUSE.SYS` / `PS2MOUSE.INI`: Driver per mouse PS/2 standard.
  - `PS2KBD.SYS` / `PS2KBD.INI`: Driver tastiera PS/2 e mappatura caratteri.
  - `RTC.SYS` / `RTC.INI`: Orologio di sistema in tempo reale e calendario CMOS.
  - `POWER.SYS` / `POWER.INI`: Sottosistema di risparmio energia, throttling e spegnimento.
* **Formato del file `.INI` di configurazione standard**:
  ```ini
  [Driver]
  Name=NewSoft AC97 Audio Subsystem
  Version=1.0.0
  Author=Newspoint Software
  Type=Audio
  VendorID=0x8086
  DeviceID=0x2415
  AutoStart=1
  Status=Active

  [Hardware]
  IRQ=11
  IO_Base=0x1000
  DMA=1

  [Settings]
  SampleRate=48000
  Volume=85
  Mute=0
  ```
* **Modulo Kernel `driver_mgr.c` / `driver_mgr.h`**:
  - Scansione e lettura della directory `C:\SYSTEM\DRIVERS\`.
  - Parser integrato per le sezioni `.INI` (`[Driver]`, `[Hardware]`, `[Settings]`).
  - Registro interno di stato dei driver (Caricato, Attivo, Sospeso, Errore).
  - Esposizione di API standard (`driver_get_list()`, `driver_reload()`, `driver_get_info()`).

---

### FASE 3: Driver Video Avanzato e Risoluzioni Schermo Dinamiche [COMPLETATO]
* **Obiettivo**: Permettere a NewOS di supportare e commutare tra molteplici risoluzioni grafiche avanzate.
* **Funzionalità di `VGA.SYS`**:
  - Modalità supportate ed effettivamente testate:
    - **800x600** @ 32bpp (Modalità compatta/retro)
    - **1024x768** @ 32bpp (Risoluzione standard predefinita)
    - **1280x720** @ 32bpp (HD 16:9 widescreen)
    - **1280x1024** @ 32bpp (SXGA 5:4)
    - **1440x900** @ 32bpp (WXGA+ 16:10 widescreen)
    - **1920x1080** @ 32bpp (Full HD 1080p widescreen)
  - Controllo dei registri Bochs/QEMU/VirtualBox VBE BGA I/O (`0x01CE` / `0x01CF`):
    - Impostazione al volo di larghezza, altezza, pitch e abilitazione senza necessità di riavviare il kernel (`vga_set_resolution`).
  - Sottosistema grafico (`graphics.c` / `mouse.c`):
    - Backbuffer dinamico fino a 1920x1080 (8.29 MB) allocato nell'heap kernel da 64 MB.
    - Sfondo e liquid glass ricalcolati automaticamente alle nuove dimensioni.
    - `mouse_set_bounds(w, h)` per adattare i confini del cursore mouse hardware PS/2.
  - Window Manager reattivo (`wm.c`):
    - Ricalcolo dinamico dell'area desktop, riposizionamento della Taskbar in fondo allo schermo (`y = new_h - 28`), orologio e system tray a destra, confinamento delle finestre attive.
  - Aggiornamento della scheda *Schermo* (`display.c`):
    - Nuova scheda `" Risoluzione "` con radio box per le 6 modalità, riquadro monitor con anteprima visiva in scala, pulsanti OK/Applica/Annulla, navigazione completa sia via mouse che via tastiera (`0..5`, Invio).
  - Prompt dei comandi (`cmd.c`):
    - Comandi `res` / `resolution` e `res <id>` / `res <LxA>` (es. `res 1280x720`).
  - File di configurazione driver `C:\SYSTEM\DRIVERS\VGA.INI` generato e aggiornato.

---

### FASE 4: Stack di Rete e Driver Net Dedicati (`[protocollo].SYS.NET`) [COMPLETATO & VERIFICATO SU HARDWARE REALE]
* **Obiettivo**: Implementare il driver di rete e i protocolli fondamentali con acquisizione **completamente dinamica e reale** dell'indirizzo IP tramite DHCP e risoluzione ARP, eliminando qualsiasi valore o fallback statico/fittizio.
* **Architettura Rete Dinamica Realizzata e Testata (`C:\SYSTEM\DRIVERS\NET\`)**:
  - Driver Scheda di Rete Fisico:
    - `E1000.SYS` / `E1000.INI`: Intel 82540EM Gigabit Ethernet (rilevazione PCI automatica, descrittori DMA RX/TX statici allineati a 128 byte come da specifiche Intel, lettura MAC autentico da registri RAL/RAH o EEPROM).
    - `RTL8139.SYS` / `RTL8139.INI`: Realtek Fast Ethernet (fallback automatico su I/O ports, buffer circolare 8KB).
  - Moduli Protocollo Separati (`.SYS.NET` e `.INI`):
    - `ETH.SYS.NET` / `ETH.INI`: Framing Ethernet II, gestione indirizzi MAC, risoluzione tabella ARP dinamica (RFC 826).
    - `IP.SYS.NET` / `IP.INI`: Protocollo IPv4 (RFC 791), checksum Internet, routing dinamico tramite gateway ottenuto da DHCP, risposta e invio ICMP Echo (RFC 792) con estrazione TTL dal pacchetto ricevuto.
    - `UDP.SYS.NET` / `UDP.INI`: Datagrammi UDP per porte client/server (RFC 768).
    - `TCP.SYS.NET` / `TCP.INI`: Protocollo TCP affidabile (RFC 793), handshake a 3 vie (SYN/ACK), tracciamento sequence/ack, sliding window.
    - `DHCP.SYS.NET` / `DHCP.INI`: Client DHCP dinamico (RFC 2131) su UDP 67/68 (Discover, Offer, Request, Ack). All'avvio la scheda parte con `0.0.0.0` e negozia dinamicamente l'assegnazione dell'indirizzo IP, subnet mask, gateway e DNS dal server DHCP virtuale o fisico.
    - `TLS.SYS.NET` / `TLS.INI`: Substrato di sicurezza e crittografia per record protetti (SHA-256, HMAC-SHA256, PRNG entropico).
* **Componenti Kernel (`kernel/net/` e `kernel/drivers/net/`)**:
  - Ring buffer per trasmissione (TX) e ricezione (RX) con polling continuo non-bloccante (`net_poll()`).
  - BSD Socket API (`net_socket()`, `net_bind()`, `net_listen()`, `net_connect()`, `net_send()`, `net_recv()`, `net_close()`, `net_ping()`).
* **Tool CLI Integrati nel Prompt dei Comandi (`cmd.c`)**:
  - `ipconfig` (o `ifconfig`): Mostra parametri reali rilevati dalla scheda (MAC address hardware, stato link, stato DHCP Bound, IPv4, Subnet, Gateway, DNS e contatore pacchetti TX/RX).
  - `ping <ip>`: Invio di 4 pacchetti ICMP Echo con attesa e ricezione di risposte reali dal controller di rete. Calcolo tempo RTT in millisecondi e lettura TTL reale dal pacchetto IP (`TTL=255`). Stampa `Richiesta scaduta.` se l'host non risponde e statistiche autentiche con % di perdita pacchetti.
  - `netstat`: Visualizzazione tabellare delle connessioni TCP/UDP realmente aperte; stampa `Nessuna connessione TCP attiva.` se non vi sono socket aperti.
  - `dhcp`: Verifica stato leasing o rinegoziazione dinamica con il router di rete.
* **Integrazione GUI**:
  - `devmgmt.c` (Gestione Periferiche): Visualizzazione nella lista dei driver hardware con icona di rete (`ICON_NET`).

---

### FASE 5: Tool di Sistema Valori di Registro (`REGEDIT.EXE` / `TOOL.EXE`) [COMPLETATO & VERIFICATO]
* **Obiettivo**: Creare un potente strumento grafico per visualizzare e modificare tutti i valori e le configurazioni di sistema (stile `regedit`), con controlli di sicurezza preventiva.
* **File del Registro**: `C:\SYSTEM\REGISTRY.DAT`
  - Struttura gerarchica ad albero:
    - `HKEY_LOCAL_MACHINE\SYSTEM` (parametri kernel, memoria, risoluzione schermo)
    - `HKEY_LOCAL_MACHINE\DRIVERS` (configurazioni e puntatori ai driver `.SYS`)
    - `HKEY_LOCAL_MACHINE\NETWORK` (impostazioni IP, DNS, protocolli attivi)
    - `HKEY_CURRENT_USER\DESKTOP` (sfondo attivo, tema visivo, icone)
    - `HKEY_CLASSES_ROOT` (estensioni file e associazioni verbi)
* **Interfaccia Grafica di `REGEDIT.EXE`**:
  - Pannello Sinistro: Tree-View gerarchica navigabile con nodi espandibili `[+]` e `[-]`.
  - Pannello Destro: Tabella valori (`Nome`, `Tipo` [REG_SZ, REG_DWORD, REG_BINARY], `Dati`).
* **Meccanismo di Sicurezza Avanzata e Finestra di Conferma/Alert**:
  - Prima di salvare qualsiasi modifica o cancellazione:
    - Finestra modale: **"Avviso di Sicurezza del Registro - NewOS"**.
    - Mostra: Chiave bersaglio, Valore attuale, Nuovo valore proposto, Livello di Rischio (Basso, Medio, Critico per i driver di sistema).
    - Messaggio informativo con icona di attenzione `(!)` su come la modifica impatterà la stabilità del sistema.
    - Conferma esplicita a due passaggi (`[ Conferma Modifica ]` / `[ Annulla ]`).
    - Audit Trail: ogni operazione viene registrata con timestamp in `C:\SYSTEM\REGISTRY.LOG`.

---

### FASE 6: Menu Contestuali Dedicati per Tipo di File e Gestione Tag/Proprietà
* **Obiettivo**: Dotare sia il Desktop che Esplora Risorse di un menu contestuale specializzato in base all'estensione del file selezionato, con supporto completo alle operazioni file e finestra Proprietà.
* **Voci del Menu Contestuale per Categoria**:
  - **File Immagine (`.BMP`, `.JPG`, `.PNG`)**:
    - *Imposta come sfondo del desktop*
    - *Visualizza immagine*
    - *Copia* / *Sposta* / *Rinomina* / *Elimina*
    - *Proprietà...*
  - **File Audio (`.WAV`)**:
    - *Riproduci suono*
    - *Imposta come suono di avvio*
    - *Copia* / *Rinomina* / *Elimina*
    - *Proprietà...*
  - **File di Testo e Configurazione (`.TXT`, `.INI`, `.CFG`, `.LOG`)**:
    - *Apri con Blocco Note*
    - *Copia* / *Sposta* / *Rinomina* / *Elimina*
    - *Proprietà...*
  - **File Eseguibili (`.EXE`)**:
    - *Esegui applicazione*
    - *Esegui come Amministratore*
    - *Crea collegamento sul desktop*
    - *Proprietà...*
  - **File Driver di Sistema (`.SYS`, `.SYS.NET`)**:
    - *Informazioni Driver*
    - *Modifica file di configurazione (.INI)*
    - *Proprietà...*
  - **Cartelle**:
    - *Apri*
    - *Esplora in nuova finestra*
    - *Rinomina* / *Elimina*
    - *Proprietà...*
* **Operazioni su File**:
  - **Rinomina**: finestra modale con campo di testo modificabile e validazione caratteri exFAT.
  - **Elimina**: finestra di conferma con opzione di spostamento nel Cestino o eliminazione definitiva.
  - **Copia e Sposta**: gestione degli appunti (clipboard) per incollare file tra cartelle diverse.
  - **Finestra Proprietà File**:
    - Nome del file, Icona in grande, Tipo di elemento.
    - Percorso fisico sul disco `C:\`.
    - Dimensione in byte, KB e MB, e cluster allocati su exFAT.
    - Data e ora di creazione e ultima modifica.
    - Attributi di file: Sola lettura, Archivio, Sistema, Nascosto.
    - Sezione Tag e Descrizione personalizzata del file.

---

### FASE 7: Suite dei Nuovi Strumenti del Pannello di Controllo
Implementazione di 8 nuove applet dedicate nel Pannello di Controllo per la gestione completa del sistema operativo:

1. **Gestione Variabili d'Ambiente (`Environment Variables Management`)**:
   - Visualizzazione divisa tra *Variabili Utente* (`USERNAME`, `USERPROFILE`, `HOMEDRIVE`) e *Variabili di Sistema* (`PATH`, `OS_NAME`, `SYSTEMROOT`, `TEMP`).
   - Pulsanti per aggiungere, modificare ed eliminare variabili con validazione immediata.
2. **Gestione Funzionalità e Servizi (`Enable/Disable Function Management`)**:
   - Centro di controllo per abilitare/disabilitare funzionalità di NewOS:
     - Sottosistema Audio AC'97
     - Effetti Visivi Liquid Glass e Trasparenze
     - Stack di Rete e Connettività
     - Dynamic Power Throttling & Standby
     - Avvio Guidato USB Live
3. **Gestione Rete (`Net Management`)**:
   - Monitoraggio schede di rete installate e stato del link.
   - Configurazione indirizzo IP statico o DHCP, Maschera di Sottorete, Gateway predefinito e server DNS.
   - Stato dei moduli `.SYS.NET` attivi e utility di test Ping integrata.
4. **Data, Ora e Regione (`Day & Region Management`)**:
   - Orologio a lancette e digitale in tempo reale sincrono col chip RTC hardware.
   - Calendario mensile interattivo per selezionare giorno, mese e anno.
   - Impostazione del fuso orario (Timezone) e dei formati regionali di data, ora e valuta.
5. **Gestione Audio Avanzata (`Audio Management`)**:
   - Mixer canali completo: cursore Volume Master, Volume Wave/PCM, Volume Altoparlante interno (Beeper).
   - Controllo bilanciamento Sinistro / Destro e casella "Disattiva audio" (Mute).
   - Pulsante "Test Audio" che invia un campione stereo di prova all'hardware AC97.
6. **Gestione Dispositivi e Driver (`Driver Management`)**:
   - Visualizzatore gerarchico di tutto l'hardware riconosciuto nel PC: Processore x86_64, Controller ATA, Scheda Video, Controller Audio, Scheda di Rete, Mouse e Tastiera.
   - Per ogni periferica: dettagli del file `.SYS` associato, visualizzazione/modifica del file `.INI`, visualizzazione porta I/O e linea IRQ, e pulsante "Ricarica Driver".
7. **Gestione Certificati e Autorizzazioni (`Certificate & Authorization Management`)**:
   - Gestore della sicurezza e dell'integrità del sistema.
   - Elenco dei certificati di autorizzazione approvati da *Newpoint Software*.
   - Livelli di autorizzazione per l'esecuzione di app ed estensioni driver (Amministratore, Utente, Ristretto).
8. **Gestione Installazione e Disinstallazione (`Installer/Uninstaller Management`)**:
   - Gestore dei programmi installati nel sistema: elenco con nome, versione, autore e spazio occupato sul disco `C:`.
   - Pulsante "Installa nuova applicazione da disco..." per configurare pacchetti e aggiungere icone al desktop o al menu Start.
   - Pulsante "Disinstalla" per rimuovere in sicurezza i file e liberare cluster exFAT.

---

### FASE 8: Driver Hardware Primari di Nuova Generazione
* **Obiettivo**: Modernizzare il supporto hardware di NewOS consentendo l'avvio su PC reali moderni e velocizzando drasticamente l'I/O.
* **Driver da implementare**:
  1. **Controller SATA & AHCI (`AHCI.SYS` / `AHCI.INI`)**:
     - Supporto nativo al controller Advanced Host Controller Interface (AHCI) con bus PCI.
     - Operazioni di lettura e scrittura tramite Direct Memory Access (DMA a 32-bit e 64-bit tramite PRDT - Physical Region Descriptor Table).
     - Riduzione del carico CPU dall'attuale 100% (PIO polling) a < 2% con velocità disco 20x-50x superiore.
  2. **Controller e Bus USB (`USB_XHCI.SYS`, `USB_EHCI.SYS` / `.INI`)**:
     - Driver per controller host USB 3.0 (XHCI) e USB 2.0 (EHCI).
     - Sottodriver **USB HID** (`USB_HID.SYS`): supporto mouse e tastiere USB senza dipendere dall'emulazione PS/2 del BIOS.
     - Sottodriver **USB Mass Storage** (`USB_STORAGE.SYS`): rilevamento di chiavette USB e dischi rimovibili.
  3. **Audio HD Audio ad Alta Definizione (`HDAUDIO.SYS` / `HDAUDIO.INI`)**:
     - Supporto al controller Intel High Definition Audio (HDA) e codec Realtek ALC.
     - Supporto a campionamenti 44.1 kHz, 48 kHz, 96 kHz a 16/24 bit stereo e multicanale.
  4. **Driver Cursore Hardware Video (`VGA_HW.SYS` / `VGA_HW.INI`)**:
     - Sprite del puntatore mouse gestito direttamente dal chip grafico (BGA / VBE / VirtIO GPU).
     - Azzeramento totale dello sfarfallio (flicker) e render a 60+ FPS indipendentemente dal carico della CPU.
  5. **Controller APIC & IO-APIC (`APIC.SYS` / `APIC.INI`)**:
     - Abilitazione del Local APIC e dell'I/O APIC a 24+ linee di interrupt, superando le 15 linee limitate del vecchio PIC 8259.
  6. **Supporto Multi-Core SMP (`SMP.SYS` / `SMP.INI`)**:
     - Risveglio e inizializzazione dei core secondari della CPU tramite IPI (Inter-Processor Interrupts).
     - Assegnazione dei thread di rete e I/O disco ai core dedicati.

---

### FASE 9: Servizi Primari di Sistema (Core System Services)
* **Obiettivo**: Strutturare i daemon e i servizi di background necessari per un sistema operativo multitasking stabile e sicuro.
* **Servizi da implementare**:
  1. **Scheduler Preemptive Multi-Threading**:
     - Gestione di processi e thread multipli con time-slicing basato sul PIT timer a 100 Hz o APIC timer.
     - Code di priorità (Realtime, High, Normal, Idle).
     - Isolamento dello stack di esecuzione per evitare che operazioni pesanti blocchino la GUI.
  2. **Mount Manager & Storage Subsystem**:
     - Demone di rilevamento volumi: scansione delle tabelle partizioni MBR e GPT.
     - Assegnazione dinamica delle lettere di unità: `C:` (disco di sistema), `D:` (seconda partizione o lettore CD-ROM), `E:`/`F:` (pendrive USB), `Z:` (RAM disk ad altissima velocità).
  3. **Servizio Sessione Utente, Login e Blocco (`LOGON_SVC`)**:
     - Schermata iniziale di Login con inserimento password per l'utente attivo.
     - Scorciatoia globale di blocco rapido del computer (`Win + L`).
     - Gestione reale dei privilegi utente (Administrator vs Standard User).
  4. **Servizio Registro Eventi di Sistema (`EVENTLOG_SVC`)**:
     - Archiviazione strutturata di log ed errori in `C:\SYSTEM\LOGS\SYSTEM.LOG`.
     - Categorie di log: Informazione, Avviso, Errore Critico, Eventi di Sicurezza.
  5. **Servizio Sincronizzazione Oraria NTP (`NTP_SVC`)**:
     - Client NTP in background che interroga periodicamente i server orari di rete e sincronizza l'orologio RTC di NewOS.

---

### FASE 10: Suite di Tool e Utilità Primarie di Sistema
* **Obiettivo**: Fornire agli utenti una dotazione completa di programmi essenziali per la gestione, diagnostica e produttività.
* **Applicazioni da implementare**:
  1. **Gestione Attività / Task Manager (`TASKMGR.EXE`)**:
     - Grafico a scorrimento in tempo reale dell'utilizzo CPU (percentuale e cronologia).
     - Barra di utilizzo memoria RAM (RAM Totale, Usata per Cache, Libera).
     - Elenco processi e finestre attive con ID processo (PID) e pulsante *"Termina Processo"* (Kill).
  2. **Gestione Dischi e Partizionatore (`DISKMGMT.EXE`)**:
     - Visualizzatore grafico della mappa dei dischi e delle partizioni collegate.
     - Visualizzazione spazio allocato/libero, verifica integrità file system exFAT e formattazione volumi.
  3. **Visualizzatore Immagini Avanzato (`IMGVIEW.EXE`)**:
     - Apertura a schermo intero o in finestra di file `.BMP`, `.JPG` e `.PNG`.
     - Controlli per Zoom In, Zoom Out, rotazione 90°, adattamento automatico alla finestra e pulsante rapido *"Imposta come sfondo del desktop"*.
  4. **Lettore Multimediale Audio/Video (`MEDIAPLR.EXE`)**:
     - Player dedicato per file audio `.WAV` con barra di avanzamento del brano (seek bar interattiva), playlist, controlli Play/Pause/Stop e visualizzazione forma d'onda.
  5. **Visualizzatore Eventi (`EVENTVWR.EXE`)**:
     - Interfaccia grafica stile Windows Event Viewer per consultare lo storico dei log registrati da `EVENTLOG_SVC`.
  6. **Gestore Archivi Compressi (`ZIPMGR.EXE`)**:
     - Utilità per comprimere ed estrarre pacchetti `.ZIP`, utile per installare software di terze parti e backup.
  7. **Funzione Ripristino nel Cestino**:
     - Salvataggio del percorso originale di ogni file eliminato e pulsante *"Ripristina file"* per rimetterlo nella sua cartella originaria.

---

### FASE 11: Pilastri Fondamentali del File System e del Kernel
* **Obiettivo**: Completare le fondamenta del kernel necessarie a supportare tutte le modifiche a runtime senza dipendere da script esterni.
* **Funzionalità da implementare**:
  1. **Supporto Scrittura Completo in `exfat.c` (VFS Write Engine)**:
     - `exfat_write_file()`: scrittura dati e allocazione di nuovi cluster nell'Allocation Bitmap e nella FAT chain.
     - `exfat_rename_entry()`: ridenominazione sicura delle directory entry su disco.
     - `exfat_delete_entry()`: marcatura entry come cancellata e deallocazione cluster corrispondenti.
  2. **Tabella Export API del Kernel & Dynamic Module Loader**:
     - Struttura `newos_api_table_t` esposta in un indirizzo fisso o tramite syscall.
     - Permette a `.SYS` ed `.EXE` di essere compilati separatamente e chiamare nativamente funzioni del kernel (GUI, mem, disk, sound, net).
  3. **Motore di Sicurezza e Backup Registro (`REGISTRY.BAK`)**:
     - Copia automatica preventiva all'avvio di `C:\SYSTEM\REGISTRY.BAK`.
     - Opzione al boot per avviare con l'*Ultima Configurazione Funzionante*.
  4. **Pipeline di Build Modulare (`tools/build.js`)**:
     - Flag dedicati: `--kernel`, `--drivers`, `--apps`, `--disk` per velocizzare lo sviluppo e la compilazione incrementale.

---

### FASE 12: Interfaccia Utente & Visual Polish (Modern UX)
* **Obiettivo**: Rendere l'esperienza d'uso fluida, reattiva e paragonabile ai sistemi operativi commerciali di ultima generazione.
* **Funzionalità da implementare**:
  1. **Snap Assist & Window Docking (Aero Snap)**:
     - Riconoscimento del trascinamento finestra sui bordi dello schermo:
       - Bordo sinistro/destro: aggancio e ridimensionamento automatico a 50% larghezza schermo.
       - Bordo superiore: massimizzazione immediata a schermo intero.
  2. **Alt + Tab con Anteprime Visive in Tempo Reale**:
     - Box modale Liquid Glass al centro dello schermo durante la pressione di `Alt + Tab`.
     - Rendering delle miniature in tempo reale (mini-screenshot dei back-buffer) di tutte le finestre aperte con selezione rapida da frecce direzionali o mouse.
  3. **Centro Notifiche a Scomparsa (Toast Notifications)**:
     - Piccoli popup con angoli arrotondati e sfumatura frosted sopra la taskbar.
     - Notifiche animate ad auto-dissolvenza per: inserimento periferiche USB, link di rete attivo, livello batteria, download/copia file terminata.
  4. **Desktop Virtuali Multipli (Workspaces)**:
     - Gestione di spazi di lavoro virtuali multipli (es. Workspace 1: Ufficio/File, Workspace 2: Sviluppo, Workspace 3: Multimedia).
     - Commutazione istantanea con scorciatoia `Win + Ctrl + Frecce` o icona dedicata nella taskbar.
  5. **Filtro Luce Blu / Modalità Notturna (Night Shift)**:
     - Regolazione software delle curve colore del Linear Framebuffer per attenuare la luce blu e ridurre l'affaticamento visivo serale.

---

### FASE 13: Produttività e Ricerca Rapida
* **Obiettivo**: Velocizzare le operazioni quotidiane dell'utente tramite strumenti ad accesso istantaneo.
* **Strumenti da implementare**:
  1. **Ricerca Universale Globale (Spotlight NewOS - `Win + Spazio`)**:
     - Barra di ricerca fluttuante e minimale al centro dello schermo.
     - Ricerca istantanea su file system, cartelle, applicazioni, strumenti del Pannello di Controllo e chiavi di registro con esecuzione immediata al tasto `Invio`.
  2. **Storico Appunti Avanzato (Clipboard History - `Win + V`)**:
     - Memorizzazione degli ultimi 10 elementi copiati negli appunti (testo, snippet di codice e percorsi file).
     - Menu a comparsa rapido per selezionare quale elemento incollare.
  3. **Strumento Cattura Schermo (`SNIP.EXE`)**:
     - Attivabile da tasto `Stamp` (Print Screen) o da menu Start.
     - Selezione di un rettangolo col mouse o cattura dell'intero schermo con salvataggio immediato sul desktop in `.PNG` o `.BMP`.

---

### FASE 14: Strumenti Software & Svago Integrati
* **Obiettivo**: Dotare NewOS di una serie di programmi applicativi iconici e ricchi di funzionalità per l'uso quotidiano.
* **Applicazioni da implementare**:
  1. **Terminale Avanzato con Colori ANSI & Scripting (`NEWCLI.EXE`)**:
     - Supporto completo a colori e stili ANSI (prompt a colori, testi evidenziati).
     - Interprete di script `.BAT` e comandi built-in: `ls`, `dir`, `cat`, `type`, `copy`, `move`, `del`, `ping`, `ifconfig`, `ps`, `kill`, `clear`.
  2. **Mini Browser Web Grafico / Client HTTP (`WEBBROWSER.EXE`)**:
     - Applicazione basata sullo stack `.SYS.NET` per interrogare server web locali o remoti.
     - Rendering di pagine HTML 1.0/2.0 con formattazione testo, immagini e collegamenti ipertestuali.
  3. **Mini-Giochi Retrò Iconici (`MINESWEEPER.EXE` e `SOLITAIRE.EXE`)**:
     - Il celebre Campo Minato e il Solitario con carte: ideali sia per lo svago sia per collaudare timer ad alta precisione, animazioni e gestione eventi mouse.

---

### FASE 15: Resilienza, Manutenzione e Sicurezza
* **Obiettivo**: Proteggere l'integrità del sistema operativo contro crash, errori di configurazione o driver difettosi.
* **Componenti da implementare**:
  1. **Punti di Ripristino del Sistema (System Restore Points)**:
     - Meccanismo di snapshot leggero per salvare lo stato di `C:\SYSTEM\DRIVERS\` e di `REGISTRY.DAT` prima di ogni aggiornamento o installazione.
     - Ripristino in un clic per annullare modifiche problematiche.
  2. **Schermata Diagnostica di Arresto Elegante ("Green Screen of Life")**:
     - Gestore delle eccezioni CPU non recuperabili (Page Fault, General Protection Fault) con schermata chiara contenente:
       - Dump dei registri a 64-bit (`RAX`, `RBX`, `RIP`, `RSP`, `CR3`).
       - Nome del modulo o driver `.SYS` responsabile del crash.
       - Opzione per riavviare in modalità provvisoria o salvare il report in `C:\SYSTEM\LOGS\CRASH.LOG`.
  3. **Schemi Sonori a Tema (Sound Themes)**:
     - Set audio personalizzabili dal Pannello di Controllo: *Classic 2000*, *Modern Glass* e *Minimal Silent*.

---

### FASE 16: Ambiente di Sviluppo Nativo & Self-Hosting
* **Obiettivo**: Rendere NewOS un sistema operativo indipendente e autosufficiente (Self-Hosting), capace di sviluppare e compilare i propri programmi al proprio interno, alloggiando l'ambiente in `C:\SYSTEM\DEV\TCC\`.
* **Componenti da implementare**:
  1. **Compilatore C Nativo Integrato (`C:\SYSTEM\DEV\TCC\TCC.EXE` 64-bit)**:
     - Porting del celebre Tiny C Compiler per target x86_64 NewOS freestanding.
     - Compilazione istantanea in memoria o su disco di file sorgente `.C` direttamente in binari eseguibili `.EXE`.
  2. **Cartella di Configurazione (`C:\SYSTEM\DEV\TCC\CONFIG\`) & Directory Temporanee (`TEMP\`, `TMP\`)**:
     - File `TCC.CFG` con parametri del compilatore:
       ```ini
       [Compiler]
       Target=x86_64-newos
       Optimization=O2
       Warnings=All
       SysInclude=C:\SYSTEM\DEV\TCC\INCLUDE\
       SysLib=C:\SYSTEM\DEV\TCC\LIB\
       TempDir=C:\SYSTEM\DEV\TCC\TEMP\
       TmpDir=C:\SYSTEM\DEV\TCC\TMP\
       DefaultOutput=C:\APPS\
       ```
     - Cartelle `C:\SYSTEM\DEV\TCC\TEMP\` e `C:\SYSTEM\DEV\TCC\TMP\` per l'elaborazione dei file intermedi di compilazione (.o, .s, sorgenti pre-processati).
  3. **NewOS Software Development Kit (SDK)**:
     - File header principale `C:\SYSTEM\DEV\TCC\INCLUDE\newos.h` contenente tutte le definizioni di tipo, costanti e prototipi delle API di sistema (finestre, grafica, file system, audio, rete).
     - Librerie statiche in `C:\SYSTEM\DEV\TCC\LIB\libnewos.a`.
     - Possibilità per l'utente di scrivere un'applicazione nel Blocco Note, compilarla con `tcc app.c -o app.exe` ed eseguirla immediatamente su NewOS senza alcun PC o cross-compilatore esterno!

---

### FASE 17: Motore Grafico 3D & Retrogaming Iconico
* **Obiettivo**: Introdurre capacità grafiche tridimensionali software e supporto al retrogaming per collaudare al massimo le prestazioni a 64-bit.
* **Componenti da implementare**:
  1. **Mini-Motore 3D Software Vettoriale (TinyGL 64-bit)**:
     - Rasterizzatore 3D software in spazio colore 32-bit: calcolo prospettico, z-buffering leggero, flat shading e texture mapping a 60 FPS su framebuffer VESA.
  2. **Screensaver 3D Iconici**:
     - Salvaschermo con tubi poligonali rotanti (*3D Pipes*) e cubo tridimensionale NewOS con riflessi in tempo reale.
  3. **Porting Nativo di DOOM 1993 a 64-bit (`DOOM.EXE`)**:
     - Porting del motore originale in C (id Software) compilato direttamente per NewOS, con supporto ad audio AC'97, tastiera e rendering a schermo.

---

### FASE 18: Connettività LAN & Condivisione File Bidirezionale (`NETSHARE`)
* **Obiettivo**: Permettere a NewOS di comunicare e scambiare file via rete in tempo reale con altri computer senza chiavette o riavvii.
* **Componenti da implementare**:
  1. **Server di Trasferimento File LAN Drag & Drop (`NETSHARE`)**:
     - Mini-server HTTP locale in background (porta 8080).
     - Aprendo il browser dal PC reale su `http://[IP-NewOS]:8080`, si accede a un'interfaccia web moderna per trascinare file direttamente dentro `C:\USER\MORGAN\DESKTOP\` di NewOS (e viceversa scaricare file da NewOS).
  2. **Audio Tracker & Sintetizzatore Musicale (`SOUNDSTUDIO.EXE`)**:
     - Tastiera di pianoforte a schermo e sequencer a 8 tracce per comporre melodie polifoniche con export diretto in formato `.WAV` impostabile come suono di avvio.
  3. **Pacchetti Temi Condivisibili a 1 Clic (`.THEME` Packager)**:
     - Formato compresso che raggruppa tavolozza colori, sfondi a risoluzione piena, icone e schemi sonori applicabili con un doppio clic.
  4. **Modalità Kiosk / Fullscreen Console (`KIOSK=1`)**:
     - Avvio diretto in un'applicazione a schermo intero senza barra o desktop per console, simulatori o terminali dedicati.

---

### FASE 19: Strumenti di Diagnostica Hardware & Benchmark (`NEWCLI SysTools`)
* **Obiettivo**: Dotare il terminale di NewOS di strumenti di analisi hardware e calcolo prestazioni professionali.
* **Comandi da implementare**:
  1. `pci`: scansione completa dell'albero bus PCI con decodifica di Vendor ID, Device ID, classe e indirizzi I/O/memoria.
  2. `cpuinfo`: estrazione informazioni `CPUID` (marca processore, frequenza stimata in MHz, istruzioni SSE/AVX e numero di core).
  3. `memmap`: mappa grafica a blocchi della memoria fisica RAM (spazio kernel, heap, buffer video e memoria libera).
  4. `speedtest`: benchmark rapido di velocità di lettura/scrittura su disco ATA/AHCI e framerate grafico del display.

---

### FASE 20: Suite di Gestione Sistema & Amministrazione Avanzata (Administrator Suite)
* **Obiettivo**: Fornire strumenti di controllo centralizzato, sicurezza e monitoraggio di livello workstation/enterprise per gli amministratori di NewOS.
* **Strumenti da implementare**:
  1. **Gestione Utenti, Gruppi e Permessi (`USERMGR.EXE`)**:
     - Gestione account multipli (`Administrator`, `Morgan`, `Guest`, `Operatore`).
     - Gruppi di sicurezza con privilegi scalari: `Administrators` (accesso totale), `Power Users` (gestione app e rete), `Standard Users` (file personali), `Guests` (modalità temporanea con reset al logout).
     - Criteri di scadenza password, complessità obbligatoria e blocco dopo 3 tentativi falliti.
  2. **User Account Control (UAC) & Elevazione Privilegi (`sudo`)**:
     - Schermata di elevazione protetta (Secure Prompt con oscuramento desktop) per autorizzare modifiche a `C:\SYSTEM\`, caricamento driver o alterazione del registro.
     - Comando `sudo` integrato nel terminale per eseguire comandi con credenziali amministrative.
  3. **Criteri di Gruppo e Politiche di Sicurezza (`GPEDIT.EXE`)**:
     - Editor per imporre restrizioni di sistema: blocco di `REGEDIT.EXE`, blocco di file `.EXE` non firmati o da USB, protezione scrittura su chiavette USB esterne (DLP) e blocco personalizzazioni desktop.
  4. **Console Gestione Servizi di Sistema (`SERVICES.EXE`)**:
     - Visualizzatore stile `services.msc` con stato in tempo reale di tutti i daemon del kernel (Audio, Net, Logger, Mount, NTP, Power).
     - Controllo dello stato del servizio con pulsanti: *Avvia*, *Arresta*, *Riavvia Servizio* a caldo.
  5. **Console Unificata "Computer Management" (`COMPMGMT.EXE`)**:
     - Console integrata a tre riquadri stile MMC che raggruppa in una sola finestra: *Visualizzatore Eventi*, *Cartelle Condivise*, *Utenti/Gruppi*, *Gestione Dispositivi*, *Gestione Disco* e *Servizi*.
  6. **Monitoraggio Risorse e Prestazioni Hardware (`PERFMON.EXE`)**:
     - Grafici cronologici in tempo reale per: frequenza CPU e carico per core, velocità di I/O disco (MB/s in lettura/scrittura e latenza comandi), throughput rete RX/TX e mappa di allocazione dell'Heap di memoria.
  7. **Controllo Integrità e Riparazione Disco (`CHKDSK.EXE`)**:
     - Scansione incrociata di coerenza tra FAT chain e Allocation Bitmap di exFAT.
     - Correzione automatica di cluster orfani e recupero dati in file `.CHK`.
  8. **Process Explorer & Monitoraggio Connessioni Socket (`PROCMON.EXE`)**:
     - Visualizzazione gerarchica Process Tree (processo genitore -> processi figli), librerie e file `.SYS` agganciati a ciascun processo, e tabella socket TCP/UDP attivi.

---

### FASE 21: Accessibilità, Ergonomia & Avvio Fulmineo (Fast Boot & Accessibility)
* **Obiettivo**: Fornire a NewOS velocità di ripristino istantanea e strumenti inclusivi per ogni tipologia di utente.
* **Componenti da implementare**:
  1. **Ibernazione & Avvio in 1 Secondo (`HIBERNATE.SYS`)**:
     - Scaricamento istantaneo dell'intera RAM (64 MB) in `C:\SYSTEM\HIBERFIL.SYS` allo spegnimento.
     - Al boot successivo, ricarica ultra-rapida dello stato per ritrovare tutte le finestre e i file esattamente come lasciati in meno di 1 secondo.
  2. **Centro Accessibilità (`ACCESSIBILITY.EXE`)**:
     - Lente d'ingrandimento dinamica dello schermo (Screen Magnifier 2x/4x/8x attorno al cursore).
     - Schemi ad Alto Contrasto (Nero/Bianco, Bianco/Nero, Ambra Retrò, Fosfori Verdi).
     - Tasti Permanenti (Sticky Keys per scorciatoie con un solo dito) e avvisi visivi.
  3. **Disco Virtuale Ramdisk Ultra-Rapido (`RAMDISK.SYS` / Unità `Z:`)**:
     - Creazione di un disco in memoria RAM a 2+ GB/s per compilazioni C istantanee a zero latenza e cache del browser.

---

### FASE 22: Distribuzione Software, Periferiche Speciali & Produttività
* **Obiettivo**: Dotare NewOS di un ecosistema di distribuzione applicazioni, supporto a controller di gioco e visualizzazione documenti.
* **Componenti da implementare**:
  1. **Gestore Pacchetti & App Store (`NEWPKG.EXE` / `APPSTORE.EXE`)**:
     - Formato pacchetto `.NPK` (NewOS Package) firmato digitalmente, contenente binari `.EXE`/`.SYS`, descrittore `.INI`, icone e collegamenti.
     - Installazione guidata a un clic o da terminale (`pkg install`, `pkg remove`).
  2. **Driver Joystick & Gamepad USB / GamePort (`JOYSTICK.SYS`)**:
     - Supporto per joypad USB standard per giocare comodamente al porting di DOOM o agli emulatori.
  3. **Visualizzatore Documenti PDF & Manuali (`PDFVIEW.EXE`)**:
     - Visualizzatore per consultare guide e il manuale nativo `C:\DOCS\MANUALE_NEWOS.PDF` con tutte le scorciatoie e le API.
  4. **Desktop Gadgets & Sticky Notes**:
     - Note adesive colorate trascinabili sul desktop e widget fluttuanti con orologio analogico stile Liquid Glass.

---

## 3. Ordine di Esecuzione Consigliato (Roadmap Master a 23 Passi)

| Passo | Macro-Fase | Deliverable Principale |
| :---: | :--- | :--- |
| **Passo 1** | **Rebranding NewOS V1** | Rinomina globale, copyright `@ Newspoint Software - NewSoft - NewOS V1`, aggiornamento banner, About, MBR e script. |
| **Passo 2** | **Driver Architecture (.SYS + .INI)** | Creazione cartella `C:\SYSTEM\DRIVERS\`, implementazione `driver_mgr.c`, generazione di tutti i file `.SYS` e `.INI`. |
| **Passo 3** | **exFAT VFS Write & File Ops** | Scrittura cluster exFAT nativa, menu con tasto destro su file e icone in base all'estensione, dialog Rinomina, Elimina, Copia e finestra Proprietà File. |
| **Passo 4** | **TOOL.EXE / REGEDIT.EXE** | Registro di sistema su disco (`REGISTRY.DAT`), albero navigabile, alert di sicurezza con finestra di conferma pre-modifica e backup `REGISTRY.BAK`. |
| **Passo 5** | **Driver Video Avanzato & Dynamic Resolution** | Supporto cambio risoluzione dinamico tramite registri BGA/VBE da 800x600 a 1080p Full HD in `VGA.SYS` e cursore hardware. |
| **Passo 6** | **Stack di Rete (.SYS.NET)** | Controller Intel E1000/RTL8139 + moduli `ETH.SYS.NET`, `IP.SYS.NET`, `TCP.SYS.NET`, `UDP.SYS.NET`, `DHCP.SYS.NET`. |
| **Passo 7** | **Pannello di Controllo Suite (8 Tool)** | Implementazione delle 8 nuove applet (Variabili Ambiente, Funzioni, Rete, Data/Ora, Audio, Driver, Certificati, Install/Uninstall). |
| **Passo 8** | **Driver Hardware Primari (AHCI, USB, HDA)** | `AHCI.SYS` con DMA per dischi ultra-rapidi, `USB_XHCI.SYS` per mouse/tastiere USB su PC fisici e `HDAUDIO.SYS`. |
| **Passo 9** | **Servizi di Sistema (Scheduler, Mount, Logon)** | Multitasking preemptive, gestore montaggio volumi e lettere (`D:`, `E:`), schermata di login e log di sistema. |
| **Passo 10** | **Tool Essenziali (TASKMGR, IMGVIEW, DISKMGMT)** | Task Manager con grafici CPU/RAM, visualizzatore immagini con zoom, lettore audio avanzato e partizionatore. |
| **Passo 11** | **Interfaccia Utente & Visual Polish** | Aero Snap 50%/100%, Alt+Tab con miniature in tempo reale, toast notifications, desktop virtuali e night shift. |
| **Passo 12** | **Produttività & Ricerca Rapida** | Spotlight globale `Win+Spazio`, storico appunti `Win+V`, strumento cattura schermo `SNIP.EXE`. |
| **Passo 13** | **Software & Svago Integrati** | Terminale avanzato `NEWCLI.EXE` con colori e script batch, `WEBBROWSER.EXE` e mini-giochi `MINESWEEPER.EXE` e `SOLITAIRE.EXE`. |
| **Passo 14** | **Resilienza & Manutenzione** | Punti di ripristino snapshot del sistema, Green Screen of Life crash handler elegante e temi sonori personalizzati. |
| **Passo 15** | **Ambiente di Sviluppo Self-Hosting** | Porting compilatore C nativo `TCC.EXE`, NewOS SDK `newos.h` per creare ed eseguire programmi direttamente dentro NewOS. |
| **Passo 16** | **Grafica 3D & Retrogaming** | Motore software TinyGL 3D, salvaschermo poligonali e porting a 64-bit di `DOOM.EXE`. |
| **Passo 17** | **Condivisione File LAN & Temi** | Mini-server `NETSHARE` web drag & drop, sintetizzatore `SOUNDSTUDIO.EXE` e pacchetti `.THEME`. |
| **Passo 18** | **Diagnostica Hardware Avanzata** | Utility terminale `pci`, `cpuinfo`, `memmap`, `speedtest` benchmark disco/RAM. |
| **Passo 19** | **Suite Amministrazione & System Management** | `USERMGR`, `UAC/sudo`, `GPEDIT`, `SERVICES`, `COMPMGMT`, `PERFMON`, `CHKDSK`, `PROCMON`. |
| **Passo 20** | **Accessibilità, Ergonomia & Ibernazione** | `HIBERNATE.SYS` avvio in <1s, `ACCESSIBILITY.EXE` magnifier e alto contrasto, `RAMDISK.SYS` unità `Z:`. |
| **Passo 21** | **Distribuzione Software & Periferiche** | `NEWPKG.EXE` App Store, `JOYSTICK.SYS`, `PDFVIEW.EXE`, Desktop Gadgets & Sticky Notes. |
| **Passo 22** | **Ottimizzazione Prestazioni & Pulizia** | Calibrazione consumi energetici, deframmentazione heap e benchmark finale. |
| **Passo 23** | **Collaudo Globale & Validazione QEMU** | Build completa a moduli, test interattivo di tutte le nuove componenti, benchmark e cattura screenshot finali. |
