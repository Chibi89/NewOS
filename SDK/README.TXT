===============================================================================
  NewSoft SDK v2.0 — NewOS V1 (64-bit) Developer Kit
  (C) Newspoint Software - Out-of-Kernel Runtime: NSRT.DLL + XUIML.DLL
===============================================================================

1. STRUTTURA DELL'SDK (C:\NewOS\SDK\)
-------------------------------------------------------------------------------
  C:\NewOS\SDK\include\nsrt.h         -> API principale NewSoft Runtime (v2.0)
  C:\NewOS\SDK\include\xuiml.h        -> Motore UI dichiarativo XUIML.DLL
  C:\NewOS\SDK\include\newos\sdk.h    -> Umbrella header unico (#include <newos/sdk.h>)
  C:\NewOS\SDK\include\newos\types.h  -> Tipi standard freestanding (uint8_t..64, bool)
  C:\NewOS\SDK\include\newos\fs.h     -> Percorsi standard e attributi VFS/ACL
  C:\NewOS\SDK\include\newos\gfx.h    -> Colori ARGB Liquid Glass e geometria 2D
  C:\NewOS\SDK\include\newos\ui.h     -> ID icone di sistema e costanti UI
  C:\NewOS\SDK\include\newos\net.h    -> Helper IPv4 e rete
  C:\NewOS\SDK\include\newos\string.h -> Helper stringa freestanding su NSRT
  C:\NewOS\SDK\examples\              -> Progetti di esempio pronti

2. API PRINCIPALI DI NSRT.DLL (v2.0)
-------------------------------------------------------------------------------
  - Memoria & Stringhe: alloc, calloc, realloc, free, memcpy, memset, memcmp,
                        strlen, strcmp, strncmp, strcpy, strncpy, snprintf
  - File System & ACL : file_exists, file_read, file_write, file_delete,
                        file_size, mkdir, readdir, is_protected, drive_list
  - Sistema & Timing  : log, get_log_count, get_log_line, serial_print,
                        get_ticks, sleep_ms, get_sys_info
  - Audio             : play_sound, play_wav, stop_sound, set_volume, get_volume
  - Rete & Browser    : dns_resolve, net_is_connected, net_get_ip, open_url, notify
  - Registro          : reg_read_sz, reg_read_dword, reg_write_sz (sandboxed)
  - Dialoghi & Clipb. : open_file_dialog, save_file_dialog, clipboard_set/get_text
  - Grafica 2D        : screen_w, screen_h, gfx_fill_rect, gfx_draw_rect,
                        gfx_fill_rounded, gfx_draw_text, gfx_draw_icon16/32
  - UI Event-Driven   : create_xuiml_app, close_app, set_text, get_text,
                        set_int, get_int, list_clear, list_add, get_window_handle

3. REGOLE DI SICUREZZA E SANDBOX ACL
-------------------------------------------------------------------------------
  - La cartella C:\NewOS\ e' protetta in scrittura per le app sandboxed.
  - Il software utente va installato in C:\ProgramFile\<NomeApp>\.
  - I dati utente vanno salvati in C:\User\<Utente>\.
  - Per testare il runtime in tempo reale dal Prompt: digita "start nsrt".
