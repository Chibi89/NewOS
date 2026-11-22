# Roadmap Migliorie e Nuove Funzionalità — NewOS V1 (64-bit)

Elenco completo delle migliorie architetturali, grafiche, di sistema e hardware integrabili in **NewOS V1**.

---

## 1. Esplora Risorse ([`explorer/`](explorer/))
- [x] **Sottomenu `Mostra` in `Visualizza`**: attivare `show_extensions` (mostra/nascondi estensioni file), `show_hidden` (mostra/nascondi file nascosti e di sistema), `Riquadro di anteprima` (preview testuale/immagine a destra) e `Caselle di controllo elementi`.
- [x] **Ordinamento da intestazione colonna**: nella vista `Dettagli`, click su `Nome`, `Tipo`, `Dimensione` o `Perm` per ordinare ASC/DESC con freccia indicatrice.
- [x] **Selezione multipla**: checkbox per elemento + `CTRL+Click`, con conteggio selezione e dimensione totale nella barra di stato.
- [ ] **Drag & Drop reale**: trascinare file/cartelle su un'altra cartella, su un disco nella sidebar o sul Desktop per spostare/copiare.
- [x] **Ricerca avanzata & Filtri**: filtro rapido mentre si digita, ricerca ricorsiva nelle sottocartelle e filtro per estensione/dimensione.
- [x] **Tab / Schede multiple**: barra tab in alto stile Windows 11 Explorer per aprire più cartelle nella stessa finestra (`Ctrl+T`, `Ctrl+W`).
- [x] **Breadcrumb cliccabile**: nella barra degli indirizzi, click sui singoli segmenti del percorso (`C:\ > NewOS > ...`) per navigare direttamente al livello cliccato.

---

