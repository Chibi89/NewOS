/**
 * newos/debug.h — Namespace Debug, Trace e Assertion Freestanding
 */
#ifndef NEWOS_SDK_DEBUG_H
#define NEWOS_SDK_DEBUG_H

#include "types.h"
#include <nsrt.h>

#define NS_LOG_INFO(rt, msg)  do { if ((rt) && (rt)->log) (rt)->log("INFO", (msg)); } while (0)
#define NS_LOG_WARN(rt, msg)  do { if ((rt) && (rt)->log) (rt)->log("WARN", (msg)); } while (0)
#define NS_LOG_ERR(rt, msg)   do { if ((rt) && (rt)->log) (rt)->log("ERR",  (msg)); } while (0)

#define NS_ASSERT(rt, cond) \
    do { \
        if (!(cond)) { \
            if ((rt) && (rt)->log) (rt)->log("ASSERT", #cond); \
            if ((rt) && (rt)->serial_print) (rt)->serial_print("[SDK ASSERT FAILED] " #cond "\n"); \
        } \
    } while (0)

#endif /* NEWOS_SDK_DEBUG_H */
