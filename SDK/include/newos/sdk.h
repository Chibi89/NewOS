/**
 * newos/sdk.h — Umbrella Header Ufficiale NewSoft SDK v2.0 (NewOS V1 64-bit)
 * Include tutti gli header freestanding dell'SDK e fornisce macro rapide
 * per inizializzazione runtime, logging e gestione eventi XUIML.
 */
#ifndef NEWOS_SDK_UMBRELLA_H
#define NEWOS_SDK_UMBRELLA_H

#include "types.h"
#include "../nsrt.h"
#include "../xuiml.h"
#include "fs.h"
#include "gfx.h"
#include "ui.h"
#include "net.h"
#include "string.h"

#define NS_APP_EXPORT __attribute__((section(".entry"), used))

#define NS_LOG(rt, tag, msg) do { if ((rt) && (rt)->log) (rt)->log((tag), (msg)); } while (0)
#define NS_ON_CLICK(id_str)  if (ns_str_eq(rt, control_id, (id_str)))

#endif /* NEWOS_SDK_UMBRELLA_H */
