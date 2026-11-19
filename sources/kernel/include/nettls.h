#ifndef NETTLS_H
#define NETTLS_H

#include <types.h>

typedef enum {
    NETTLS_CMD_INIT     = 0,
    NETTLS_CMD_HTTP_GET = 1
} nettls_cmd_t;

typedef struct {
    int32_t  status_code;
    bool     is_https;
    bool     tls_active;
    char     final_url[512];
    char     content_type[64];
    char    *body;
    uint32_t body_len;
    uint32_t max_body_bytes;
    char     error_msg[128];
} nettls_response_t;

typedef struct {
    nettls_cmd_t       cmd;
    const char        *url;
    nettls_response_t *resp;
} nettls_params_t;

#endif // NETTLS_H

