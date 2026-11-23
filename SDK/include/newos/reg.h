/**
 * newos/reg.h — Namespace Registro di Sistema (Hive, Tipi e Chiavi Sandbox)
 */
#ifndef NEWOS_SDK_REG_H
#define NEWOS_SDK_REG_H

#include "types.h"

#define NS_REG_HKLM_SOFTWARE  "HKLM\\Software\\NewSoft\\"
#define NS_REG_HKCU_SOFTWARE  "HKCU\\Software\\"
#define NS_REG_HKLM_SYSTEM    "HKLM\\System\\CurrentControlSet\\"

typedef enum {
    NS_REG_NONE   = 0,
    NS_REG_SZ     = 1,
    NS_REG_DWORD  = 2,
    NS_REG_BINARY = 3
} ns_reg_type_t;

#endif /* NEWOS_SDK_REG_H */
