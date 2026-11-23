/**
 * newos/error.h — Codici di stato e diagnostica uniforme per NewSoft SDK
 */
#ifndef NEWOS_SDK_ERROR_H
#define NEWOS_SDK_ERROR_H

#include "types.h"

#define NS_OK               0
#define NS_ERR_UNKNOWN     (-1)
#define NS_ERR_INVALID     (-2)
#define NS_ERR_NOMEM       (-3)
#define NS_ERR_DENIED      (-4)
#define NS_ERR_NOTFOUND    (-5)
#define NS_ERR_EXISTS      (-6)
#define NS_ERR_IO          (-7)
#define NS_ERR_NET         (-8)
#define NS_ERR_TIMEOUT     (-9)
#define NS_ERR_OVERFLOW    (-10)
#define NS_ERR_UNSUPPORTED (-11)
#define NS_ERR_BUSY        (-12)

#define NS_SUCCEEDED(st) ((st) >= 0)
#define NS_FAILED(st)    ((st) < 0)

static inline const char* ns_status_str(ns_status_t st) {
    switch (st) {
        case NS_OK:              return "OK";
        case NS_ERR_INVALID:     return "Argomento non valido";
        case NS_ERR_NOMEM:       return "Memoria esaurita";
        case NS_ERR_DENIED:      return "Accesso negato (ACL)";
        case NS_ERR_NOTFOUND:    return "Non trovato";
        case NS_ERR_EXISTS:      return "Gia' esistente";
        case NS_ERR_IO:          return "Errore I/O";
        case NS_ERR_NET:         return "Errore di rete";
        case NS_ERR_TIMEOUT:     return "Timeout";
        case NS_ERR_OVERFLOW:    return "Overflow buffer";
        case NS_ERR_UNSUPPORTED: return "Non supportato";
        case NS_ERR_BUSY:        return "Risorsa occupata";
        default:                 return "Errore sconosciuto";
    }
}

#endif /* NEWOS_SDK_ERROR_H */
