# NewOS V1 (64-bit) — Specifiche Tecniche
## Architettura di Bypass del Boot Manager post-Installazione

**Data Documento**: 27-28 Ottobre 2026  
**Stato Specifica**: Convalidata & Implementata (Strategia A)  
**Autore**: Morgan / NewSoft Architect  
**Target Kernel**: Ring 0 / Bootloader Stage 2  

---

### 1. Obiettivo dell'Ottimizzazione (UX & Logica)
L'attuale catena di boot di NewOS V1 mostra una ridondanza logica post-installazione: il **Boot Manager** continua a presentare a ogni avvio la finestra di dialogo modale a scorrimento per selezionare l'ambiente Live o l'esecutivo BootSetup. 

Una volta che i cluster del file system exFAT su `C:\` sono stati estratti dal container cifrato `SYSTEM.PAK` ed il database `REGISTRY.DAT` è stato inizializzato con successo tramite la procedura guidata OOBE, tale menu perde la sua utilità tecnica. L'obiettivo è implementare un meccanismo di **Bypass Atomico Automatico** per saltare l'interfaccia interattiva del Boot Menu, dirottando la CPU direttamente al modulo protetto di autenticazione e accesso **`LOGIN.MOD`** (`C:\NewOS\Boot\LOGIN.MOD`), e soltanto a valle dell'autenticazione convalidata alla sessione grafica del desktop.

---

### 2. Flag di Stato nel Registro Nativo NewOS
Il perimetro di sicurezza di NewOS V1 memorizza la variabile di avanzamento all'interno del database centrale blindato in AES-256 CBC. Durante l'esecuzione del Passo 5 dell'OOBE (Completamento Wizard), l'esecutivo `oobe.c` scrive esplicitamente i seguenti record logici nel database prima del flush su disco:

- **Ramo (Hive)**: `HKEY_LOCAL_MACHINE\NEWOS\SYSTEM`
- **Chiave**: `SetupStage`
- **Valore**: `Completed`

- **Ramo (Hive)**: `HKEY_LOCAL_MACHINE\NEWOS\SYSTEM`
- **Chiave**: `OOBECompleted`
- **Valore**: `1`

---

### 3. Flussi di Esecuzione Comparati

*   **Flusso Precedente (Ridondante):**  
    `BIOS/UEFI` $\rightarrow$ `Boot Manager Menu [Scelta Utente]` $\rightarrow$ `Caricamento Kernel Ring 0` $\rightarrow$ `Verifica Registro` $\rightarrow$ `Desktop`.
*   **Flusso Ottimizzato (Bypass Automatico Attivo):**  
    `BIOS/UEFI` $\rightarrow$ `Caricamento Kernel Ring 0` $\rightarrow$ `Verifica Registro [SetupStage == "Completed"]` $\rightarrow$ `Bypass Atomico` $\rightarrow$ `LOGIN.MOD` $\rightarrow$ `Desktop`.

---

### 4. Implementazione Architetturale Adottata (Strategia A: Kernel Core)

L'intercettazione è implementata nel modulo centrale di boot [`kernel/kernel.c`](file:///c:/Users/morga/Desktop/MyOS/kernel/kernel.c) supportata dalle routine native in [`kernel/apps/boot_menu.c`](file:///c:/Users/morga/Desktop/MyOS/kernel/apps/boot_menu.c) e [`kernel/fs/registry.c`](file:///c:/Users/morga/Desktop/MyOS/kernel/fs/registry.c):

1. **Interrogazione Multi-Stadio (`boot_menu_should_bypass`)**:
   - `registry_read_string("HKEY_LOCAL_MACHINE\\NEWOS\\SYSTEM", "SetupStage") == "Completed"`
   - `registry_get_value_dword("HKEY_LOCAL_MACHINE\\NEWOS\\SYSTEM", "OOBECompleted", &oobe_done) == 1`
   - Fallback sicuro sul file di lock del file system: `oobe_is_completed()` (`C:\NewOS\OOBE\OOBE.INI`).

2. **Override di Emergenza Utente (F8 / ESC)**:
   - Qualora l'utente prema o mantenga premuto il tasto **F8** (scancode `0x42`) o **ESC** (scancode `0x01` o codice ASCII 27) durante la fase di boot, il kernel annulla il bypass e visualizza il Boot Menu completo per consentire operazioni di ripristino o diagnostica.

3. **Invocazione Atomica di `LOGIN.MOD` (`boot_action_set_direct_login`)**:
   - Emette traccia seriale di sicurezza e record di audit nel registro (`BOOT_BYPASS`).
   - Esegue direttamente `screen_mgr_run_loginscreen()`.
   - A valle dell'autenticazione, carica le preferenze in sola lettura (`registry_logon_load_readonly_preferences()`), sblocca la scrittura del registro (`registry_set_read_only(false)`) e avvia l'ambiente Desktop.
