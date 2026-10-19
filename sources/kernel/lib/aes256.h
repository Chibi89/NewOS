#ifndef KERNEL_AES256_H
#define KERNEL_AES256_H

#include <types.h>

#define AES256_KEY_BYTES    32
#define AES256_BLOCK_BYTES  16
#define AES256_ROUNDS       14

typedef struct {
    uint32_t round_keys[60]; // 15 round keys x 4 words = 60 words
} aes256_ctx_t;

// Inizializza il contesto espandendo la chiave a 256 bit per la decifratura
void aes256_init_decrypt(aes256_ctx_t* ctx, const uint8_t key[AES256_KEY_BYTES]);

// Decifra un singolo blocco di 16 byte (Rijndael-256 inverse cipher)
void aes256_decrypt_block(const aes256_ctx_t* ctx, const uint8_t in[AES256_BLOCK_BYTES], uint8_t out[AES256_BLOCK_BYTES]);

// Decifra un buffer di dimensione multipla di 16 byte in modalita' AES-256-CBC
void aes256_cbc_decrypt(const aes256_ctx_t* ctx, const uint8_t iv[AES256_BLOCK_BYTES], const uint8_t* in, uint8_t* out, size_t length);

#endif // KERNEL_AES256_H
