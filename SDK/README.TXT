===============================================================================
  NewSoft SDK v3.0 — NewOS V1 (64-bit) Complete Developer Kit
  (C) Newspoint Software - Out-of-Kernel Runtime: NSRT.DLL v3.0 + XUIML.DLL
===============================================================================

1. HEADER STANDARD C FREESTANDING (C:\NewOS\SDK\include\)
-------------------------------------------------------------------------------
  stdint.h, stddef.h, stdbool.h, limits.h, stdarg.h, errno.h
  nsrt.h   -> Runtime completo NSRT.DLL v3.0
  xuiml.h  -> Motore UI dichiarativo XUIML.DLL

2. TUTTI I NAMESPACE NEWOS (C:\NewOS\SDK\include\newos\)
-------------------------------------------------------------------------------
  newos/sdk.h     -> Umbrella header unico (#include <newos/sdk.h>)
  newos/types.h   -> Tipi esatti, least, fast, intptr/ptrdiff/intmax, limiti,
                     ns_handle_t, ns_status_t, ns_uuid_t, ns_version_t,
                     ns_buffer_t, ns_strview_t, ns_color32_t, macro utilita'
  newos/error.h   -> Codici stato NS_OK / NS_ERR_* e ns_status_str()
  newos/mem.h     -> Arena allocator (ns_arena_t) e Ring Buffer (ns_ringbuf_t)
  newos/string.h  -> Helper stringa freestanding su NSRT
  newos/fs.h      -> Percorsi standard, attributi VFS e permessi ACL
  newos/time.h    -> ns_datetime_t (RTC) e Stopwatch (ns_stopwatch_t)
  newos/sys.h     -> Telemetria sistema, architettura x86_64 e capabilities
  newos/gfx.h     -> Colori ARGB Liquid Glass, ns_point_t, ns_rect_t
  newos/ui.h      -> ID icone di sistema e costanti UI XUIML
  newos/input.h   -> Keycode, maschere mouse e ns_mouse_state_t
  newos/audio.h   -> Formati audio e suoni di sistema (AC97 / WAV)
  newos/net.h     -> Helper IPv4, NS_IP4() e stato rete
  newos/reg.h     -> Hive Registro (HKLM/HKCU) e tipi (SZ, DWORD, BINARY)
  newos/ipc.h     -> Tipi eventi finestra/IPC (ns_ipc_msg_t) e Clipboard
  newos/crypto.h  -> CRC32, FNV-1a 32/64 e codifica hex freestanding
  newos/math.h    -> Fixed-point 16.16 (senza FPU/SSE), vettori 2D/3D/4D, isqrt
  newos/driver.h  -> Costanti bus hardware reali (AHCI, NVMe, xHCI, PCI)
  newos/pkg.h     -> Manifest pacchetti app (ns_pkg_manifest_t) e permessi
  newos/debug.h   -> Macro NS_ASSERT, NS_LOG_INFO/WARN/ERR
  newos/app.h     -> Contesto e ciclo di vita app (ns_app_context_t)

3. ESEMPI PRONTI (C:\NewOS\SDK\examples\)
-------------------------------------------------------------------------------
  hello_xuiml\    -> Finestra XUIML + callback + salvataggio C:\User\
  sys_monitor\    -> Telemetria live, dischi montati e stato rete
