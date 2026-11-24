# Piano di Implementazione — Moduli di Ripristino Base e Avanzato (NewOS V1)

Piano per introdurre due moduli protetti esterni (**Ripristino Base** e **Ripristino Avanzato**) richiamabili in fase di boot, mantenendo intatto il bootloader (`boot/stage2.asm`) e il budget del kernel (`kernel.bin < 480 KB`).

---

## 1. Architettura Out-of-Kernel (`RECBASIC.MOD` e `RECADV.MOD`)

Seguendo la regola dei moduli fuori dal kernel e la pipeline `screenModules` di `tools/build.js`, vengono creati due moduli protetti in `C:\NewOS\Boot\` firmati con `module_header_t` (`NOSM`, `SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED`, `"NEWSOFT"`):

- **`recovery/rec_basic.c` → `RECBASIC.MOD` (`RECBASIC.INI`)** — *Ripristino Base*:
  - Verifica automatica dei file critici di boot/sistema (`KERNEL.BIN`, `BOOTSCR.MOD`, `LOGIN.MOD`, `LOGOUT.MOD`, `EXPLORER.DLL`, `NSCOMP.DLL`, `NSMOTION.DLL`, `THEMEMGR.DLL`).
  - Ripristino automatico di eventuali file mancanti o corrotti estraendoli da `C:\BootSetup\SYSTEM.PAK` (AES-256-CBC).
  - Pulizia dei flag di boot bloccanti (`RebootTrick=0` in `C:\NewOS\Boot\boot.ini`, `logout_mode.ini`, `boot_action.ini`).
  - Report finale con pulsante **Riavvia in NewOS V1**.

- **`recovery/rec_adv.c` → `RECADV.MOD` (`RECADV.INI`)** — *Ripristino Avanzato*:
  - Interfaccia grafica interattiva (mouse + tastiera, stile Liquid Glass con fallback sicuro a `gfx_fill_rounded_alpha`) con 4 strumenti:
    1. **Ripristino Completo di Sistema da `SYSTEM.PAK`**: riscrive tutti i binari/librerie di `C:\NewOS\` preservando i dati utente e `C:\ProgramFile\`.
    2. **Ripristino / Reset Registro e Configurazione Boot**: ripristina i valori sicuri del registro e dei file `.INI` di avvio.
    3. **Diagnostica Moduli, VFS & Dischi**: controlla integrità `NOSM`/CRC32 dei moduli, montaggio di `C:\` e stato bus (`vfs_get_drive_bus_info`).
    4. **Console di Ripristino Pre-Desktop**: mini prompt integrato con comandi essenziali (`dir`, `type`, `verify`, `repair`, `fixboot`, `reboot`, `shutdown`).

---

## 2. Punti di Accesso in Fase di Boot

1. **Menu di Boot (`kernel/apps/boot_menu.c`)**:
   - Aggiunta di due voci cliccabili e richiamabili da tastiera senza alterare le scorciatoie esistenti `1..0`:
     - **`R`**: *Ripristino Base (`RECBASIC.MOD`)*
     - **`A`**: *Ripristino Avanzato (`RECADV.MOD`)*
2. **Tasti rapidi, trigger automatico e ordine di inizializzazione (`kernel/kernel.c`)**:
   - Mantenimento di `F8` / `ESC` per forzare il Boot Menu anche quando `boot_menu_should_bypass()` è attivo.
   - Riconoscimento di **`RecoveryMode=BASIC`** o **`RecoveryMode=ADVANCED`** in `C:\NewOS\Boot\boot.ini` (o `boot_action.ini`) per avviare direttamente il ripristino al boot successivo, con reset automatico del flag prima del lancio.
   - **Ordine di inizializzazione sicuro**: l'invocazione di `screen_mgr_run_recovery_*()` deve avvenire **dopo** `vfs_init()`, framebuffer grafico (`gfx_init`/`vga_init`) e driver input (`keyboard_init`/`mouse_init`), ma **prima** e senza dipendenza bloccante dalle `.DLL` di sistema (`EXPLORER.DLL`, `NSCOMP.DLL`, `NSMOTION.DLL`). Se queste `.DLL` sono corrotte, `RECBASIC.MOD` e `RECADV.MOD` devono avviarsi ugualmente usando il fallback a `gfx_fill_rounded_alpha` / `gfx_fill_rect`.

---

## 3. Estensioni ABI e Kernel

- **`kernel/include/screen_modules.h`**:
  - Aggiunta di `MODULE_TYPE_RECOVERY_BASIC` (`9`) e `MODULE_TYPE_RECOVERY_ADV` (`10`).
  - Aggiunta di `recovery_params_t` e `recovery_result_t` (`RECOVERY_REBOOT`, `RECOVERY_SHUTDOWN`, `RECOVERY_CONTINUE`, `RECOVERY_ERROR`).
- **`kernel/gui/screen_mgr.h` e `kernel/gui/screen_mgr.c`**:
  - Aggiunta di `screen_mgr_run_recovery_basic()` e `screen_mgr_run_recovery_advanced()`, con fallback sicuro in caso di modulo mancante.
  - Esposizione via `kernel_ext_api_t` della funzione di verifica/repair da `SYSTEM.PAK` (riutilizzando `repair_files_from_system_pak` / `screen_mgr_shutdown_integrity_check`) per evitare duplicazione di codice nel kernel.
  - **Auto-riparazione preventiva**: aggiungere `NewOS/Boot/RECBASIC.MOD` e `NewOS/Boot/RECADV.MOD` alla lista `critical_rel[]` di `screen_mgr_shutdown_integrity_check()` (`kernel/gui/screen_mgr.c`), così eventuali corruzioni in sessione vengono riparate automaticamente da `SYSTEM.PAK` allo spegnimento.
- **Vincolo `.MOD`**: tutte le stringhe e tabelle interne ai due `.MOD` useranno array contigui 2D o variabili locali sullo stack (nessun puntatore globale assoluto non rilocato).

---

## 4. Build System e Piano di Collaudo

1. Registrare `RECBASIC.MOD`/`RECBASIC.INI` e `RECADV.MOD`/`RECADV.INI` in `screenModules` dentro `tools/build.js`.
2. Compilazione e verifica budget con `node tools/build.js` (`kernel.bin < 480 KB`, zero warning/errori).
3. Esecuzione della suite completa `node tools/test_all.js` (`16 PASS / 0 FAIL`).
4. Collaudo mirato in QEMU con screendump visivo:
   - Accesso a `RECBASIC.MOD` da Boot Menu (`R`) + verifica report e riavvio.
   - Accesso a `RECADV.MOD` da Boot Menu (`A`) + verifica UI, diagnostica e console pre-desktop.
