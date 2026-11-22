#ifndef KERNEL_LAUNCHER_H
#define KERNEL_LAUNCHER_H

#include <types.h>

typedef enum {
    LAUNCH_TYPE_AUTO = 0,
    LAUNCH_TYPE_APP,
    LAUNCH_TYPE_FILE,
    LAUNCH_TYPE_FOLDER,
    LAUNCH_TYPE_URL,
    LAUNCH_TYPE_MODULE
} launcher_type_t;

#define LAUNCHER_CMD_OPEN 1

typedef struct {
    uint32_t    cmd;
    uint32_t    type;
    const char* target;
    const char* args;
    const char* content_hint;
    int         result;
    char        resolved_handler[64];
} launcher_params_t;

void launcher_init(void);
bool launcher_open(const char* target, const char* args);
bool launcher_open_typed(int type, const char* target, const char* args, const char* content_hint);
void launcher_get_last_handler(char* out, size_t max_len);

#endif // KERNEL_LAUNCHER_H
