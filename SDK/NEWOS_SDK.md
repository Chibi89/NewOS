# NewSoft SDK v3.0 & Runtime (`NSRT.DLL`) — Riferimento Completo

**NewSoft Runtime (`C:\NewOS\SystemLibrary\NSRT.DLL`)** e **NewSoft SDK (`C:\NewOS\SDK\`)** costituiscono il livello di sviluppo ufficiale **100% out-of-kernel** e **freestanding** per **NewOS V1 (64-bit x86_64)**.

- **Magic ABI**: `NSRT_MAGIC = 0x5452534E` (`'NSRT'`)
- **Versione ABI**: `NSRT_VERSION = 0x00030000` (`v3.0`)
- **Sicurezza**: Modulo firmato `NOSM + CRC32` (`MODULE_TYPE_SYSTEM_LIBRARY`) con enforcement ACL integrato.
- **Compilazione standalone**: Gli header in `C:\NewOS\SDK\include\` non dipendono da `libc` dell'host né dagli header interni del kernel (`-ffreestanding -nostdlib`).

---

## 1. Struttura delle Cartelle SDK (`C:\NewOS\SDK\`)

| Percorso | Descrizione |
| :--- | :--- |
| `C:\NewOS\SDK\include\nsrt.h` | Tabella export ufficiale `nsrt_api_t` (`NSRT.DLL` v3.0) |
| `C:\NewOS\SDK\include\xuiml.h` | Interfaccia pubblica del motore UI dichiarativo `XUIML.DLL` |
| `C:\NewOS\SDK\include\stdint.h`, `stddef.h`, `stdbool.h`, `limits.h`, `stdarg.h`, `errno.h` | Header standard C99/C11 freestanding |
| `C:\NewOS\SDK\include\newos\sdk.h` | Umbrella header unico (`#include <newos/sdk.h>`) |
| `C:\NewOS\SDK\include\newos\*.h` | Tutti i **21 namespace** funzionali dell'OS |
| `C:\NewOS\SDK\examples\` | Progetti pilota (`hello_xuiml`, `sys_monitor`) |
| `C:\NewOS\SDK\README.TXT` | Guida rapida su disco |

---

## 2. Riepilogo Completo di `NSRT.DLL` (`nsrt_api_t`)

L'entry point della libreria (`nsrt_dll_entry(const kernel_api_t* api)`) restituisce il puntatore immutabile `const nsrt_api_t*` contenente tutti i servizi del runtime:

### 2.1 Memoria & Stringhe Freestanding
| Campo `nsrt_api_t` | Firma | Funzione |
| :--- | :--- | :--- |
| `alloc` | `void* (*alloc)(size_t size)` | Alloca blocco su heap di sistema |
| `calloc` | `void* (*calloc)(size_t count, size_t elem_size)` | Alloca e azzera (`volatile` safe) |
| `realloc` | `void* (*realloc)(void* ptr, size_t old_size, size_t new_size)` | Ridimensiona blocco copiando i dati |
| `free` | `void (*free)(void* ptr)` | Libera blocco allocato |
| `memcpy` | `void* (*memcpy)(void* dst, const void* src, size_t n)` | Copia memoria byte-a-byte |
| `memset` | `void* (*memset)(void* dst, int c, size_t n)` | Riempie memoria |
| `memcmp` | `int (*memcmp)(const void* a, const void* b, size_t n)` | Confronta buffer |
| `strlen` | `size_t (*strlen)(const char* s)` | Lunghezza stringa C |
| `strcmp` | `int (*strcmp)(const char* a, const char* b)` | Confronto stringhe |
| `strncmp` | `int (*strncmp)(const char* a, const char* b, size_t n)` | Confronto fino a `n` caratteri |
| `strcpy` | `char* (*strcpy)(char* dst, const char* src)` | Copia stringa |
| `strncpy` | `char* (*strncpy)(char* dst, const char* src, size_t n)` | Copia stringa con terminatore garantito |
| `snprintf` | `int (*snprintf)(char* buf, size_t size, const char* fmt, ...)` | Formattazione freestanding (`%s`, `%d`, `%i`, `%u`, `%x`, `%X`, `%p`, `%c`, `%%`) |

### 2.2 File System & Sandbox ACL
| Campo `nsrt_api_t` | Firma | Funzione |
| :--- | :--- | :--- |
| `file_exists` | `bool (*file_exists)(const char* path)` | Verifica esistenza file/percorso |
| `file_read` | `void* (*file_read)(const char* path, uint32_t* out_size)` | Legge intero file in memoria (liberare con `free`) |
| `file_write` | `bool (*file_write)(const char* path, const void* data, uint32_t size, bool sandboxed)` | Scrive file; se `sandboxed == true`, **blocca `C:\NewOS\`** |
| `file_delete` | `bool (*file_delete)(const char* path, bool sandboxed)` | Elimina file; rispetta sandbox ACL su `C:\NewOS\` |
| `file_size` | `uint64_t (*file_size)(const char* path)` | Restituisce dimensione file in byte |
| `mkdir` | `bool (*mkdir)(const char* path, bool sandboxed)` | Crea directory; rispetta sandbox ACL |
| `readdir` | `int32_t (*readdir)(const char* dir_path, ns_dirent_t* out_entries, int32_t max_entries)` | Elenca voci directory in `ns_dirent_t` |
| `is_protected` | `bool (*is_protected)(const char* path)` | Controlla se il percorso ricade sotto `C:\NewOS\` |
| `drive_list` | `int (*drive_list)(char* out_letters, int max_drives)` | Restituisce le lettere delle unità montate (`C`, `D`, `R`, ...) |

### 2.3 Logging, Timing, Input & Sistema
| Campo `nsrt_api_t` | Firma | Funzione |
| :--- | :--- | :--- |
| `log` | `void (*log)(const char* tag, const char* msg)` | Scrive nel ring-buffer diagnostico (`8` righe, `56` char) |
| `get_log_count` | `int (*get_log_count)(void)` | Numero di righe nel buffer log |
| `get_log_line` | `const char* (*get_log_line)(int idx)` | Legge riga di log per indice |
| `serial_print` | `void (*serial_print)(const char* msg)` | Invia output su porta seriale COM1 |
| `get_ticks` | `uint64_t (*get_ticks)(void)` | Tick correnti del timer di sistema |
| `sleep_ms` | `void (*sleep_ms)(uint32_t ms)` | Attesa in millisecondi |
| `get_sys_info` | `bool (*get_sys_info)(ns_sys_info_t* out_info)` | Risoluzione schermo, uptime, cluster exFAT, drive count, utente e PC |
| `get_rtc_time` | `void (*get_rtc_time)(ns_datetime_t* out_dt)` | Data e ora hardware RTC (`second`, `minute`, `hour`, `day`, `month`, `year`) |
| `mouse_get_state` | `void (*mouse_get_state)(ns_mouse_state_t* out_state)` | Coordinate e bottoni correnti del mouse |
| `keyboard_has_char` | `bool (*keyboard_has_char)(void)` | Verifica presenza carattere nel buffer tastiera |
| `keyboard_get_char` | `char (*keyboard_get_char)(void)` | Legge prossimo carattere dalla tastiera |
| `crc32` | `uint32_t (*crc32)(const void* data, size_t len)` | Calcola checksum CRC32 IEEE 802.3 |

### 2.4 Audio & Multimedia
| Campo `nsrt_api_t` | Firma | Funzione |
| :--- | :--- | :--- |
| `play_sound` | `void (*play_sound)(int sound_id)` | Riproduce suono di sistema (`0`=click, `1`=notify, `2`=error) |
| `play_wav` | `bool (*play_wav)(const char* path)` | Riproduce file `.wav` da VFS |
| `stop_sound` | `void (*stop_sound)(void)` | Ferma la riproduzione audio |
| `set_volume` | `void (*set_volume)(uint8_t vol)` | Imposta volume master (`0..100`) |
| `get_volume` | `uint8_t (*get_volume)(void)` | Legge volume corrente |
| `ac97_is_available` | `bool (*ac97_is_available)(void)` | Verifica presenza controller AC97 |

### 2.5 Rete, Browser & Notifiche
| Campo `nsrt_api_t` | Firma | Funzione |
| :--- | :--- | :--- |
| `dns_resolve` | `uint32_t (*dns_resolve)(const char* hostname)` | Risolve hostname in IPv4 |
| `net_is_connected` | `bool (*net_is_connected)(void)` | Verifica se esiste una NIC attiva |
| `net_get_ip` | `bool (*net_get_ip)(char* out_buf, size_t max_len)` | Restituisce stato DHCP/NIC |
| `open_url` | `void (*open_url)(const char* url)` | Apre URL nel Web Browser di sistema |
| `notify` | `void (*notify)(const char* title, const char* msg)` | Registra notifica applicativa nel log di sistema |

### 2.6 Registro di Sistema
| Campo `nsrt_api_t` | Firma | Funzione |
| :--- | :--- | :--- |
| `reg_read_sz` | `bool (*reg_read_sz)(const char* path, const char* val_name, char* out_buf, size_t max_len)` | Legge valore stringa dal Registro |
| `reg_read_dword` | `bool (*reg_read_dword)(const char* path, const char* val_name, uint32_t* out_val)` | Legge valore `DWORD` dal Registro |
| `reg_write_sz` | `bool (*reg_write_sz)(const char* path, const char* val_name, const char* val, bool sandboxed)` | Scrive stringa; in sandbox permette solo `HKLM\Software\NewSoft\` e `HKCU\Software\` |

### 2.7 Dialoghi di Sistema (`COMDLG.DLL`) & Clipboard
| Campo `nsrt_api_t` | Firma | Funzione |
| :--- | :--- | :--- |
| `open_file_dialog` | `bool (*open_file_dialog)(const char* init_dir, void* cb, void* userdata)` | Apre dialogo di selezione file |
| `save_file_dialog` | `bool (*save_file_dialog)(const char* init_dir, const char* def_name, void* cb, void* userdata)` | Apre dialogo di salvataggio file |
| `clipboard_set_text` | `void (*clipboard_set_text)(const char* text)` | Copia testo negli appunti globali |
| `clipboard_get_text` | `const char* (*clipboard_get_text)(void)` | Legge testo dagli appunti globali |

### 2.8 Primitive Grafiche 2D
| Campo `nsrt_api_t` | Firma | Funzione |
| :--- | :--- | :--- |
| `screen_w` / `screen_h` | `uint32_t (*screen_w/h)(void)` | Dimensioni schermo correnti |
| `gfx_fill_rect` | `void (*gfx_fill_rect)(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color)` | Rettangolo pieno ARGB |
| `gfx_draw_rect` | `void (*gfx_draw_rect)(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color)` | Bordo rettangolo ARGB |
| `gfx_fill_rounded` | `void (*gfx_fill_rounded)(int32_t x, int32_t y, int32_t w, int32_t h, int32_t radius, uint32_t color)` | Rettangolo smussato pieno |
| `gfx_draw_text` | `void (*gfx_draw_text)(int32_t x, int32_t y, const char* str, uint32_t color, uint32_t bg, bool transparent)` | Disegna stringa con font di sistema |
| `gfx_draw_icon16` / `32` | `void (*gfx_draw_icon16/32)(int32_t x, int32_t y, int icon_id)` | Disegna icona da `ICONS.DLL` (`16x16` o `32x32`) |

### 2.9 Finestre & UI Event-Driven (`XUIML.DLL`)
| Campo `nsrt_api_t` | Firma | Funzione |
| :--- | :--- | :--- |
| `create_xuiml_app` | `nsrt_app_t* (*create_xuiml_app)(const char* title, int32_t w, int32_t h, const char* xuiml_path, nsrt_event_cb on_event, void* user_ctx)` | Carica un file `.xuiml`, crea la finestra WM centrata e collega la callback eventi `nsrt_event_cb` |
| `close_app` | `void (*close_app)(nsrt_app_t* app)` | Chiude e distrugge la finestra dell'app |
| `set_text` / `get_text` | `void (*set_text)(...)` / `const char* (*get_text)(...)` | Data-binding testuale per controlli identificati da `id` |
| `set_int` / `get_int` | `void (*set_int)(...)` / `int (*get_int)(...)` | Data-binding numerico (checkbox, slider, progress) |
| `list_clear` / `list_add` | `void (*list_clear)(...)` / `void (*list_add)(...)` | Gestione voci di un controllo `<List id="..." />` |
| `get_window_handle` | `void* (*get_window_handle)(nsrt_app_t* app)` | Restituisce l'handle opaco `window_t*` sottostante |

---

## 3. I 21 Namespace & Tipi di Dato (`newos/*.h`)

| Header | Namespace / Contenuto Principale |
| :--- | :--- |
| `<newos/sdk.h>` | Umbrella header unico + macro `NS_APP_EXPORT`, `NS_LOG`, `NS_ON_CLICK` |
| `<newos/types.h>` | Tutti i tipi C99/C11 (`int8_t`..`uint64_t`, `least`, `fast`, `intptr_t`, `ptrdiff_t`, `intmax_t`, `char16_t`, `char32_t`, `wchar_t`, `max_align_t`), limiti (`INT8_MIN`..`SIZE_MAX`), tipi SDK (`ns_handle_t`, `ns_status_t`, `ns_color32_t`, `ns_timestamp_t`, `ns_flags_t`, `ns_version_t`, `ns_uuid_t`, `ns_buffer_t`, `ns_strview_t`, `ns_kv_int_t`, `ns_kv_str_t`), macro (`NS_ARRAY_LEN`, `NS_MIN`, `NS_MAX`, `NS_CLAMP`, `NS_BIT`, `NS_ALIGN_UP/DOWN`, `NS_STATIC_ASSERT`) |
| `<newos/error.h>` | Codici `NS_OK`, `NS_ERR_*`, macro `NS_SUCCEEDED`/`NS_FAILED`, `ns_status_str()` |
| `<newos/mem.h>` | `ns_arena_t` (`ns_arena_init/alloc/reset`), `ns_ringbuf_t` (`ns_ringbuf_init/push/pop`) |
| `<newos/string.h>` | Helper inline `ns_str_eq()`, `ns_str_starts_with()` |
| `<newos/fs.h>` | Percorsi standard (`NS_PATH_SYSTEM`, `NS_PATH_SYSLIB`, `NS_PATH_UI`, `NS_PATH_SDK`, `NS_PATH_PROGRAMFILE`, `NS_PATH_USER`), attributi `NS_ATTR_*`, permessi `NS_PERM_*` |
| `<newos/time.h>` | `ns_stopwatch_t` (`ns_stopwatch_start/elapsed/stop`) e supporto `ns_datetime_t` |
| `<newos/sys.h>` | `ns_arch_t`, `ns_power_source_t`, `ns_platform_caps_t`, `ns_sys_query_caps()` |
| `<newos/gfx.h>` | `NS_RGB`/`NS_RGBA`, palette Liquid Glass (`NS_COLOR_*`), `ns_point_t`, `ns_rect_t`, `ns_rect_contains()` |
| `<newos/ui.h>` | Costanti `NS_ICON_*` e `NS_SOUND_*` |
| `<newos/input.h>` | `NS_KEY_*`, `ns_mouse_btn_t`, `ns_mouse_buttons_mask()` |
| `<newos/audio.h>` | `ns_audio_format_t`, `ns_sound_event_t`, `ns_audio_beep()` |
| `<newos/net.h>` | `NS_IP4(a,b,c,d)`, `ns_ip4_octet()` |
| `<newos/reg.h>` | Hive sandbox (`NS_REG_HKLM_SOFTWARE`, `NS_REG_HKCU_SOFTWARE`) e `ns_reg_type_t` |
| `<newos/ipc.h>` | `ns_event_type_t` (`NS_EV_*`), `ns_ipc_msg_t` |
| `<newos/crypto.h>` | `ns_fnv1a_32()`, `ns_fnv1a_64()`, `ns_hex_encode()` |
| `<newos/math.h>` | Fixed-point 16.16 (`ns_fixed16_t` senza FPU/SSE), `ns_vec2i_t`, `ns_vec3i_t`, `ns_vec4i_t`, `ns_vec2f_t`, `ns_vec3f_t`, `ns_mat4f_t`, `ns_lerp_i32()`, `ns_isqrt()` |
| `<newos/driver.h>` | Enum `ns_bus_type_t` (`SATA_AHCI`, `NVME`, `USB_XHCI`, ...), `ns_bus_info_t`, `ns_pci_id_t` |
| `<newos/pkg.h>` | `NS_PKG_PERM_*`, `ns_pkg_manifest_t` per `C:\ProgramFile\<App>\` |
| `<newos/debug.h>` | Macro `NS_LOG_INFO/WARN/ERR`, `NS_ASSERT()` |
| `<newos/app.h>` | Struttura `ns_app_context_t` per ciclo di vita applicativo |

---

## 4. Collaudo Live in NewOS (`Dev Console`)

Dal Prompt dei comandi (`CMD`) di NewOS V1 è possibile avviare in qualsiasi momento la console diagnostica del runtime:

```text
start nsrt
```
*(oppure `start sdk`)*

La finestra **`NewSoft Runtime (NSRT.DLL) - Dev Console`** verifica dal vivo allocazione heap, protezione ACL su `C:\NewOS\`, scrittura utente in `C:\User\`, presenza degli header SDK, orologio RTC e checksum `CRC32`.
