/**
 * newos/ipc.h — Namespace IPC, Eventi Finestra e Appunti Globali (Clipboard)
 */
#ifndef NEWOS_SDK_IPC_H
#define NEWOS_SDK_IPC_H

#include "types.h"

typedef enum {
    NS_EV_NONE  = 0,
    NS_EV_PAINT = 1,
    NS_EV_MOUSE = 2,
    NS_EV_KEY   = 3,
    NS_EV_TIMER = 4,
    NS_EV_IPC   = 5,
    NS_EV_CLOSE = 6
} ns_event_type_t;

typedef struct {
    ns_event_type_t type;
    uint32_t        sender_pid;
    int32_t         arg1;
    int32_t         arg2;
    void*           payload;
    size_t          payload_size;
} ns_ipc_msg_t;

#endif /* NEWOS_SDK_IPC_H */
