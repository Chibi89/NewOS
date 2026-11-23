/**
 * newos/fs.h — Costanti File System, Percorsi Standard e Permessi ACL NewOS V1
 */
#ifndef NEWOS_SDK_FS_H
#define NEWOS_SDK_FS_H

#define NS_PATH_SYSTEM      "C:\\NewOS\\"
#define NS_PATH_SYSLIB      "C:\\NewOS\\SystemLibrary\\"
#define NS_PATH_UI          "C:\\NewOS\\SystemLibrary\\UI\\"
#define NS_PATH_SDK         "C:\\NewOS\\SDK\\"
#define NS_PATH_PROGRAMFILE "C:\\ProgramFile\\"
#define NS_PATH_USER        "C:\\User\\"

#define NS_ATTR_READONLY    0x01
#define NS_ATTR_HIDDEN      0x02
#define NS_ATTR_SYSTEM      0x04
#define NS_ATTR_DIRECTORY   0x10
#define NS_ATTR_ARCHIVE     0x20

#define NS_PERM_READ        0x01
#define NS_PERM_WRITE       0x02
#define NS_PERM_EXEC        0x04

#endif /* NEWOS_SDK_FS_H */
