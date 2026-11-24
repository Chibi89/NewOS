# Panoramica Tecnica — Sottosistema Logout (NewOS V1)

Documento di riepilogo dell'architettura, delle strutture dati, dei flag e del flusso di esecuzione del sottosistema **Logout** in NewOS V1 (64-bit).

---

## 1. Modulo Esterno e Header di Sicurezza (`LOGOUT.MOD`)

Il logout è implementato come modulo protetto esterno al kernel in [`logoutscreen/logoutscreen.c`](logoutscreen/logoutscreen.c) con header [`logoutscreen/logoutscreen.h`](logoutscreen/logoutscreen.h).

All'offset `0` è presente l'intestazione `module_header_t` (`g_logoutscreen_header`, 64 byte esatti, sezione `.header`) definita in [`kernel/include/screen_modules.h`](kernel/include/screen_modules.h):

- **`magic`**: `MODULE_MAGIC_NEWOS` (`0x4D534F4E` / `'NOSM'`)
- **`header_size`**: `sizeof(module_header_t)` (64 byte)
- **`module_version`**: `MODULE_VERSION_CURRENT` (`0x00010000`)
- **`module_type`**: `MODULE_TYPE_LOGOUTSCREEN` (`3`)
- **`entry_offset`**: `64` (`logoutscreen_main` in sezione `.text.entry`)
- **`security_flags`**:
  - `SEC_FLAG_PROTECTED` (`0x01`)
  - `SEC_FLAG_SYSTEM_SIGNED` (`0x02`)
  - `SEC_FLAG_VERIFIED` (`0x04`)
- **`module_name`**: `"LOGOUTSCREEN"`
- **`signature`**: `MODULE_AUTH_TAG` (`"NEWSOFT"`)

---

## 2. Tipi, Enum e Strutture Dati (ABI Kernel ↔ Modulo)

Definiti in [`kernel/include/screen_modules.h`](kernel/include/screen_modules.h):

### Modalità di ingresso: `logout_mode_t`

Indica l'azione ereditata dalla sessione utente attiva:

| Costante | Valore | Comportamento |
| :--- | :---: | :--- |
| `LOGOUT_MODE_SHUTDOWN` | `1` | Salva configurazioni/registro, esegue `SafeCheck`, ripara eventuali file critici da `SYSTEM.PAK`, chiude le sessioni e spegne. |
| `LOGOUT_MODE_CHANGE_USER` | `2` | *Fast User Switching*: mantiene aperta la sessione attiva in background e apre il Login. |
| `LOGOUT_MODE_CLOSE_SESSION` | `3` | Chiude la sessione dell'utente attivo (distruggendo le finestre) e torna al Login. |
| `LOGOUT_MODE_REBOOT` | `4` | Salva `RebootTrick=1` e `LastTrigger=REBOOT` in `C:\NewOS\Boot\boot.ini`, poi prosegue come `LOGOUT_MODE_SHUTDOWN`. |
| `LOGOUT_MODE_SUSPEND` | `5` | Attiva la modalità standby/risparmio energia dello schermo. |

### Parametri di chiamata: `logoutscreen_params_t`

```c
typedef struct {
  const char *current_user;
  logout_mode_t mode;
} logoutscreen_params_t;
```

- **`current_user`**: nome utente della sessione attiva (fallback `"Morgan"`).
- **`mode`**: modalità richiesta (`logout_mode_t`).

### Valori di ritorno: `logoutscreen_result_t`

| Costante | Valore | Significato |
| :--- | :---: | :--- |
| `LOGOUT_ACTION_DISCONNECT` | `0` | Chiusura sessione utente attivo (`CloseUserSession`). |
| `LOGOUT_ACTION_SHUTDOWN` | `1` | Arresto sistema. |
| `LOGOUT_ACTION_REBOOT` | `2` | Riavvio sistema. |
| `LOGOUT_ACTION_SUSPEND` | `3` | Standby schermo. |
| `LOGOUT_ACTION_CANCEL` | `4` | Annullamento (restituito anche se `api == NULL`). |
| `LOGOUT_ACTION_CHANGE_USER` | `5` | Cambio utente lasciando la sessione attiva (`ChangeUser`). |

### Interfacce e strutture collegate

- **`kernel_api_t`** (`kernel/include/screen_modules.h`): tabella ABI base esposta ai moduli per grafica (`gfx_*`), caricamento immagini (`image_t`), audio (`sound_play_wav_file`), timer (`timer_sleep`), utente (`user_get_current`) e power management (`power_shutdown`, `power_reboot`).
- **`kernel_ext_api_t`** (`api->ext_api`): estensioni utilizzate da `LOGOUT.MOD`:
  - `nscomp_apply_window_refraction` e `gfx_fill_rounded_glass`
  - `get_system_service("NSMOTION")` / `nsmotion_get_api()`
  - `boot_set_reboot_trick()`
  - `system_shutdown_integrity_check()`
