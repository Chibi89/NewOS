#include <net/tls.h>
#include <drivers/timer.h>
#include <drivers/net/net_nic.h>
#include <string.h>

#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define SIG0(x) (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define SIG1(x) (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define TH0(x)  (ROTR(x, 7) ^ ROTR(x, 18) ^ ((x) >> 3))
#define TH1(x)  (ROTR(x, 17) ^ ROTR(x, 19) ^ ((x) >> 10))

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static void sha256_transform(sha256_ctx_t* ctx, const uint8_t data[64]) {
    uint32_t W[64];
    for (int i = 0; i < 16; i++) {
        W[i] = ((uint32_t)data[i * 4] << 24) |
               ((uint32_t)data[i * 4 + 1] << 16) |
               ((uint32_t)data[i * 4 + 2] << 8) |
               ((uint32_t)data[i * 4 + 3]);
    }
    for (int i = 16; i < 64; i++) {
        W[i] = TH1(W[i - 2]) + W[i - 7] + TH0(W[i - 15]) + W[i - 16];
    }

    uint32_t a = ctx->state[0];
    uint32_t b = ctx->state[1];
    uint32_t c = ctx->state[2];
    uint32_t d = ctx->state[3];
    uint32_t e = ctx->state[4];
    uint32_t f = ctx->state[5];
    uint32_t g = ctx->state[6];
    uint32_t h = ctx->state[7];

    for (int i = 0; i < 64; i++) {
        uint32_t T1 = h + SIG1(e) + CH(e, f, g) + K[i] + W[i];
        uint32_t T2 = SIG0(a) + MAJ(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + T1;
        d = c;
        c = b;
        b = a;
        a = T1 + T2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void sha256_init_ctx(sha256_ctx_t* ctx) {
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
    ctx->count = 0;
}

static void sha256_update(sha256_ctx_t* ctx, const void* data, size_t len) {
    const uint8_t* p = (const uint8_t*)data;
    size_t buffer_fill = (size_t)(ctx->count & 0x3F);
    ctx->count += len;

    if (buffer_fill > 0) {
        size_t need = 64 - buffer_fill;
        if (len < need) {
            memcpy(ctx->buffer + buffer_fill, p, len);
            return;
        }
        memcpy(ctx->buffer + buffer_fill, p, need);
        sha256_transform(ctx, ctx->buffer);
        p += need;
        len -= need;
    }

    while (len >= 64) {
        sha256_transform(ctx, p);
        p += 64;
        len -= 64;
    }

    if (len > 0) {
        memcpy(ctx->buffer, p, len);
    }
}

static void sha256_final(sha256_ctx_t* ctx, uint8_t hash[32]) {
    uint8_t pad[64] = {0x80};
    uint64_t bit_len = ctx->count * 8;
    size_t buffer_fill = (size_t)(ctx->count & 0x3F);
    size_t pad_len = (buffer_fill < 56) ? (56 - buffer_fill) : (120 - buffer_fill);

    sha256_update(ctx, pad, pad_len);

    uint8_t len_bytes[8];
    for (int i = 0; i < 8; i++) {
        len_bytes[7 - i] = (uint8_t)(bit_len >> (i * 8));
    }
    sha256_update(ctx, len_bytes, 8);

    for (int i = 0; i < 8; i++) {
        hash[i * 4]     = (uint8_t)(ctx->state[i] >> 24);
        hash[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
        hash[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 8);
        hash[i * 4 + 3] = (uint8_t)(ctx->state[i]);
    }
}

void sha256_hash(const void* data, size_t len, uint8_t hash_out[32]) {
    sha256_ctx_t ctx;
    sha256_init_ctx(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, hash_out);
}

void hmac_sha256(const void* key, size_t key_len, const void* data, size_t data_len, uint8_t mac_out[32]) {
    uint8_t k_pad[64];
    memset(k_pad, 0, 64);

    if (key_len > 64) {
        sha256_hash(key, key_len, k_pad);
    } else {
        memcpy(k_pad, key, key_len);
    }

    uint8_t ipad[64];
    uint8_t opad[64];
    for (int i = 0; i < 64; i++) {
        ipad[i] = k_pad[i] ^ 0x36;
        opad[i] = k_pad[i] ^ 0x5C;
    }

    uint8_t inner_hash[32];
    sha256_ctx_t ctx;

    // Inner hash: SHA256(ipad || data)
    sha256_init_ctx(&ctx);
    sha256_update(&ctx, ipad, 64);
    sha256_update(&ctx, data, data_len);
    sha256_final(&ctx, inner_hash);

    // Outer hash: SHA256(opad || inner_hash)
    sha256_init_ctx(&ctx);
    sha256_update(&ctx, opad, 64);
    sha256_update(&ctx, inner_hash, 32);
    sha256_final(&ctx, mac_out);
}

static uint32_t g_prng_seed = 0x9E3779B9;

void tls_init(void) {
    g_prng_seed ^= (uint32_t)timer_get_ticks();
}

void tls_get_random(uint8_t* out, size_t len) {
    if (!out) return;
    for (size_t i = 0; i < len; i++) {
        g_prng_seed = g_prng_seed * 1103515245 + 12345;
        out[i] = (uint8_t)((g_prng_seed >> 16) & 0xFF);
    }
}

size_t tls_wrap_record(uint8_t content_type, const void* payload, uint16_t payload_len, uint8_t* out_buf, size_t max_out) {
    if (!payload || !out_buf || max_out < (size_t)(sizeof(tls_record_header_t) + payload_len)) {
        return 0;
    }

    tls_record_header_t* hdr = (tls_record_header_t*)out_buf;
    hdr->content_type = content_type;
    hdr->version = net_htons(TLS_VERSION_1_2);
    hdr->length = net_htons(payload_len);

    memcpy(out_buf + sizeof(tls_record_header_t), payload, payload_len);
    return sizeof(tls_record_header_t) + payload_len;
}

bool tls_unwrap_record(const uint8_t* in_buf, size_t in_len, uint8_t* content_type, const uint8_t** payload_out, uint16_t* payload_len_out) {
    if (!in_buf || in_len < sizeof(tls_record_header_t)) return false;

    const tls_record_header_t* hdr = (const tls_record_header_t*)in_buf;
    uint16_t rec_len = net_ntohs(hdr->length);

    if (in_len < (size_t)(sizeof(tls_record_header_t) + rec_len)) return false;

    if (content_type) *content_type = hdr->content_type;
    if (payload_out) *payload_out = in_buf + sizeof(tls_record_header_t);
    if (payload_len_out) *payload_len_out = rec_len;

    return true;
}
