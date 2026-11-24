/**
 * newos/sdk.h — Umbrella Header Ufficiale NewSoft SDK v3.0 (NewOS V1 64-bit)
 * Copertura completa di tutti i namespace di sistema e tipi di dato freestanding.
 */
#ifndef NEWOS_SDK_UMBRELLA_H
#define NEWOS_SDK_UMBRELLA_H

#include "types.h"
#include "error.h"
#include <nsrt.h>
#include <xuiml.h>
#include "mem.h"
#include "string.h"
#include "fs.h"
#include "time.h"
#include "sys.h"
#include "gfx.h"
#include "ui.h"
#include "input.h"
#include "audio.h"
#include "net.h"
#include "reg.h"
#include "ipc.h"
#include "crypto.h"
#include "math.h"
#include "driver.h"
#include "pkg.h"
#include "debug.h"
#include "motion.h"
#include "doc.h"
#include "calc.h"
#include "app.h"

#define NS_APP_EXPORT __attribute__((section(".entry"), used))

#define NS_LOG(rt, tag, msg) do { if ((rt) && (rt)->log) (rt)->log((tag), (msg)); } while (0)
#define NS_ON_CLICK(id_str)  if (ns_str_eq(rt, control_id, (id_str)))

#endif /* NEWOS_SDK_UMBRELLA_H */