- **`nsmotion_api_t`**: fornisce `ease_q16(NSM_EASE_OUT_CUBIC, t_q16)` per animare card e progress bar in `render_logout_card()`.
- **`user_profile_t`**: profilo utente restituito da `api->user_get_current()`.
- **`shutdown_pak_hdr_t`** (`kernel/gui/screen_mgr.c`): header AES-256-CBC (`"NOSPAK10"`) usato da `repair_files_from_system_pak()` per decifrare `SYSTEM.PAK` e ripristinare i file critici danneggiati.

---

## 3. Flusso di Esecuzione e Integrazione Kernel/Desktop

1. **Risoluzione della modalità e caricamento (`kernel/gui/screen_mgr.c`)**
   - `screen_mgr_run_logoutscreen()` legge `C:\NewOS\Boot\logout_mode.ini` (primo carattere `'1'`–`'5'`, default `LOGOUT_MODE_SHUTDOWN`) e invoca `screen_mgr_run_logoutscreen_mode(mode)`.
   - `screen_mgr_run_logoutscreen_mode(mode)` cerca `LOGOUT.MOD` (`C:\NewOS\Boot\LOGOUT.MOD`, `C:\NewOS\SystemProgram\LOGOUT.MOD`, ecc.), lo valida tramite `screen_mgr_verify_module(...)`, prepara `logoutscreen_params_t` e chiama `logoutscreen_main(&g_kernel_api, &p)`.
   - In caso di modulo assente o non valido, interviene `logoutscreen_fallback()` che esegue `power_shutdown()` e ritorna `LOGOUT_ACTION_SHUTDOWN`.

2. **Sequenza interna in `logoutscreen_main()` (`logoutscreen/logoutscreen.c`)**
   - Carica lo sfondo ufficiale `LOGOUT.BMP` (da `C:\NewOS\Resources\Screens\LOGOUT.BMP` o percorsi alternativi).
   - Disegna la card glass/refracted (`500x220`) con barra di avanzamento animata e colore accento dedicato per modalità:
     - **`LOGOUT_MODE_CHANGE_USER`** (accento `0x2563EB`) → ritorna `LOGOUT_ACTION_CHANGE_USER`.
     - **`LOGOUT_MODE_CLOSE_SESSION`** (accento `0x7C3AED`) → riproduce `RECYCLE.WAV`, ritorna `LOGOUT_ACTION_DISCONNECT`.
     - **`LOGOUT_MODE_SUSPEND`** (accento `0xD97706`) → ritorna `LOGOUT_ACTION_SUSPEND`.
     - **`LOGOUT_MODE_REBOOT`** (accento `0x059669`) → chiama `ext->boot_set_reboot_trick()` (aggiorna `C:\NewOS\Boot\boot.ini` preservando le altre chiavi), esegue la sequenza completa di `LOGOUT_MODE_SHUTDOWN` e al termine invoca `api->power_reboot()` ritornando `LOGOUT_ACTION_REBOOT`. Inoltre `power_shutdown()` (`kernel/drivers/power.c`) verifica `boot_is_reboot_trick_active()` deviando automaticamente a `power_reboot()` anziché entrare in ACPI S5.
     - **`LOGOUT_MODE_SHUTDOWN`** (accento `0xDC2626`) → esegue:
       1. Salvataggio impostazioni utente, audit e salvataggio registro (`screen_mgr_shutdown_integrity_check()`).
       2. Controllo stabilità dei file critici (`KERNEL.BIN`, `BOOTSCR.MOD`, `LOGIN.MOD`, `LOGOUT.MOD`, `EXPLORER.DLL`, `NSCOMP.DLL`, `NSMOTION.DLL`, `THEMEMGR.DLL`) con eventuale riparazione da `SYSTEM.PAK`.
       3. Riproduzione `RECYCLE.WAV`, attesa `450 ms`, chiamata `api->power_shutdown()` e ritorno `LOGOUT_ACTION_SHUTDOWN`.

3. **Gestione post-logout in `kernel/gui/wm.c` e `kernel/kernel.c`**
   - Dal desktop (`wm.c`), prima del logout viene eseguita l'animazione di uscita dello sfondo (`wm_animate_wallpaper_transition(false)`).
   - In base al `logoutscreen_result_t` restituito:
     - `LOGOUT_ACTION_SHUTDOWN` → `power_shutdown()`
     - `LOGOUT_ACTION_REBOOT` → `power_reboot()`
     - `LOGOUT_ACTION_SUSPEND` → `power_enter_screen_standby()`
     - `LOGOUT_ACTION_CHANGE_USER` → salva utente e registro mantenendo le finestre aperte, avvia `screen_mgr_run_loginscreen()`, ricarica le preferenze in sola lettura e riesegue `wm_animate_wallpaper_transition(true)`.
     - `LOGOUT_ACTION_DISCONNECT` → salva utente e registro, distrugge tutte le finestre aperte (`wm_destroy_window()`), avvia `screen_mgr_run_loginscreen()`, ricarica le preferenze e riesegue `wm_animate_wallpaper_transition(true)`.
