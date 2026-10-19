#ifndef NET_TLS_H
#define NET_TLS_H

#include <types.h>

#define TLS_TYPE_CHANGE_CIPHER_SPEC 20
#define TLS_TYPE_ALERT              21
#define TLS_TYPE_HANDSHAKE          22
#define TLS_TYPE_APPLICATION_DATA   23

#define TLS_VERSION_1_2             0x0303
#define TLS_VERSION_1_3             0x0304

typedef struct __attribute__((packed)) {
    uint8_t  content_type;
    uint16_t version;
    uint16_t length;
} tls_record_header_t;

typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t  buffer[64];
} sha256_ctx_t;

// Inizializza il layer crittografico TLS
void tls_init(void);

// Calcolo hash SHA-256 su un blocco di dati
void sha256_hash(const void* data, size_t len, uint8_t hash_out[32]);

// Calcolo HMAC-SHA256 (RFC 2104)
void hmac_sha256(const void* key, size_t key_len, const void* data, size_t data_len, uint8_t mac_out[32]);

// Generatore numeri pseudo-casuali crittografici
void tls_get_random(uint8_t* out, size_t len);

// Incapsula payload in un record TLS 1.2/1.3
size_t tls_wrap_record(uint8_t content_type, const void* payload, uint16_t payload_len, uint8_t* out_buf, size_t max_out);

// Decapsula un record TLS verificando header e integrità
bool tls_unwrap_record(const uint8_t* in_buf, size_t in_len, uint8_t* content_type, const uint8_t** payload_out, uint16_t* payload_len_out);

#endif // NET_TLS_H
