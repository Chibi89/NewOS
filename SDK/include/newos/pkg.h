/**
 * newos/pkg.h — Namespace Packaging & Permessi App (C:\ProgramFile\<App>\ + PKGSVC.DLL)
 */
#ifndef NEWOS_SDK_PKG_H
#define NEWOS_SDK_PKG_H

#include "types.h"

#define NS_PKG_PERM_FS_USER  NS_BIT(0)
#define NS_PKG_PERM_NET      NS_BIT(1)
#define NS_PKG_PERM_AUDIO    NS_BIT(2)
#define NS_PKG_PERM_REG_APP  NS_BIT(3)
#define NS_PKG_PERM_DIALOGS  NS_BIT(4)

typedef struct {
    char         app_id[32];
    char         display_name[48];
    char         author[32];
    ns_version_t version;
    uint32_t     permissions;
    char         install_dir[64]; /* es. C:\ProgramFile\MyApp\ */
} ns_pkg_manifest_t;

#endif /* NEWOS_SDK_PKG_H */
