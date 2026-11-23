/**
 * newos/app.h — Namespace Ciclo di Vita Applicazioni & Manifest
 */
#ifndef NEWOS_SDK_APP_H
#define NEWOS_SDK_APP_H

#include "types.h"
#include "pkg.h"
#include <nsrt.h>

typedef struct {
    ns_pkg_manifest_t manifest;
    const nsrt_api_t* rt;
    nsrt_app_t*       main_win;
    void*             user_state;
} ns_app_context_t;

#endif /* NEWOS_SDK_APP_H */
