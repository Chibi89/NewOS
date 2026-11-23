/**
 * newos/reg.h — Namespace Registro di Sistema (Lettura globale + Scrittura settori non critici)
 */
#ifndef NEWOS_SDK_REG_H
#define NEWOS_SDK_REG_H

#include "types.h"
#include "error.h"
#include <nsrt.h>

#define NS_REG_HKLM_SOFTWARE  "HKLM\\Software\\NewSoft\\"
#define NS_REG_HKCU_SOFTWARE  "HKCU\\Software\\"
#define NS_REG_HKCU_CPL       "HKCU\\ControlPanel\\"
#define NS_REG_HKCU_ENV       "HKCU\\Environment\\"
#define NS_REG_HKLM_SYSTEM    "HKLM\\System\\CurrentControlSet\\"

typedef enum {
    NS_REG_NONE   = 0,
    NS_REG_SZ     = 1,
    NS_REG_DWORD  = 2,
    NS_REG_BINARY = 3
} ns_reg_type_t;

static inline bool ns_reg_is_non_critical(const nsrt_api_t* rt, const char* path) {
    return (rt && rt->reg_is_non_critical && path) ? rt->reg_is_non_critical(path) : false;
}

static inline ns_status_t ns_reg_read_sz(const nsrt_api_t* rt, const char* path,
                                         const char* val_name, char* out_buf, size_t max_len) {
    if (!rt || !rt->reg_read_sz || !path || !val_name || !out_buf || max_len == 0) return NS_ERR_INVALID;
    return rt->reg_read_sz(path, val_name, out_buf, max_len) ? NS_OK : NS_ERR_NOTFOUND;
}

static inline ns_status_t ns_reg_read_dword(const nsrt_api_t* rt, const char* path,
                                            const char* val_name, uint32_t* out_val) {
    if (!rt || !rt->reg_read_dword || !path || !val_name || !out_val) return NS_ERR_INVALID;
    return rt->reg_read_dword(path, val_name, out_val) ? NS_OK : NS_ERR_NOTFOUND;
}

static inline ns_status_t ns_reg_write_sz(const nsrt_api_t* rt, const char* path,
                                          const char* val_name, const char* val) {
    if (!rt || !rt->reg_write_sz || !path || !val_name || !val) return NS_ERR_INVALID;
    if (rt->reg_is_non_critical && !rt->reg_is_non_critical(path)) return NS_ERR_DENIED;
    return rt->reg_write_sz(path, val_name, val, true) ? NS_OK : NS_ERR_DENIED;
}

static inline ns_status_t ns_reg_write_dword(const nsrt_api_t* rt, const char* path,
                                             const char* val_name, uint32_t val) {
    if (!rt || !rt->reg_write_dword || !path || !val_name) return NS_ERR_INVALID;
    if (rt->reg_is_non_critical && !rt->reg_is_non_critical(path)) return NS_ERR_DENIED;
    return rt->reg_write_dword(path, val_name, val, true) ? NS_OK : NS_ERR_DENIED;
}

#endif /* NEWOS_SDK_REG_H */
