#ifndef NET_NETTLS_LOADER_H
#define NET_NETTLS_LOADER_H

#include <nettls.h>
#include <types.h>

// Inizializza e carica C:\NewOS\SystemLibrary\NETTLS.DLL
bool nettls_init(void);

// Esegue una richiesta HTTP/1.1 o HTTPS (TLS 1.3) con redirect, chunked e gzip
bool nettls_http_get(const char *url, uint32_t max_body_bytes, nettls_response_t *out_resp);

// Libera il buffer body allocato da nettls_http_get
void nettls_free_response(nettls_response_t *resp);

#endif // NET_NETTLS_LOADER_H