## 2. Window Manager, Desktop, Menu Start & Taskbar ([`kernel/gui/wm.c`](kernel/gui/wm.c))
- [x] **Nuovo Menu Start Unificato Moderno (stile Windows 11 24H2/25H2, Out-of-Kernel [`STARTMENU.DLL`](SystemLibrary/startmenu_dll.c))**:
  - **Configurazione dal CPL `Personalizzazione` (`DESK.CPL` / `SETTINGS.DLL`)**: selettore dedicato per scegliere il tipo di Menu Start (*Classico Windows 2000*, *Moderno Compatto*, *Unificato stile Windows 11*) e l'allineamento (*Sinistra* / *Centrato*), indipendente dal tema grafico attivo e persistito nel Registro di Sistema.
  - **Barra di ricerca unificata in alto**: ricerca live su app installate, impostazioni (`.CPL`), file e documenti (`C:\User\`).
  - **Sezione `Preferiti`**: griglia personalizzabile di app pinnate (Impostazioni, Esplora file, Web Browser, Blocco Note, Calcolatrice, Media Player, ecc.) con possibilità di aggiungere/rimuovere da menu contestuale.
  - **Sezione `Tutto` scrollabile (a rotella / scrollbar)** con selettore **`Visualizzazione: Griglia | Elenco | Categoria`**: raggruppamento alfabetico (`#`, `A`, `B`, `C`...), cartelle espandibili e badge **`Nuova`** per le applicazioni appena installate tramite `PKGSVC.DLL`.
  - **Barra inferiore (Footer)**: avatar + nome utente corrente (click -> disconnessione / cambio utente) + pulsanti rapidi configurabili (Documenti, Download, Musica, Immagini, Video, Esplora Risorse, Impostazioni, Alimentazione -> `LOGOUT.MOD`).
- [x] **Window Snap & Ridimensionamento multi-bordo**: affiancamento 50%/50% trascinando una finestra sul bordo sinistro/destro, massimizzazione sul bordo superiore, e resize fluido da tutti e 4 i lati + 4 angoli.
- [x] **Alt+Tab grafico**: overlay traslucido al centro dello schermo con icone e titoli delle finestre aperte durante la pressione di `Alt`.
- [x] **Menu contestuale sui pulsanti della Taskbar**: click destro sul pulsante di un'app nella taskbar (`Ripristina`, `Minimizza`, `Massimizza`, `Affianca a sinistra`, `Chiudi`), più pulsante **"Mostra Desktop"** all'estrema destra accanto all'orologio.
- [x] **Anteprima finestre / Tooltip taskbar**: tooltip o miniatura al passaggio del mouse sui pulsanti della taskbar.
- [x] **Notifiche Toast & Centro Notifiche**: popup temporanei nell'angolo in basso a destra (chiavetta USB connessa, flush completato, policy di sicurezza aggiornata).
- [x] **Scorciatoie da tastiera globali**: `Win+E` (Esplora Risorse), `Win+R` (Esegui tramite `LAUNCHER.DLL`), `Win+D` (Mostra Desktop), `Win+L` (Blocca sessione -> `LOGIN.MOD`), `Ctrl+Shift+Esc` (Task Manager).

---

## 3. Libreria Controlli, Gestione Font & Coerenza UI ([`SystemLibrary/comctl.h`](SystemLibrary/comctl.h), `THEMEMGR.DLL`, `FONTMGR.DLL`)
- [x] **Gestione Font & Applet `FONTSCPL.CPL` (TrueType System Font)**:
  - Applet `.CPL` out-of-kernel [`FONTSCPL.DLL`](SystemLibrary/fontscpl.c) nel Pannello di Controllo (`Caratteri`) con scansione `C:\NewOS\Resources\Fonts\`, anteprima TrueType live a 14px/20px/28px tramite `FONTMGR.DLL` (`FONTMGR_CMD_SET_TTF`), selezione modalità rendering (`Bitmap 8x16`, `TrueType AA`, `ClearType`), dimensione base e salvataggio nel Registro.
- [ ] **Migrazione completa delle app a `COMCTL.DLL`**: convertire `Task Manager`, `Regedit`, `Notepad`, `Calcolatrice` e le applet `.CPL` per usare `comctl_listview`, `comctl_treeview`, `comctl_tab`, `comctl_property_grid`, `comctl_scrollbar` e `comctl_textbox`, eliminando codice GUI duplicato.
- [ ] **Controlli `COMCTL` interattivi completi**: supporto tastiera completo (`Tab` focus traversal tra controlli, frecce su `ListView`/`TreeView`/`ComboBox`, `PgUp`/`PgDn`, selezione testo con mouse/Shift in `TextBox`, copia/incolla `Ctrl+C/V` nei campi input).
- [ ] **Scaling DPI dinamico**: adattamento automatico di font, icone e padding alle risoluzioni `1280x720`, `1600x900`, `1920x1080`.
- [ ] **Animazioni leggere opzionali**: transizioni apertura/chiusura menu e minimizzazione finestre (disattivabili in modalità Classic 2000).

---

## 4. File System, Storage & Cestino ([`kernel/fs/vfs.c`](kernel/fs/vfs.c), `RECYCLE.DLL`)
- [x] **Cestino reale integrato**: tasto `Canc` / **Elimina** in Explorer sposta in `C:\Recycle.Bin\` con file metadati (percorso originale, data, dimensione) per il **Ripristino** da `RECYCLE.DLL`; `Shift+Canc` per eliminazione definitiva.
- [ ] **Scrittura completa su `EXT4` e `FAT32`**: consolidare il supporto R/W sicuro su `FAT32` (chiavette USB e partizione ESP) ed `EXT4` in `FS_EXT.DLL`.
- [ ] **Write-back cache & Flush periodico sicuro**: timer di flush automatico in background dei settori dirty e indicatore di **"Rimozione sicura dell'hardware"** USB nella tray bar.
- [ ] **Compressione / Archivi `.ZIP` / `.TAR`**: supporto in `FS_EXT.DLL` ed Explorer per aprire/estrarre archivi `.ZIP` come cartelle virtuali.
- [x] **Collegamenti (`.LNK`)**: file shortcut su Desktop ed Explorer gestiti da `LAUNCHER.DLL` per puntare a programmi o cartelle. (`Target=`/`Args=` in `LAUNCHER.DLL`, riconoscimento Explorer e comando `mklnk` in `CMD`).

---

## 5. Sicurezza, Registro, Multi-Utente & Gestione Pacchetti ([`SystemLibrary/seccpl.c`](SystemLibrary/seccpl.c), [`kernel/fs/registry.c`](kernel/fs/registry.c))
- [x] **Servizio Standardizzato Obbligatorio di Installazione Software Terzi (`PKGSVC.DLL`)**:
  - Implementato in [`SystemLibrary/pkgsvc.c`](SystemLibrary/pkgsvc.c) con isolamento forzato in `C:\ProgramFile\<App>\` (blocco ACL di percorsi in `C:\NewOS\`), creazione manifest `APP.INI`, registrazione in `HKLM\SOFTWARE\NewOS\InstalledApps` e disinstallazione pulita.
- [x] **Persistenza preferenze utente**: salvare nel Registro (a sessione autenticata) vista Explorer preferita, `show_extensions`, `show_hidden`, posizione delle icone sul desktop, tema e risoluzione scelta.
- [ ] **Gestione multi-account reale (`USERACCT.CPL`)**: creazione di più utenti locali in `C:\User\<Nome>\` con password hashata, distinzione Standard/Amministratore e schermata di selezione utente in `LOGIN.MOD`.
- [ ] **Dialogo UAC / Elevazione inline**: prompt di elevazione password amministratore quando un utente Standard tenta un'operazione protetta (`C:\NewOS\`, `Regedit` su `HKLM`, cambio policy).
- [x] **Audit Log di Sicurezza**: registro eventi critici (login riusciti/falliti, modifiche policy LSA, tentativi ACL negati) consultabile in `SECCPL.CPL`.

---

## 6. Kernel Core, Processi Ring 3 & Memoria ([`kernel/arch/x86_64/`](kernel/arch/x86_64/))
- [ ] **Eseguibili User-Mode Ring 3 reali (`.EXE` / ELF64)**: esecuzione di processi utente isolati nel proprio spazio `PML4` con protezione hardware contro crash applicativi (dialogo di terminazione processo al posto del BSOD di sistema).
- [ ] **Preemptive Multitasking & SMP Bilanciato**: distribuzione dei thread sui core secondari (AP) tramite `smp.c` con spinlock/mutex granulari su VFS, Heap e WM.
- [ ] **Slab / Buddy Allocator o Free-List Coalescing avanzato**: ottimizzare `kmalloc`/`kfree` con bucket per piccole allocazioni frequenti e statistiche dettagliate per `Task Manager`.
- [ ] **Page Fault Demand Paging**: lazy allocation / mappatura su richiesta per file di grandi dimensioni e moduli browser pesanti (`WEBBROWSER.DLL` / `JSENGINE.DLL`).

---

## 7. Hardware Reale Bare-Metal & Driver ([`kernel/drivers/`](kernel/drivers/))
- [ ] **EDID & Impostazione Risoluzione Reale**: lettura blocchi EDID da VBE/GOP per proporre in `DESK.CPL` le risoluzioni effettivamente supportate dal monitor fisico.
- [ ] **MSI / MSI-X Interrupts**: supporto Message Signaled Interrupts su PCIe per **NVMe**, **xHCI**, **Intel HDA** ed **E1000/RTL8169**.
- [ ] **Gestione Risparmio Energetico ACPI completa**: lettura stato batteria (su laptop), stati `S3` (Sospensione RAM) e `S5` (Spegnimento) affidabili su silicio fisico.
- [ ] **Touchpad PS/2 / I2C HID & Layout Tastiera Multipli**: scorrimento a due dita, layout dinamici in `KEYBCPL.CPL` (IT/US/UK/DE/FR) e tasti multimediali.
- [x] **Sottosistema Porte Parallele (`LPT`), Seriali (`COM`) & Porte Virtuali (Out-of-Kernel [`PORTS.DLL`](SystemLibrary/ports_dll.c))**:
  - Rilevamento I/O bare-metal di **Porte Seriali `COM1`–`COM4`** (UART 16550A/16450 con FIFO 16B e test Loopback HW), **Porte Parallele Legacy `LPT1`–`LPT2`** (IEEE 1284 SPP/EPP/ECP) e **Porte Virtuali `VCOM1`/`VCOM2`/`VLPT1`**, con configurazione Baud Rate.
- [x] **Diagnostica / Benchmark Bare-Metal**: tool in `SYSDM.CPL` o `DEVMGMT.CPL` per testare velocità reale lettura/scrittura dischi (NVMe/AHCI), test RAM, stato core SMP e IRQ/PCI.

---

## 8. Rete & Web Browser ([`WebBrowser/`](WebBrowser/), `NETSTACK.DLL`)
- [ ] **Cache HTTP su disco & Cookie/Cronologia**: salvataggio persistente di cronologia, segnalibri e cache in `C:\User\<Utente>\AppData\`.
- [ ] **Browser Multi-Tab & Download Manager**: schede multiple in `WEBBROWSER.DLL` e download file in `C:\User\<Utente>\Downloads\` con progress bar `COMCTL`.
- [ ] **Client DHCPv6 / DNS Caching & Strumenti di Rete**: cache DNS locale con TTL, `traceroute`, `arp -a` e diagnostica schede in `NETCPL.CPL`.
- [x] **Client HTTP / FTP CLI (`curl` / `wget`)** in `CMD` per scaricare file e risorse da terminale. (Aggiunti anche `arp -a` e `nslookup` in `CMD` con esportazione DNS/TCP in `kernel_ext_api.h`).

---

## 9. Applicazioni di Sistema & Multimedia
- [x] **Blocco Note (`notepad/`)**: supporto file grandi (>64 KB), `Trova / Sostituisci` (`Ctrl+F`), `Annulla` (`Ctrl+Z`), numeri di riga opzionali e apertura multipla.
- [x] **Visualizzatore Immagini dedicato (`IMGVIEW`)**: zoom, rotazione e presentazione per `.BMP`, `.PNG`, `.JPG`, `.WEBP` tramite `IMGCODEC.DLL`.
- [ ] **NewOS Media Player (`player/`)**: completare la decodifica e riproduzione reale di `.MP3` / `.FLAC` (codec `.CDC`) e seek bar interattiva.
- [x] **Task Manager (`taskmgr/`)**: tab **Prestazioni** (grafici storici CPU/RAM/Disk I/O/Network I/O) e tab **Moduli DLL** caricati con consumo memoria e CRC32.
- [x] **Terminale `CMD` potenziato**: cronologia comandi con frecce `Su/Giù`, completamento percorsi con `Tab`, redirect output (`>` e `>>`) e scrollback con rotella del mouse.

---

## 10. Build, Test & Qualità ([`tools/`](tools/))
- [x] **Suite di Regressione Automatica Unificata**: script `node tools/test_all.js` per testare in sequenza boot BIOS, boot UEFI (`OVMF`), `SECCPL`, `EXPLORER`, `COMCTL`, `WEBBROWSER` e rete con report PASS/FAIL.
- [x] **Controllo automatico Budget Kernel**: alert in `tools/build.js` con percentuale di occupazione `kernel.bin` rispetto a `491.520` byte (480 KB) e verifica `0 warning` sui moduli core.
