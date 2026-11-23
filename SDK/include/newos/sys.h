/**
 * newos/sys.h — Namespace Sistema, Telemetria, Power e Architettura x86_64
 */
#ifndef NEWOS_SDK_SYS_H
#define NEWOS_SDK_SYS_H

#include "types.h"
#include <nsrt.h>

typedef enum {
    NS_ARCH_X86_64 = 1
} ns_arch_t;

typedef enum {
    NS_POWER_AC      = 0,
    NS_POWER_BATTERY = 1,
    NS_POWER_LOW     = 2
} ns_power_source_t;

typedef struct {
    ns_arch_t arch;
    uint32_t  abi_version;
    bool      has_ac97;
    bool      has_network;
} ns_platform_caps_t;

static inline void ns_sys_query_caps(const nsrt_api_t* rt, ns_platform_caps_t* out) {
    if (!out) return;
    out->arch = NS_ARCH_X86_64;
    out->abi_version = rt ? rt->version : 0;
    out->has_ac97 = (rt && rt->ac97_is_available) ? rt->ac97_is_available() : false;
    out->has_network = (rt && rt->net_is_connected) ? rt->net_is_connected() : false;
}

#endif /* NEWOS_SDK_SYS_H */
