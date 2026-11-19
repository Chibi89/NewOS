#include <nettls.h>
#include <screen_modules.h>

__attribute__((section(".header"), used)) module_header_t g_nettls_header = {
    .magic = MODULE_MAGIC_NEWOS,
    .header_size = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type = MODULE_TYPE_SYSTEM_LIBRARY,
    .code_size = 0,
    .entry_offset = 64,
    .checksum = 0,
    .security_flags =
        SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name = "NETTLS",
    .signature = MODULE_AUTH_TAG};

static int nettls_dispatch(const kernel_api_t *api, nettls_params_t *p);

// Entry point garantito all'offset 64 (subito dopo .header)
int module_main(const kernel_api_t *api, nettls_params_t *p) {
  return nettls_dispatch(api, p);
}

// ==============================================================================
// UTILITA' BASE FREESTANDING
// ==============================================================================
static void *n_memcpy(void *dst, const void *src, size_t n) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  for (size_t i = 0; i < n; i++) d[i] = s[i];
  return dst;
}

static void *n_memset(void *dst, int v, size_t n) {
  uint8_t *d = (uint8_t *)dst;
  for (size_t i = 0; i < n; i++) d[i] = (uint8_t)v;
  return dst;
}

static size_t n_strlen(const char *s) {
  size_t l = 0;
  while (s && s[l]) l++;
  return l;
}

static void n_strcpy(char *dst, const char *src, size_t max_len) {
  if (!dst || max_len == 0) return;
  size_t i = 0;
  while (src && src[i] && i + 1 < max_len) {
    dst[i] = src[i];
    i++;
  }
  dst[i] = '\0';
}

static int n_tolower(int c) {
  return (c >= 'A' && c <= 'Z') ? (c + 32) : c;
}

static int n_strncasecmp(const char *a, const char *b, size_t n) {
  for (size_t i = 0; i < n; i++) {
    int ca = n_tolower((uint8_t)a[i]);
    int cb = n_tolower((uint8_t)b[i]);
    if (ca != cb) return ca - cb;
    if (ca == 0) return 0;
  }
  return 0;
}

static const char *n_strcasestr(const char *hay, const char *needle) {
  size_t nl = n_strlen(needle);
  if (nl == 0) return hay;
  for (size_t i = 0; hay[i]; i++) {
    if (n_strncasecmp(hay + i, needle, nl) == 0)
      return hay + i;
  }
  return NULL;
}

// ==============================================================================
// COOKIE JAR IN MEMORIA (Set-Cookie / Cookie per sessione HTTP/HTTPS)
// ==============================================================================
#define MAX_COOKIES 16
typedef struct {
  bool used;
  char domain[64];
  char name[48];
  char value[128];
} nettls_cookie_t;

static nettls_cookie_t g_cookies[MAX_COOKIES];

static bool cookie_domain_match(const char *host, const char *dom) {
  size_t hl = n_strlen(host), dl = n_strlen(dom);
  if (hl == 0 || dl == 0) return false;
  if (dom[0] == '.') { dom++; dl--; }
  if (hl < dl) return false;
  if (n_strncasecmp(host + (hl - dl), dom, dl) != 0) return false;
  return (hl == dl || host[hl - dl - 1] == '.');
}

static void cookie_store_one(const char *host, const char *name, const char *val) {
  if (!name[0]) return;
  int free_idx = -1;
  for (int i = 0; i < MAX_COOKIES; i++) {
    if (g_cookies[i].used && cookie_domain_match(host, g_cookies[i].domain) &&
        n_strncasecmp(g_cookies[i].name, name, sizeof(g_cookies[i].name)) == 0) {
      n_strcpy(g_cookies[i].value, val, sizeof(g_cookies[i].value));
      return;
    }
    if (!g_cookies[i].used && free_idx < 0) free_idx = i;
  }
  if (free_idx >= 0) {
    g_cookies[free_idx].used = true;
    n_strcpy(g_cookies[free_idx].domain, host, sizeof(g_cookies[free_idx].domain));
    n_strcpy(g_cookies[free_idx].name, name, sizeof(g_cookies[free_idx].name));
    n_strcpy(g_cookies[free_idx].value, val, sizeof(g_cookies[free_idx].value));
  }
}

static void cookie_parse_headers(const char *hdr, const char *host) {
  const char *p = hdr;
  while (p && *p) {
    const char *sc = n_strcasestr(p, "Set-Cookie:");
    if (!sc) break;
    sc += 11;
    while (*sc == ' ' || *sc == '\t') sc++;
    char cname[48], cval[128];
    size_t ni = 0, vi = 0;
    while (*sc && *sc != '=' && *sc != '\r' && *sc != '\n' && ni + 1 < sizeof(cname))
      cname[ni++] = *sc++;
    cname[ni] = '\0';
    if (*sc == '=') {
      sc++;
      while (*sc && *sc != ';' && *sc != '\r' && *sc != '\n' && vi + 1 < sizeof(cval))
        cval[vi++] = *sc++;
    }
    cval[vi] = '\0';
    if (ni > 0) cookie_store_one(host, cname, cval);
    while (*sc && *sc != '\n') sc++;
    if (*sc == '\n') sc++;
    p = sc;
  }
}

static size_t cookie_append_header(const char *host, char *dst, size_t max_dst) {
  size_t pos = 0;
  bool any = false;
  for (int i = 0; i < MAX_COOKIES; i++) {
    if (!g_cookies[i].used || !cookie_domain_match(host, g_cookies[i].domain))
      continue;
    if (!any) {
      const char *pfx = "Cookie: ";
      for (size_t k = 0; pfx[k] && pos + 1 < max_dst; k++) dst[pos++] = pfx[k];
      any = true;
    } else if (pos + 2 < max_dst) {
      dst[pos++] = ';';
      dst[pos++] = ' ';
    }
    for (size_t k = 0; g_cookies[i].name[k] && pos + 1 < max_dst; k++) dst[pos++] = g_cookies[i].name[k];
    if (pos + 1 < max_dst) dst[pos++] = '=';
    for (size_t k = 0; g_cookies[i].value[k] && pos + 1 < max_dst; k++) dst[pos++] = g_cookies[i].value[k];
  }
  if (any && pos + 2 < max_dst) {
    dst[pos++] = '\r';
    dst[pos++] = '\n';
  }
  dst[pos] = '\0';
  return pos;
}

static uint16_t rd_be16(const uint8_t *p) {
  return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t rd_be24(const uint8_t *p) {
  return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}

static void wr_be16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)(v >> 8);
  p[1] = (uint8_t)(v & 0xFF);
}

static void wr_be24(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)((v >> 16) & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
  p[2] = (uint8_t)(v & 0xFF);
}

static uint16_t n_htons(uint16_t v) {
  return (uint16_t)((v >> 8) | (v << 8));
}

static uint64_t g_prng_state = 0x9E3779B97F4A7C15ULL;
static void n_random_bytes(uint8_t *out, size_t len) {
  uint32_t lo, hi;
  __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
  g_prng_state ^= ((uint64_t)hi << 32) | lo;
  for (size_t i = 0; i < len; i++) {
    g_prng_state ^= g_prng_state << 13;
    g_prng_state ^= g_prng_state >> 7;
    g_prng_state ^= g_prng_state << 17;
    out[i] = (uint8_t)(g_prng_state >> 24);
  }
}

// ==============================================================================
// SHA-256 & HMAC-SHA256 & HKDF (RFC 5869 / TLS 1.3)
// ==============================================================================
typedef struct {
  uint32_t state[8];
  uint64_t bitlen;
  uint8_t  data[64];
  size_t   datalen;
} sha256_ctx_t;

static const uint32_t K256[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

#define ROR32(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha256_transform(sha256_ctx_t *ctx, const uint8_t d[64]) {
  uint32_t m[64], a, b, c, dd, e, f, g, h, t1, t2;
  for (int i = 0, j = 0; i < 16; i++, j += 4)
    m[i] = ((uint32_t)d[j] << 24) | ((uint32_t)d[j + 1] << 16) | ((uint32_t)d[j + 2] << 8) | d[j + 3];
  for (int i = 16; i < 64; i++) {
    uint32_t s0 = ROR32(m[i - 15], 7) ^ ROR32(m[i - 15], 18) ^ (m[i - 15] >> 3);
    uint32_t s1 = ROR32(m[i - 2], 17) ^ ROR32(m[i - 2], 19) ^ (m[i - 2] >> 10);
    m[i] = m[i - 16] + s0 + m[i - 7] + s1;
  }
  a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; dd = ctx->state[3];
  e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];
  for (int i = 0; i < 64; i++) {
    uint32_t S1 = ROR32(e, 6) ^ ROR32(e, 11) ^ ROR32(e, 25);
    uint32_t ch = (e & f) ^ ((~e) & g);
    t1 = h + S1 + ch + K256[i] + m[i];
    uint32_t S0 = ROR32(a, 2) ^ ROR32(a, 13) ^ ROR32(a, 22);
    uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    t2 = S0 + maj;
    h = g; g = f; f = e; e = dd + t1; dd = c; c = b; b = a; a = t1 + t2;
  }
  ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += dd;
  ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

static void sha256_init(sha256_ctx_t *ctx) {
  ctx->datalen = 0;
  ctx->bitlen = 0;
  ctx->state[0] = 0x6a09e667; ctx->state[1] = 0xbb67ae85;
  ctx->state[2] = 0x3c6ef372; ctx->state[3] = 0xa54ff53a;
  ctx->state[4] = 0x510e527f; ctx->state[5] = 0x9b05688c;
  ctx->state[6] = 0x1f83d9ab; ctx->state[7] = 0x5be0cd19;
}

static void sha256_update(sha256_ctx_t *ctx, const void *data, size_t len) {
  const uint8_t *p = (const uint8_t *)data;
  for (size_t i = 0; i < len; i++) {
    ctx->data[ctx->datalen++] = p[i];
    if (ctx->datalen == 64) {
      sha256_transform(ctx, ctx->data);
      ctx->bitlen += 512;
      ctx->datalen = 0;
    }
  }
}

static void sha256_final(sha256_ctx_t *ctx, uint8_t hash[32]) {
  size_t i = ctx->datalen;
  if (ctx->datalen < 56) {
    ctx->data[i++] = 0x80;
    while (i < 56) ctx->data[i++] = 0;
  } else {
    ctx->data[i++] = 0x80;
    while (i < 64) ctx->data[i++] = 0;
    sha256_transform(ctx, ctx->data);
    n_memset(ctx->data, 0, 56);
  }
  ctx->bitlen += ctx->datalen * 8;
  for (int j = 0; j < 8; j++)
    ctx->data[63 - j] = (uint8_t)((ctx->bitlen >> (j * 8)) & 0xFF);
  sha256_transform(ctx, ctx->data);
  for (int j = 0; j < 8; j++) {
    hash[j * 4 + 0] = (uint8_t)((ctx->state[j] >> 24) & 0xFF);
    hash[j * 4 + 1] = (uint8_t)((ctx->state[j] >> 16) & 0xFF);
    hash[j * 4 + 2] = (uint8_t)((ctx->state[j] >> 8) & 0xFF);
    hash[j * 4 + 3] = (uint8_t)(ctx->state[j] & 0xFF);
  }
}

static void sha256_oneshot(const void *data, size_t len, uint8_t hash[32]) {
  sha256_ctx_t c;
  sha256_init(&c);
  sha256_update(&c, data, len);
  sha256_final(&c, hash);
}

static void hmac_sha256(const uint8_t *key, size_t key_len, const uint8_t *data, size_t data_len, uint8_t out[32]) {
  uint8_t k[64], ipad[64], opad[64], inner[32];
  n_memset(k, 0, 64);
  if (key_len > 64) sha256_oneshot(key, key_len, k);
  else n_memcpy(k, key, key_len);
  for (int i = 0; i < 64; i++) {
    ipad[i] = k[i] ^ 0x36;
    opad[i] = k[i] ^ 0x5C;
  }
  sha256_ctx_t c;
  sha256_init(&c);
  sha256_update(&c, ipad, 64);
  sha256_update(&c, data, data_len);
  sha256_final(&c, inner);

  sha256_init(&c);
  sha256_update(&c, opad, 64);
  sha256_update(&c, inner, 32);
  sha256_final(&c, out);
}

static void hkdf_extract(const uint8_t *salt, size_t salt_len, const uint8_t *ikm, size_t ikm_len, uint8_t prk[32]) {
  uint8_t zero_salt[32];
  if (!salt || salt_len == 0) {
    n_memset(zero_salt, 0, 32);
    salt = zero_salt;
    salt_len = 32;
  }
  hmac_sha256(salt, salt_len, ikm, ikm_len, prk);
}

static void hkdf_expand_label(const uint8_t secret[32], const char *label,
                              const uint8_t *ctx, size_t ctx_len,
                              uint8_t *out, size_t out_len) {
  uint8_t info[128];
  size_t ll = n_strlen(label);
  size_t pos = 0;
  wr_be16(info + pos, (uint16_t)out_len); pos += 2;
  info[pos++] = (uint8_t)(6 + ll);
  n_memcpy(info + pos, "tls13 ", 6); pos += 6;
  n_memcpy(info + pos, label, ll); pos += ll;
  info[pos++] = (uint8_t)ctx_len;
  if (ctx_len > 0 && ctx) {
    n_memcpy(info + pos, ctx, ctx_len);
    pos += ctx_len;
  }
  info[pos++] = 0x01; // counter = 1 (sufficiente per <= 32 byte)
  uint8_t okm[32];
  hmac_sha256(secret, 32, info, pos, okm);
  n_memcpy(out, okm, out_len);
}

// ==============================================================================
// X25519 (Curve25519 ECDHE - TweetNaCl compact field arithmetic)
// ==============================================================================
typedef int64_t gf[16];

static void gf_set25519(gf r, const gf a) {
  for (int i = 0; i < 16; i++) r[i] = a[i];
}

static void gf_car(gf o) {
  int64_t c;
  for (int i = 0; i < 16; i++) {
    o[i] += (1LL << 16);
    c = o[i] >> 16;
    o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
    o[i] -= c << 16;
  }
}

static void gf_sel(gf p, gf q, int b) {
  int64_t t, c = ~(b - 1);
  for (int i = 0; i < 16; i++) {
    t = c & (p[i] ^ q[i]);
    p[i] ^= t;
    q[i] ^= t;
  }
}

static void gf_add(gf o, const gf a, const gf b) {
  for (int i = 0; i < 16; i++) o[i] = a[i] + b[i];
}

static void gf_sub(gf o, const gf a, const gf b) {
  for (int i = 0; i < 16; i++) o[i] = a[i] - b[i];
}

static void gf_mul(gf o, const gf a, const gf b) {
  int64_t t[31];
  for (int i = 0; i < 31; i++) t[i] = 0;
  for (int i = 0; i < 16; i++)
    for (int j = 0; j < 16; j++)
      t[i + j] += a[i] * b[j];
  for (int i = 0; i < 15; i++) t[i] += 38 * t[i + 16];
  for (int i = 0; i < 16; i++) o[i] = t[i];
  gf_car(o);
  gf_car(o);
}

static void gf_sq(gf o, const gf a) { gf_mul(o, a, a); }

static void gf_inv(gf o, const gf i) {
  gf c;
  gf_set25519(c, i);
  for (int a = 253; a >= 0; a--) {
    gf_sq(c, c);
    if (a != 2 && a != 4) gf_mul(c, c, i);
  }
  gf_set25519(o, c);
}

static void gf_pack(uint8_t *o, const gf n) {
  int b;
  gf m, t;
  gf_set25519(t, n);
  gf_car(t); gf_car(t); gf_car(t);
  for (int j = 0; j < 2; j++) {
    m[0] = t[0] - 0xffed;
    for (int i = 1; i < 15; i++) {
      m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
      m[i - 1] &= 0xffff;
    }
    m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
    b = (int)((m[15] >> 16) & 1);
    m[14] &= 0xffff;
    gf_sel(t, m, 1 - b);
  }
  for (int i = 0; i < 16; i++) {
    o[2 * i] = (uint8_t)(t[i] & 0xff);
    o[2 * i + 1] = (uint8_t)(t[i] >> 8);
  }
}

static void gf_unpack(gf o, const uint8_t *n) {
  for (int i = 0; i < 16; i++)
    o[i] = (int64_t)n[2 * i] + ((int64_t)n[2 * i + 1] << 8);
  o[15] &= 0x7fff;
}

static void x25519_scalarmult(uint8_t q[32], const uint8_t n[32], const uint8_t p[32]) {
  uint8_t z[32];
  int64_t r;
  gf x, a, b, c, d, e, f;
  static const gf c121665 = {0xDB41, 1, 0,0,0,0,0,0,0,0,0,0,0,0,0,0};
  for (int i = 0; i < 31; i++) z[i] = n[i];
  z[31] = (n[31] & 127) | 64;
  z[0] &= 248;
  gf_unpack(x, p);
  for (int i = 0; i < 16; i++) {
    b[i] = x[i];
    a[i] = c[i] = d[i] = 0;
  }
  a[0] = d[0] = 1;
  for (int i = 254; i >= 0; --i) {
    r = (z[i >> 3] >> (i & 7)) & 1;
    gf_sel(a, b, (int)r);
    gf_sel(c, d, (int)r);
    gf_add(e, a, c);
    gf_sub(a, a, c);
    gf_add(c, b, d);
    gf_sub(b, b, d);
    gf_sq(d, e);
    gf_sq(f, a);
    gf_mul(a, c, a);
    gf_mul(c, b, e);
    gf_add(e, a, c);
    gf_sub(a, a, c);
    gf_sq(b, a);
    gf_sub(c, d, f);
    gf_mul(a, c, c121665);
    gf_add(a, a, d);
    gf_mul(c, c, a);
    gf_mul(a, d, f);
    gf_mul(d, b, x);
    gf_sq(b, e);
    gf_sel(a, b, (int)r);
    gf_sel(c, d, (int)r);
  }
  gf_inv(c, c);
  gf_mul(a, a, c);
  gf_pack(q, a);
}

static void x25519_base(uint8_t pub[32], const uint8_t priv[32]) {
  uint8_t base[32];
  n_memset(base, 0, 32);
  base[0] = 9;
  x25519_scalarmult(pub, priv, base);
}

// ==============================================================================
// CHACHA20-POLY1305 AEAD (RFC 8439)
// ==============================================================================
#define ROTL32(v, n) (((v) << (n)) | ((v) >> (32 - (n))))
#define QR(a, b, c, d) do { \
  a += b; d ^= a; d = ROTL32(d, 16); \
  c += d; b ^= c; b = ROTL32(b, 12); \
  a += b; d ^= a; d = ROTL32(d, 8);  \
  c += d; b ^= c; b = ROTL32(b, 7);  \
} while (0)

static uint32_t ld_le32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void st_le32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static void chacha20_block(const uint8_t key[32], uint32_t counter, const uint8_t nonce[12], uint8_t out[64]) {
  uint32_t st[16], x[16];
  st[0] = 0x61707865; st[1] = 0x3320646e; st[2] = 0x79622d32; st[3] = 0x6b206574;
  for (int i = 0; i < 8; i++) st[4 + i] = ld_le32(key + i * 4);
  st[12] = counter;
  for (int i = 0; i < 3; i++) st[13 + i] = ld_le32(nonce + i * 4);
  for (int i = 0; i < 16; i++) x[i] = st[i];
  for (int i = 0; i < 10; i++) {
    QR(x[0], x[4], x[8],  x[12]); QR(x[1], x[5], x[9],  x[13]);
    QR(x[2], x[6], x[10], x[14]); QR(x[3], x[7], x[11], x[15]);
    QR(x[0], x[5], x[10], x[15]); QR(x[1], x[6], x[11], x[12]);
    QR(x[2], x[7], x[8],  x[13]); QR(x[3], x[4], x[9],  x[14]);
  }
  for (int i = 0; i < 16; i++) st_le32(out + i * 4, x[i] + st[i]);
}

static void chacha20_xor(const uint8_t key[32], uint32_t counter, const uint8_t nonce[12],
                         const uint8_t *in, uint8_t *out, size_t len) {
  uint8_t block[64];
  size_t pos = 0;
  while (pos < len) {
    chacha20_block(key, counter++, nonce, block);
    size_t chunk = (len - pos < 64) ? (len - pos) : 64;
    for (size_t i = 0; i < chunk; i++) out[pos + i] = in[pos + i] ^ block[i];
    pos += chunk;
  }
}

static void poly1305_mac(const uint8_t key[32], const uint8_t *aad, size_t aad_len,
                         const uint8_t *ct, size_t ct_len, uint8_t tag[16]) {
  uint32_t r0, r1, r2, r3, r4;
  uint32_t s1, s2, s3, s4;
  uint32_t h0 = 0, h1 = 0, h2 = 0, h3 = 0, h4 = 0;

  r0 = (ld_le32(key + 0)) & 0x3ffffff;
  r1 = (ld_le32(key + 3) >> 2) & 0x3ffff03;
  r2 = (ld_le32(key + 6) >> 4) & 0x3ffc0ff;
  r3 = (ld_le32(key + 9) >> 6) & 0x3f03fff;
  r4 = (ld_le32(key + 12) >> 8) & 0x00fffff;

  s1 = r1 * 5; s2 = r2 * 5; s3 = r3 * 5; s4 = r4 * 5;

  // Macro per processare un blocco da <= 16 byte (in AEAD RFC 8439 aad e ct sono paddati con 0 a 16 byte -> pad_at_16=1)
  #define POLY_BLOCK(ptr, blen, pad_at_16) do { \
    uint8_t buf[17]; \
    n_memset(buf, 0, 17); \
    n_memcpy(buf, ptr, blen); \
    buf[(pad_at_16) ? 16 : (blen)] = 1; \
    uint32_t t0 = ld_le32(buf + 0); \
    uint32_t t1 = ld_le32(buf + 4); \
    uint32_t t2 = ld_le32(buf + 8); \
    uint32_t t3 = ld_le32(buf + 12); \
    h0 += t0 & 0x3ffffff; \
    h1 += ((t0 >> 26) | (t1 << 6)) & 0x3ffffff; \
    h2 += ((t1 >> 20) | (t2 << 12)) & 0x3ffffff; \
    h3 += ((t2 >> 14) | (t3 << 18)) & 0x3ffffff; \
    h4 += (t3 >> 8) | ((uint32_t)buf[16] << 24); \
    uint64_t d0 = (uint64_t)h0*r0 + (uint64_t)h1*s4 + (uint64_t)h2*s3 + (uint64_t)h3*s2 + (uint64_t)h4*s1; \
    uint64_t d1 = (uint64_t)h0*r1 + (uint64_t)h1*r0 + (uint64_t)h2*s4 + (uint64_t)h3*s3 + (uint64_t)h4*s2; \
    uint64_t d2 = (uint64_t)h0*r2 + (uint64_t)h1*r1 + (uint64_t)h2*r0 + (uint64_t)h3*s4 + (uint64_t)h4*s3; \
    uint64_t d3 = (uint64_t)h0*r3 + (uint64_t)h1*r2 + (uint64_t)h2*r1 + (uint64_t)h3*r0 + (uint64_t)h4*s4; \
    uint64_t d4 = (uint64_t)h0*r4 + (uint64_t)h1*r3 + (uint64_t)h2*r2 + (uint64_t)h3*r1 + (uint64_t)h4*r0; \
    uint32_t c; \
    c = (uint32_t)(d0 >> 26); h0 = (uint32_t)d0 & 0x3ffffff; d1 += c; \
    c = (uint32_t)(d1 >> 26); h1 = (uint32_t)d1 & 0x3ffffff; d2 += c; \
    c = (uint32_t)(d2 >> 26); h2 = (uint32_t)d2 & 0x3ffffff; d3 += c; \
    c = (uint32_t)(d3 >> 26); h3 = (uint32_t)d3 & 0x3ffffff; d4 += c; \
    c = (uint32_t)(d4 >> 26); h4 = (uint32_t)d4 & 0x3ffffff; h0 += c * 5; \
    c = h0 >> 26; h0 &= 0x3ffffff; h1 += c; \
  } while (0)

  size_t pos = 0;
  while (pos < aad_len) {
    size_t bl = (aad_len - pos < 16) ? (aad_len - pos) : 16;
    POLY_BLOCK(aad + pos, bl, 1);
    pos += bl;
  }
  pos = 0;
  while (pos < ct_len) {
    size_t bl = (ct_len - pos < 16) ? (ct_len - pos) : 16;
    POLY_BLOCK(ct + pos, bl, 1);
    pos += bl;
  }

  uint8_t lens[16];
  uint64_t al = (uint64_t)aad_len, cl = (uint64_t)ct_len;
  for (int i = 0; i < 8; i++) {
    lens[i] = (uint8_t)((al >> (i * 8)) & 0xFF);
    lens[8 + i] = (uint8_t)((cl >> (i * 8)) & 0xFF);
  }
  POLY_BLOCK(lens, 16, 1);
  #undef POLY_BLOCK

  uint32_t c = h1 >> 26; h1 &= 0x3ffffff; h2 += c;
  c = h2 >> 26; h2 &= 0x3ffffff; h3 += c;
  c = h3 >> 26; h3 &= 0x3ffffff; h4 += c;
  c = h4 >> 26; h4 &= 0x3ffffff; h0 += c * 5;
  c = h0 >> 26; h0 &= 0x3ffffff; h1 += c;

  uint32_t g0 = h0 + 5; c = g0 >> 26; g0 &= 0x3ffffff;
  uint32_t g1 = h1 + c; c = g1 >> 26; g1 &= 0x3ffffff;
  uint32_t g2 = h2 + c; c = g2 >> 26; g2 &= 0x3ffffff;
  uint32_t g3 = h3 + c; c = g3 >> 26; g3 &= 0x3ffffff;
  uint32_t g4 = h4 + c - (1U << 26);

  uint32_t mask = (g4 >> 31) - 1;
  g0 &= mask; g1 &= mask; g2 &= mask; g3 &= mask; g4 &= mask;
  mask = ~mask;
  h0 = (h0 & mask) | g0; h1 = (h1 & mask) | g1;
  h2 = (h2 & mask) | g2; h3 = (h3 & mask) | g3; h4 = (h4 & mask) | g4;

  uint32_t f0 = (h0) | (h1 << 26);
  uint32_t f1 = (h1 >> 6) | (h2 << 20);
  uint32_t f2 = (h2 >> 12) | (h3 << 14);
  uint32_t f3 = (h3 >> 18) | (h4 << 8);

  uint64_t f;
  f = (uint64_t)f0 + ld_le32(key + 16); st_le32(tag + 0, (uint32_t)f);
  f = (uint64_t)f1 + ld_le32(key + 20) + (f >> 32); st_le32(tag + 4, (uint32_t)f);
  f = (uint64_t)f2 + ld_le32(key + 24) + (f >> 32); st_le32(tag + 8, (uint32_t)f);
  f = (uint64_t)f3 + ld_le32(key + 28) + (f >> 32); st_le32(tag + 12, (uint32_t)f);
}

static void aead_chacha_encrypt(const uint8_t key[32], const uint8_t iv[12], uint64_t seq,
                                const uint8_t *aad, size_t aad_len,
                                const uint8_t *pt, size_t pt_len, uint8_t *ct_out, uint8_t tag[16]) {
  uint8_t nonce[12], otk[64];
  n_memcpy(nonce, iv, 12);
  for (int i = 0; i < 8; i++) nonce[11 - i] ^= (uint8_t)((seq >> (i * 8)) & 0xFF);
  chacha20_block(key, 0, nonce, otk);
  chacha20_xor(key, 1, nonce, pt, ct_out, pt_len);
  poly1305_mac(otk, aad, aad_len, ct_out, pt_len, tag);
}

static bool aead_chacha_decrypt(const uint8_t key[32], const uint8_t iv[12], uint64_t seq,
                                const uint8_t *aad, size_t aad_len,
                                const uint8_t *ct, size_t ct_len, const uint8_t tag[16], uint8_t *pt_out) {
  uint8_t nonce[12], otk[64], calc_tag[16];
  n_memcpy(nonce, iv, 12);
  for (int i = 0; i < 8; i++) nonce[11 - i] ^= (uint8_t)((seq >> (i * 8)) & 0xFF);
  chacha20_block(key, 0, nonce, otk);
  poly1305_mac(otk, aad, aad_len, ct, ct_len, calc_tag);
  uint8_t diff = 0;
  for (int i = 0; i < 16; i++) diff |= (calc_tag[i] ^ tag[i]);
  if (diff != 0) return false;
  chacha20_xor(key, 1, nonce, ct, pt_out, ct_len);
  return true;
}

// ==============================================================================
// AES-128-GCM AEAD (NIST SP 800-38D) per TLS_AES_128_GCM_SHA256 (0x1301)
// ==============================================================================
static const uint8_t aes_sbox[256] = {
  0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
  0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
  0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
  0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
  0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
  0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
  0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
  0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
  0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
  0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
  0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
  0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
  0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
  0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
  0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
  0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

static inline uint8_t gf_mul2(uint8_t x) {
  return (uint8_t)((x << 1) ^ ((x & 0x80) ? 0x1b : 0x00));
}

static void aes128_key_expand(const uint8_t key[16], uint8_t rk[176]) {
  static const uint8_t rcon[11] = {0x00,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36};
  n_memcpy(rk, key, 16);
  for (int i = 4; i < 44; i++) {
    uint8_t t[4];
    n_memcpy(t, rk + (i - 1) * 4, 4);
    if ((i % 4) == 0) {
      uint8_t tmp = t[0];
      t[0] = aes_sbox[t[1]] ^ rcon[i / 4];
      t[1] = aes_sbox[t[2]];
      t[2] = aes_sbox[t[3]];
      t[3] = aes_sbox[tmp];
    }
    for (int j = 0; j < 4; j++)
      rk[i * 4 + j] = rk[(i - 4) * 4 + j] ^ t[j];
  }
}

static void aes128_encrypt_block(const uint8_t rk[176], const uint8_t in[16], uint8_t out[16]) {
  uint8_t s[16];
  for (int i = 0; i < 16; i++) s[i] = in[i] ^ rk[i];
  for (int r = 1; r <= 10; r++) {
    for (int i = 0; i < 16; i++) s[i] = aes_sbox[s[i]];
    uint8_t t;
    t = s[1]; s[1] = s[5]; s[5] = s[9]; s[9] = s[13]; s[13] = t;
    t = s[2]; s[2] = s[10]; s[10] = t; t = s[6]; s[6] = s[14]; s[14] = t;
    t = s[15]; s[15] = s[11]; s[11] = s[7]; s[7] = s[3]; s[3] = t;
    if (r < 10) {
      for (int c = 0; c < 4; c++) {
        uint8_t *col = s + c * 4;
        uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
        uint8_t h = a0 ^ a1 ^ a2 ^ a3;
        col[0] ^= h ^ gf_mul2(a0 ^ a1);
        col[1] ^= h ^ gf_mul2(a1 ^ a2);
        col[2] ^= h ^ gf_mul2(a2 ^ a3);
        col[3] ^= h ^ gf_mul2(a3 ^ a0);
      }
    }
    const uint8_t *rkp = rk + r * 16;
    for (int i = 0; i < 16; i++) s[i] ^= rkp[i];
  }
  n_memcpy(out, s, 16);
}

static void ghash_mul(uint8_t x[16], const uint8_t y[16]) {
  uint8_t z[16], v[16];
  n_memset(z, 0, 16);
  n_memcpy(v, y, 16);
  for (int i = 0; i < 16; i++) {
    for (int b = 7; b >= 0; b--) {
      if ((x[i] >> b) & 1) {
        for (int k = 0; k < 16; k++) z[k] ^= v[k];
      }
      uint8_t lsb = v[15] & 1;
      for (int k = 15; k > 0; k--) v[k] = (uint8_t)((v[k] >> 1) | ((v[k - 1] & 1) << 7));
      v[0] >>= 1;
      if (lsb) v[0] ^= 0xe1;
    }
  }
  n_memcpy(x, z, 16);
}

static uint32_t rd_be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static void wr_be32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
  p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}
static void wr_be64(uint8_t *p, uint64_t v) {
  wr_be32(p, (uint32_t)(v >> 32));
  wr_be32(p + 4, (uint32_t)v);
}
static void build_tls13_nonce(const uint8_t iv[12], uint64_t seq, uint8_t nonce[12]) {
  n_memcpy(nonce, iv, 12);
  for (int i = 0; i < 8; i++) nonce[11 - i] ^= (uint8_t)((seq >> (i * 8)) & 0xFF);
}

static void ghash_compute(const uint8_t h[16], const uint8_t *aad, size_t aad_len,
                          const uint8_t *ct, size_t ct_len, uint8_t out[16]) {
  n_memset(out, 0, 16);
  for (size_t off = 0; off < aad_len; off += 16) {
    size_t n = (aad_len - off < 16) ? (aad_len - off) : 16;
    for (size_t i = 0; i < n; i++) out[i] ^= aad[off + i];
    ghash_mul(out, h);
  }
  for (size_t off = 0; off < ct_len; off += 16) {
    size_t n = (ct_len - off < 16) ? (ct_len - off) : 16;
    for (size_t i = 0; i < n; i++) out[i] ^= ct[off + i];
    ghash_mul(out, h);
  }
  uint8_t len_blk[16];
  wr_be64(len_blk, (uint64_t)aad_len * 8);
  wr_be64(len_blk + 8, (uint64_t)ct_len * 8);
  for (int i = 0; i < 16; i++) out[i] ^= len_blk[i];
  ghash_mul(out, h);
}

static void aes128_gcm_xor(const uint8_t rk[176], const uint8_t j0[16],
                           const uint8_t *in, uint8_t *out, size_t len) {
  uint8_t ctr[16], ks[16];
  n_memcpy(ctr, j0, 16);
  uint32_t c = rd_be32(ctr + 12);
  for (size_t off = 0; off < len; off += 16) {
    c++;
    wr_be32(ctr + 12, c);
    aes128_encrypt_block(rk, ctr, ks);
    size_t n = (len - off < 16) ? (len - off) : 16;
    for (size_t i = 0; i < n; i++) out[off + i] = in[off + i] ^ ks[i];
  }
}

static void aead_aes128_gcm_encrypt(const uint8_t key[16], const uint8_t iv[12], uint64_t seq,
                                    const uint8_t *aad, size_t aad_len,
                                    const uint8_t *pt, size_t pt_len,
                                    uint8_t *ct_out, uint8_t tag_out[16]) {
  uint8_t rk[176], zeros[16], h[16], j0[16], s[16], e0[16];
  aes128_key_expand(key, rk);
  n_memset(zeros, 0, 16);
  aes128_encrypt_block(rk, zeros, h);
  build_tls13_nonce(iv, seq, j0);
  j0[12] = 0; j0[13] = 0; j0[14] = 0; j0[15] = 1;
  aes128_gcm_xor(rk, j0, pt, ct_out, pt_len);
  ghash_compute(h, aad, aad_len, ct_out, pt_len, s);
  aes128_encrypt_block(rk, j0, e0);
  for (int i = 0; i < 16; i++) tag_out[i] = s[i] ^ e0[i];
}

static bool aead_aes128_gcm_decrypt(const uint8_t key[16], const uint8_t iv[12], uint64_t seq,
                                    const uint8_t *aad, size_t aad_len,
                                    const uint8_t *ct, size_t ct_len,
                                    const uint8_t tag[16], uint8_t *pt_out) {
  uint8_t rk[176], zeros[16], h[16], j0[16], s[16], e0[16], exp_tag[16];
  aes128_key_expand(key, rk);
  n_memset(zeros, 0, 16);
  aes128_encrypt_block(rk, zeros, h);
  build_tls13_nonce(iv, seq, j0);
  j0[12] = 0; j0[13] = 0; j0[14] = 0; j0[15] = 1;
  ghash_compute(h, aad, aad_len, ct, ct_len, s);
  aes128_encrypt_block(rk, j0, e0);
  uint8_t diff = 0;
  for (int i = 0; i < 16; i++) {
    exp_tag[i] = s[i] ^ e0[i];
    diff |= (uint8_t)(exp_tag[i] ^ tag[i]);
  }
  if (diff != 0) return false;
  aes128_gcm_xor(rk, j0, ct, pt_out, ct_len);
  return true;
}

// ==============================================================================
// DECOMPRESSORE INFLATE / GZIP (RFC 1951 & RFC 1952)
// ==============================================================================
typedef struct {
  const uint8_t *src;
  size_t         src_len;
  size_t         byte_pos;
  uint32_t       bit_buf;
  int            bit_cnt;
} bit_reader_t;

static uint32_t br_bits(bit_reader_t *br, int n) {
  while (br->bit_cnt < n) {
    uint8_t b = (br->byte_pos < br->src_len) ? br->src[br->byte_pos++] : 0;
    br->bit_buf |= ((uint32_t)b) << br->bit_cnt;
    br->bit_cnt += 8;
  }
  uint32_t v = br->bit_buf & ((1U << n) - 1U);
  br->bit_buf >>= n;
  br->bit_cnt -= n;
  return v;
}

typedef struct {
  int16_t counts[16];
  int16_t symbols[320];
} huff_t;

static void huff_build(huff_t *h, const uint8_t *lens, int num) {
  int16_t offs[16];
  for (int i = 0; i < 16; i++) h->counts[i] = 0;
  for (int i = 0; i < num; i++) h->counts[lens[i]]++;
  h->counts[0] = 0;
  int sum = 0;
  for (int l = 1; l <= 15; l++) {
    offs[l] = (int16_t)sum;
    sum += h->counts[l];
  }
  for (int i = 0; i < num; i++) {
    if (lens[i] != 0)
      h->symbols[offs[lens[i]]++] = (int16_t)i;
  }
}

static int huff_decode(bit_reader_t *br, const huff_t *h) {
  int code = 0, first = 0, index = 0;
  for (int len = 1; len <= 15; len++) {
    code |= (int)br_bits(br, 1);
    int count = h->counts[len];
    if (code - count < first)
      return h->symbols[index + (code - first)];
    index += count;
    first += count;
    first <<= 1;
    code <<= 1;
  }
  return -1;
}

static bool inflate_raw(const uint8_t *src, size_t src_len, uint8_t *dst, size_t max_dst, size_t *out_len) {
  static const uint16_t lbase[29] = {
      3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
  static const uint8_t lext[29] = {
      0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
  static const uint16_t dbase[30] = {
      1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,
      1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
  static const uint8_t dext[30] = {
      0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
  static const uint8_t cl_order[19] = {
      16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};

  bit_reader_t br = {src, src_len, 0, 0, 0};
  size_t dp = 0;
  int bfinal = 0;

  huff_t lit_h, dist_h, cl_h;
  uint8_t lens[320];

  do {
    bfinal = (int)br_bits(&br, 1);
    int btype = (int)br_bits(&br, 2);
    if (btype == 0) {
      br.bit_buf = 0; br.bit_cnt = 0;
      if (br.byte_pos + 4 > br.src_len) return false;
      uint16_t len = (uint16_t)(br.src[br.byte_pos] | (br.src[br.byte_pos + 1] << 8));
      br.byte_pos += 4;
      for (uint16_t i = 0; i < len && br.byte_pos < br.src_len; i++) {
        if (dp < max_dst) dst[dp++] = br.src[br.byte_pos];
        br.byte_pos++;
      }
    } else if (btype == 1 || btype == 2) {
      if (btype == 1) {
        int i = 0;
        for (; i <= 143; i++) lens[i] = 8;
        for (; i <= 255; i++) lens[i] = 9;
        for (; i <= 279; i++) lens[i] = 7;
        for (; i <= 287; i++) lens[i] = 8;
        huff_build(&lit_h, lens, 288);
        for (i = 0; i < 32; i++) lens[i] = 5;
        huff_build(&dist_h, lens, 32);
      } else {
        int hlit = (int)br_bits(&br, 5) + 257;
        int hdist = (int)br_bits(&br, 5) + 1;
        int hclen = (int)br_bits(&br, 4) + 4;
        uint8_t cl_lens[19];
        n_memset(cl_lens, 0, 19);
        for (int i = 0; i < hclen; i++)
          cl_lens[cl_order[i]] = (uint8_t)br_bits(&br, 3);
        huff_build(&cl_h, cl_lens, 19);

        int idx = 0;
        int total = hlit + hdist;
        while (idx < total && idx < 320) {
          int sym = huff_decode(&br, &cl_h);
          if (sym < 0) return false;
          if (sym < 16) {
            lens[idx++] = (uint8_t)sym;
          } else {
            int rep = 0;
            uint8_t val = 0;
            if (sym == 16) {
              val = (idx > 0) ? lens[idx - 1] : 0;
              rep = 3 + (int)br_bits(&br, 2);
            } else if (sym == 17) {
              rep = 3 + (int)br_bits(&br, 3);
            } else {
              rep = 11 + (int)br_bits(&br, 7);
            }
            while (rep-- > 0 && idx < total && idx < 320)
              lens[idx++] = val;
          }
        }
        huff_build(&lit_h, lens, hlit);
        huff_build(&dist_h, lens + hlit, hdist);
      }

      for (;;) {
        int sym = huff_decode(&br, &lit_h);
        if (sym < 0) return false;
        if (sym < 256) {
          if (dp < max_dst) dst[dp++] = (uint8_t)sym;
        } else if (sym == 256) {
          break;
        } else {
          int lidx = sym - 257;
          if (lidx >= 29) return false;
          uint32_t length = lbase[lidx] + br_bits(&br, lext[lidx]);
          int dsym = huff_decode(&br, &dist_h);
          if (dsym < 0 || dsym >= 30) return false;
          uint32_t dist = dbase[dsym] + br_bits(&br, dext[dsym]);
          if (dist > dp) return false;
          for (uint32_t k = 0; k < length; k++) {
            uint8_t b = dst[dp - dist];
            if (dp < max_dst) dst[dp++] = b;
          }
        }
      }
    } else {
      return false;
    }
  } while (!bfinal);

  *out_len = dp;
  return true;
}

static bool gunzip_or_inflate(const uint8_t *src, size_t src_len, uint8_t *dst, size_t max_dst, size_t *out_len) {
  if (src_len >= 10 && src[0] == 0x1F && src[1] == 0x8B && src[2] == 0x08) {
    uint8_t flg = src[3];
    size_t pos = 10;
    if (flg & 4) { // FEXTRA
      if (pos + 2 > src_len) return false;
      pos += 2 + (src[pos] | (src[pos + 1] << 8));
    }
    if (flg & 8) { // FNAME
      while (pos < src_len && src[pos] != 0) pos++;
      pos++;
    }
    if (flg & 16) { // FCOMMENT
      while (pos < src_len && src[pos] != 0) pos++;
      pos++;
    }
    if (flg & 2) pos += 2; // FHCRC
    if (pos >= src_len) return false;
    return inflate_raw(src + pos, src_len - pos, dst, max_dst, out_len);
  }
  if (src_len >= 2 && (src[0] & 0x0F) == 0x08) {
    return inflate_raw(src + 2, src_len - 2, dst, max_dst, out_len);
  }
  return inflate_raw(src, src_len, dst, max_dst, out_len);
}

// ==============================================================================
// SESSIONE TLS 1.3 CLIENT
// ==============================================================================
typedef struct {
  int          sock;
  const kernel_api_t *api;
  uint16_t     cipher_suite;
  uint8_t      key_len;
  uint8_t      last_err_code; // 1=hdr_timeout, 2=bad_rlen, 3=body_timeout, 4=decrypt_fail
  uint8_t      c_key[32], c_iv[12];
  uint8_t      s_key[32], s_iv[12];
  uint64_t     c_seq;
  uint64_t     s_seq;
  uint8_t      c_ap_secret[32], s_ap_secret[32];
  uint8_t     *rx_buf;
  size_t       rx_len;
  size_t       rx_cap;
} tls13_ctx_t;

static bool tcp_send_all(const kernel_api_t *api, int sock, const uint8_t *buf, size_t len) {
  size_t sent = 0;
  while (sent < len) {
    int r = api->net_send(sock, buf + sent, len - sent, 0);
    if (r <= 0) return false;
    sent += (size_t)r;
  }
  return true;
}

static bool tcp_recv_fill(const kernel_api_t *api, int sock, uint8_t *buf, size_t *len, size_t need, int max_wait_ms) {
  int elapsed = 0;
  while (*len < need && elapsed < max_wait_ms) {
    api->net_poll();
    int r = api->net_recv(sock, buf + *len, need - *len, 0);
    if (r > 0) {
      *len += (size_t)r;
    } else if (r == 0) {
      break; // Connessione chiusa (FIN)
    } else {
      api->timer_sleep(10);
      elapsed += 10;
    }
  }
  return (*len >= need);
}

static bool tls13_send_encrypted(tls13_ctx_t *ctx, uint8_t inner_type, const uint8_t *data, size_t len) {
  uint8_t *rec = (uint8_t *)ctx->api->kmalloc(5 + len + 1 + 16);
  if (!rec) return false;
  uint8_t *pt = (uint8_t *)ctx->api->kmalloc(len + 1);
  if (!pt) { ctx->api->kfree(rec); return false; }

  n_memcpy(pt, data, len);
  pt[len] = inner_type;

  size_t ct_len = len + 1 + 16;
  rec[0] = 0x17; rec[1] = 0x03; rec[2] = 0x03;
  wr_be16(rec + 3, (uint16_t)ct_len);

  uint8_t tag[16];
  if (ctx->cipher_suite == 0x1301) {
    aead_aes128_gcm_encrypt(ctx->c_key, ctx->c_iv, ctx->c_seq++, rec, 5, pt, len + 1, rec + 5, tag);
  } else {
    aead_chacha_encrypt(ctx->c_key, ctx->c_iv, ctx->c_seq++, rec, 5, pt, len + 1, rec + 5, tag);
  }
  n_memcpy(rec + 5 + len + 1, tag, 16);

  bool ok = tcp_send_all(ctx->api, ctx->sock, rec, 5 + ct_len);
  ctx->api->kfree(pt);
  ctx->api->kfree(rec);
  return ok;
}

// Legge il prossimo record TLS (cifrato o in chiaro) in out_buf
static bool tls13_recv_record(tls13_ctx_t *ctx, bool encrypted, uint8_t *rec_type_out,
                              uint8_t *out_buf, size_t max_out, size_t *out_len) {
  ctx->last_err_code = 0;
  uint8_t hdr[5];
  size_t hlen = 0;
  if (!tcp_recv_fill(ctx->api, ctx->sock, hdr, &hlen, 5, 7000)) {
    ctx->last_err_code = 1;
    return false;
  }

  uint8_t rtype = hdr[0];
  uint16_t rlen = rd_be16(hdr + 3);
  if (rlen > max_out || rlen == 0) {
    ctx->last_err_code = 2;
    return false;
  }

  uint8_t *raw = (uint8_t *)ctx->api->kmalloc(rlen);
  if (!raw) return false;

  size_t got = 0;
  if (!tcp_recv_fill(ctx->api, ctx->sock, raw, &got, rlen, 7000)) {
    ctx->api->kfree(raw);
    ctx->last_err_code = 3;
    return false;
  }

  if (!encrypted || rtype == 0x14 || rtype == 0x16 || rtype == 0x15) {
    n_memcpy(out_buf, raw, rlen);
    *out_len = rlen;
    *rec_type_out = rtype;
    ctx->api->kfree(raw);
    return true;
  }

  if (rlen < 17) {
    ctx->api->kfree(raw);
    ctx->last_err_code = 2;
    return false;
  }

  size_t pt_len = rlen - 16;
  bool ok = (ctx->cipher_suite == 0x1301)
    ? aead_aes128_gcm_decrypt(ctx->s_key, ctx->s_iv, ctx->s_seq++, hdr, 5, raw, pt_len, raw + pt_len, out_buf)
    : aead_chacha_decrypt(ctx->s_key, ctx->s_iv, ctx->s_seq++, hdr, 5, raw, pt_len, raw + pt_len, out_buf);
  ctx->api->kfree(raw);
  if (!ok) {
    ctx->last_err_code = 4;
    return false;
  }

  // Rimuove padding zero e estrae inner ContentType
  while (pt_len > 0 && out_buf[pt_len - 1] == 0)
    pt_len--;
  if (pt_len == 0) return false;

  *rec_type_out = out_buf[pt_len - 1];
  *out_len = pt_len - 1;
  return true;
}

static bool tls13_handshake(tls13_ctx_t *ctx, const char *host, char *err_out, size_t err_sz) {
  uint8_t priv[32], pub[32], rnd[32], sid[32];
  n_random_bytes(priv, 32);
  x25519_base(pub, priv);
  n_random_bytes(rnd, 32);
  n_random_bytes(sid, 32);

  size_t host_len = n_strlen(host);
  uint8_t *ch = (uint8_t *)ctx->api->kmalloc(512);
  uint8_t *rec = (uint8_t *)ctx->api->kmalloc(20480);
  if (!ch || !rec) {
    if (ch) ctx->api->kfree(ch);
    if (rec) ctx->api->kfree(rec);
    n_strcpy(err_out, "OOM TLS", err_sz);
    return false;
  }

  // Costruzione ClientHello (Record 0x16 + Handshake 0x01)
  size_t p = 5;
  ch[p++] = 0x01; // ClientHello
  size_t hs_len_pos = p; p += 3;
  ch[p++] = 0x03; ch[p++] = 0x03; // legacy_version = TLS 1.2
  n_memcpy(ch + p, rnd, 32); p += 32;
  ch[p++] = 32; n_memcpy(ch + p, sid, 32); p += 32; // legacy_session_id
  // cipher_suites: TLS_CHACHA20_POLY1305_SHA256 (0x1303) + TLS_AES_128_GCM_SHA256 (0x1301)
  ch[p++] = 0x00; ch[p++] = 0x04;
  ch[p++] = 0x13; ch[p++] = 0x03;
  ch[p++] = 0x13; ch[p++] = 0x01;
  ch[p++] = 0x01; ch[p++] = 0x00; // compression_methods: null

  size_t ext_len_pos = p; p += 2;
  // 1. server_name (0x0000)
  wr_be16(ch + p, 0x0000); p += 2;
  wr_be16(ch + p, (uint16_t)(host_len + 5)); p += 2;
  wr_be16(ch + p, (uint16_t)(host_len + 3)); p += 2;
  ch[p++] = 0x00;
  wr_be16(ch + p, (uint16_t)host_len); p += 2;
  n_memcpy(ch + p, host, host_len); p += host_len;
  // 2. supported_groups (0x000a): x25519 (0x001d)
  wr_be16(ch + p, 0x000a); p += 2; wr_be16(ch + p, 4); p += 2;
  wr_be16(ch + p, 2); p += 2; wr_be16(ch + p, 0x001d); p += 2;
  // 3. signature_algorithms (0x000d)
  wr_be16(ch + p, 0x000d); p += 2; wr_be16(ch + p, 10); p += 2;
  wr_be16(ch + p, 8); p += 2;
  wr_be16(ch + p, 0x0403); p += 2; wr_be16(ch + p, 0x0804); p += 2;
  wr_be16(ch + p, 0x0401); p += 2; wr_be16(ch + p, 0x0807); p += 2;
  // 4. ALPN (0x0010): http/1.1
  wr_be16(ch + p, 0x0010); p += 2; wr_be16(ch + p, 11); p += 2;
  wr_be16(ch + p, 9); p += 2; ch[p++] = 8;
  n_memcpy(ch + p, "http/1.1", 8); p += 8;
  // 5. supported_versions (0x002b): TLS 1.3 (0x0304)
  wr_be16(ch + p, 0x002b); p += 2; wr_be16(ch + p, 3); p += 2;
  ch[p++] = 2; wr_be16(ch + p, 0x0304); p += 2;
  // 6. key_share (0x0033): x25519 (32 byte)
  wr_be16(ch + p, 0x0033); p += 2; wr_be16(ch + p, 38); p += 2;
  wr_be16(ch + p, 36); p += 2; wr_be16(ch + p, 0x001d); p += 2;
  wr_be16(ch + p, 32); p += 2;
  n_memcpy(ch + p, pub, 32); p += 32;

  wr_be16(ch + ext_len_pos, (uint16_t)(p - ext_len_pos - 2));
  wr_be24(ch + hs_len_pos, (uint32_t)(p - hs_len_pos - 3));
  ch[0] = 0x16; ch[1] = 0x03; ch[2] = 0x01;
  wr_be16(ch + 3, (uint16_t)(p - 5));

  sha256_ctx_t tr_ctx;
  sha256_init(&tr_ctx);
  sha256_update(&tr_ctx, ch + 5, p - 5);

  if (!tcp_send_all(ctx->api, ctx->sock, ch, p)) {
    ctx->api->kfree(ch); ctx->api->kfree(rec);
    n_strcpy(err_out, "Invio ClientHello fallito", err_sz);
    return false;
  }
  ctx->api->kfree(ch);

  // Ricezione ServerHello
  uint8_t rtype = 0;
  size_t rlen = 0;
  if (!tls13_recv_record(ctx, false, &rtype, rec, 20480, &rlen) || rtype != 0x16 || rlen < 42 || rec[0] != 0x02) {
    ctx->api->kfree(rec);
    n_strcpy(err_out, "ServerHello TLS 1.3 non ricevuto", err_sz);
    return false;
  }

  sha256_update(&tr_ctx, rec, rlen);

  // Parsing cipher_suite + key_share dal ServerHello
  size_t sh_pos = 4 + 2 + 32; // hdr(4) + ver(2) + rnd(32)
  if (sh_pos >= rlen) { ctx->api->kfree(rec); return false; }
  uint8_t sh_sid_len = rec[sh_pos++];
  if (sh_pos + sh_sid_len + 3 > rlen) { ctx->api->kfree(rec); return false; }
  sh_pos += sh_sid_len;
  ctx->cipher_suite = rd_be16(rec + sh_pos);
  if (ctx->cipher_suite != 0x1301 && ctx->cipher_suite != 0x1303) {
    ctx->api->kfree(rec);
    n_strcpy(err_out, "Cipher suite TLS non supportata", err_sz);
    return false;
  }
  ctx->key_len = (ctx->cipher_suite == 0x1301) ? 16 : 32;
  sh_pos += 3; // cipher(2) + comp(1)
  if (sh_pos + 2 > rlen) { ctx->api->kfree(rec); return false; }
  uint16_t sh_ext_len = rd_be16(rec + sh_pos); sh_pos += 2;

  uint8_t srv_pub[32];
  bool got_key_share = false;
  size_t sh_end = sh_pos + sh_ext_len;
  while (sh_pos + 4 <= sh_end && sh_pos + 4 <= rlen) {
    uint16_t etype = rd_be16(rec + sh_pos);
    uint16_t elen = rd_be16(rec + sh_pos + 2);
    sh_pos += 4;
    if (etype == 0x0033 && elen >= 36 && rd_be16(rec + sh_pos) == 0x001d && rd_be16(rec + sh_pos + 2) == 32) {
      n_memcpy(srv_pub, rec + sh_pos + 4, 32);
      got_key_share = true;
      break;
    }
    sh_pos += elen;
  }

  if (!got_key_share) {
    ctx->api->kfree(rec);
    n_strcpy(err_out, "Server non supporta X25519/TLS 1.3", err_sz);
    return false;
  }

  // Calcolo chiavi Handshake TLS 1.3
  uint8_t zeros[32], empty_hash[32], early_sec[32], derived[32], shared[32], hs_sec[32], tr_hash[32];
  n_memset(zeros, 0, 32);
  sha256_oneshot(zeros, 0, empty_hash);
  hkdf_extract(zeros, 32, zeros, 32, early_sec);
  hkdf_expand_label(early_sec, "derived", empty_hash, 32, derived, 32);

  x25519_scalarmult(shared, priv, srv_pub);
  hkdf_extract(derived, 32, shared, 32, hs_sec);

  sha256_ctx_t tr_copy = tr_ctx;
  sha256_final(&tr_copy, tr_hash);

  uint8_t c_hs_sec[32], s_hs_sec[32];
  hkdf_expand_label(hs_sec, "c hs traffic", tr_hash, 32, c_hs_sec, 32);
  hkdf_expand_label(hs_sec, "s hs traffic", tr_hash, 32, s_hs_sec, 32);

  hkdf_expand_label(c_hs_sec, "key", NULL, 0, ctx->c_key, ctx->key_len);
  hkdf_expand_label(c_hs_sec, "iv",  NULL, 0, ctx->c_iv,  12);
  hkdf_expand_label(s_hs_sec, "key", NULL, 0, ctx->s_key, ctx->key_len);
  hkdf_expand_label(s_hs_sec, "iv",  NULL, 0, ctx->s_iv,  12);
  ctx->c_seq = 0;
  ctx->s_seq = 0;

  // Riceve EncryptedExtensions, Certificate (anche multi-record), CertificateVerify, Finished
  size_t hs_cap = 32768;
  uint8_t *hs_buf = (uint8_t *)ctx->api->kmalloc(hs_cap);
  if (!hs_buf) {
    ctx->api->kfree(rec);
    n_strcpy(err_out, "OOM Handshake", err_sz);
    return false;
  }
  size_t hs_len = 0;
  bool got_finished = false;
  while (!got_finished) {
    if (!tls13_recv_record(ctx, true, &rtype, rec, 20480, &rlen)) {
      ctx->api->kfree(hs_buf);
      ctx->api->kfree(rec);
      n_strcpy(err_out, "Errore decifratura handshake server", err_sz);
      return false;
    }
    if (rtype == 0x14) continue; // ChangeCipherSpec middlebox ignorato
    if (rtype != 0x16) {
      ctx->api->kfree(hs_buf);
      ctx->api->kfree(rec);
      n_strcpy(err_out, "Alert TLS dal server", err_sz);
      return false;
    }

    if (hs_len + rlen > hs_cap) {
      ctx->api->kfree(hs_buf);
      ctx->api->kfree(rec);
      n_strcpy(err_out, "Handshake server troppo grande", err_sz);
      return false;
    }
    n_memcpy(hs_buf + hs_len, rec, rlen);
    hs_len += rlen;

    size_t pos = 0;
    while (pos + 4 <= hs_len) {
      uint8_t htype = hs_buf[pos];
      uint32_t hlen = rd_be24(hs_buf + pos + 1);
      if (pos + 4 + hlen > hs_len) break; // Attende prossimo record TLS
      sha256_update(&tr_ctx, hs_buf + pos, 4 + hlen);
      if (htype == 20) { // Finished
        got_finished = true;
      }
      pos += 4 + hlen;
    }
    if (pos > 0) {
      size_t rem = hs_len - pos;
      for (size_t i = 0; i < rem; i++) hs_buf[i] = hs_buf[pos + i];
      hs_len = rem;
    }
  }
  ctx->api->kfree(hs_buf);

  // Derivazione chiavi Application Traffic (usando transcript fino a Server Finished)
  tr_copy = tr_ctx;
  sha256_final(&tr_copy, tr_hash);

  uint8_t master_sec[32];
  hkdf_expand_label(hs_sec, "derived", empty_hash, 32, derived, 32);
  hkdf_extract(derived, 32, zeros, 32, master_sec);
  hkdf_expand_label(master_sec, "c ap traffic", tr_hash, 32, ctx->c_ap_secret, 32);
  hkdf_expand_label(master_sec, "s ap traffic", tr_hash, 32, ctx->s_ap_secret, 32);

  // Costruzione e invio Client Finished (cifrato con c_hs_key)
  uint8_t fin_key[32], verify_data[32], cfin[36];
  hkdf_expand_label(c_hs_sec, "finished", NULL, 0, fin_key, 32);
  hmac_sha256(fin_key, 32, tr_hash, 32, verify_data);
  cfin[0] = 20;
  wr_be24(cfin + 1, 32);
  n_memcpy(cfin + 4, verify_data, 32);

  bool ok_fin = tls13_send_encrypted(ctx, 0x16, cfin, 36);
  ctx->api->kfree(rec);
  if (!ok_fin) {
    n_strcpy(err_out, "Invio Client Finished fallito", err_sz);
    return false;
  }

  // Attiva chiavi Application Data!
  hkdf_expand_label(ctx->c_ap_secret, "key", NULL, 0, ctx->c_key, ctx->key_len);
  hkdf_expand_label(ctx->c_ap_secret, "iv",  NULL, 0, ctx->c_iv,  12);
  hkdf_expand_label(ctx->s_ap_secret, "key", NULL, 0, ctx->s_key, ctx->key_len);
  hkdf_expand_label(ctx->s_ap_secret, "iv",  NULL, 0, ctx->s_iv,  12);
  ctx->c_seq = 0;
  ctx->s_seq = 0;
  return true;
}

// ==============================================================================
// PARSING URL + CLIENT HTTP/1.1 + REDIRECT + CHUNKED + GZIP
// ==============================================================================
static bool parse_url(const char *url, bool *is_https, char *host, size_t max_host,
                      uint16_t *port, char *path, size_t max_path) {
  const char *p = url;
  *is_https = false;
  if (n_strncasecmp(p, "https://", 8) == 0) {
    *is_https = true;
    p += 8;
    *port = 443;
  } else if (n_strncasecmp(p, "http://", 7) == 0) {
    p += 7;
    *port = 80;
  } else {
    *port = 80;
  }

  size_t hi = 0;
  while (*p && *p != '/' && *p != ':' && *p != '?' && hi + 1 < max_host) {
    host[hi++] = *p++;
  }
  host[hi] = '\0';
  if (hi == 0) return false;

  if (*p == ':') {
    p++;
    uint32_t pv = 0;
    while (*p >= '0' && *p <= '9') {
      pv = pv * 10 + (uint32_t)(*p - '0');
      p++;
    }
    if (pv > 0 && pv <= 65535) *port = (uint16_t)pv;
  }

  if (*p == '/' || *p == '?') {
    n_strcpy(path, p, max_path);
  } else {
    n_strcpy(path, "/", max_path);
  }
  return true;
}

static size_t decode_chunked(uint8_t *buf, size_t len) {
  size_t r = 0, w = 0;
  while (r < len) {
    uint32_t chunk_sz = 0;
    bool got_hex = false;
    while (r < len) {
      uint8_t c = buf[r];
      if (c >= '0' && c <= '9') { chunk_sz = (chunk_sz << 4) | (c - '0'); got_hex = true; r++; }
      else if (c >= 'a' && c <= 'f') { chunk_sz = (chunk_sz << 4) | (c - 'a' + 10); got_hex = true; r++; }
      else if (c >= 'A' && c <= 'F') { chunk_sz = (chunk_sz << 4) | (c - 'A' + 10); got_hex = true; r++; }
      else break;
    }
    while (r < len && buf[r] != '\n') r++;
    if (r < len && buf[r] == '\n') r++;
    if (!got_hex || chunk_sz == 0) break;
    size_t copy = (r + chunk_sz <= len) ? chunk_sz : (len - r);
    for (size_t i = 0; i < copy; i++) buf[w++] = buf[r + i];
    r += copy;
    while (r < len && (buf[r] == '\r' || buf[r] == '\n')) r++;
  }
  return w;
}

static bool http_fetch_single(const kernel_api_t *api, const char *url,
                              uint32_t max_body, nettls_response_t *resp,
                              char *redirect_loc, size_t max_loc) {
  bool is_https = false;
  uint16_t port = 80;
  char host[128], path[512];
  redirect_loc[0] = '\0';

  if (!parse_url(url, &is_https, host, sizeof(host), &port, path, sizeof(path))) {
    n_strcpy(resp->error_msg, "URL non valido", sizeof(resp->error_msg));
    return false;
  }

  resp->is_https = is_https;
  resp->tls_active = false;
  n_strcpy(resp->final_url, url, sizeof(resp->final_url));

  uint32_t ip = api->dns_resolve(host);
  if (ip == 0) {
    n_strcpy(resp->error_msg, "DNS: Host non risolto", sizeof(resp->error_msg));
    return false;
  }

  int sock = api->net_socket(2, 1, 0); // AF_INET, SOCK_STREAM
  if (sock < 0) {
    n_strcpy(resp->error_msg, "Errore apertura socket TCP", sizeof(resp->error_msg));
    return false;
  }

  struct {
    uint16_t sin_family;
    uint16_t sin_port;
    uint32_t sin_addr;
    uint8_t  sin_zero[8];
  } saddr;
  n_memset(&saddr, 0, sizeof(saddr));
  saddr.sin_family = 2;
  saddr.sin_port = n_htons(port);
  saddr.sin_addr = ip;

  if (api->net_connect(sock, &saddr) != 0) {
    api->net_close(sock);
    n_strcpy(resp->error_msg, "Connessione TCP rifiutata / timeout", sizeof(resp->error_msg));
    return false;
  }

  tls13_ctx_t tls;
  n_memset(&tls, 0, sizeof(tls));
  tls.sock = sock;
  tls.api = api;

  if (is_https) {
    if (!tls13_handshake(&tls, host, resp->error_msg, sizeof(resp->error_msg))) {
      api->net_close(sock);
      return false;
    }
    resp->tls_active = true;
  }

  // Costruzione richiesta HTTP/1.1 (con User-Agent moderno e Cookie Jar)
  char req[2048];
  size_t rp = 0;
  const char *p1 = "GET ";
  const char *p2 = " HTTP/1.1\r\nHost: ";
  const char *p3 = "\r\nUser-Agent: NewOS-WebBrowser/1.0\r\nAccept: text/html,application/xhtml+xml,*/*;q=0.8\r\nAccept-Language: it-IT,it;q=0.9,en-US;q=0.8,en;q=0.7\r\nAccept-Encoding: gzip, deflate\r\nConnection: close\r\n";
  for (size_t i = 0; p1[i]; i++) req[rp++] = p1[i];
  for (size_t i = 0; path[i] && rp < 700; i++) req[rp++] = path[i];
  for (size_t i = 0; p2[i]; i++) req[rp++] = p2[i];
  for (size_t i = 0; host[i] && rp < 860; i++) req[rp++] = host[i];
  for (size_t i = 0; p3[i] && rp + 4 < sizeof(req); i++) req[rp++] = p3[i];
  rp += cookie_append_header(host, req + rp, sizeof(req) - rp - 3);
  req[rp++] = '\r';
  req[rp++] = '\n';
  req[rp] = '\0';

  bool sent_ok = is_https ? tls13_send_encrypted(&tls, 0x17, (const uint8_t *)req, rp)
                          : tcp_send_all(api, sock, (const uint8_t *)req, rp);
  if (!sent_ok) {
    api->net_close(sock);
    n_strcpy(resp->error_msg, "Errore invio richiesta HTTP", sizeof(resp->error_msg));
    return false;
  }

  size_t raw_cap = max_body + 32768;
  uint8_t *raw = (uint8_t *)api->kmalloc(raw_cap);
  if (!raw) {
    api->net_close(sock);
    n_strcpy(resp->error_msg, "Memoria insufficiente", sizeof(resp->error_msg));
    return false;
  }

  size_t total = 0;
  int last_alert = -1;
  if (is_https) {
    uint8_t *rec = (uint8_t *)api->kmalloc(20480);
    if (rec) {
      while (total < raw_cap - 1) {
        uint8_t rtype = 0;
        size_t rlen = 0;
        if (!tls13_recv_record(&tls, true, &rtype, rec, 20480, &rlen)) {
          break;
        }
        if (rtype == 0x16) continue; // NewSessionTicket
        if (rtype == 0x15) {         // Alert / close_notify
          if (rlen >= 2) last_alert = rec[1];
          break;
        }
        if (rtype == 0x17 && rlen > 0) {
          size_t cp = (total + rlen < raw_cap - 1) ? rlen : (raw_cap - 1 - total);
          n_memcpy(raw + total, rec, cp);
          total += cp;
        }
      }
      api->kfree(rec);
    }
  } else {
    int wait = 0;
    while (wait < 80 && total < raw_cap - 1) {
      api->net_poll();
      int r = api->net_recv(sock, raw + total, raw_cap - 1 - total, 0);
      if (r > 0) {
        total += (size_t)r;
        wait = 0;
      } else if (r == 0) {
        break;
      } else {
        api->timer_sleep(10);
        wait++;
      }
    }
  }
  api->net_close(sock);

  if (total == 0) {
    api->kfree(raw);
    if (last_alert >= 0) {
      n_strcpy(resp->error_msg, "Alert TLS dal server (connessione chiusa)", sizeof(resp->error_msg));
    } else if (tls.last_err_code == 4) {
      n_strcpy(resp->error_msg, "Errore decifratura record TLS applicativo", sizeof(resp->error_msg));
    } else if (tls.last_err_code == 2) {
      n_strcpy(resp->error_msg, "Record TLS fuori dimensione", sizeof(resp->error_msg));
    } else if (tls.last_err_code == 1 || tls.last_err_code == 3) {
      n_strcpy(resp->error_msg, "Timeout / chiusura TCP durante ricezione TLS", sizeof(resp->error_msg));
    } else {
      n_strcpy(resp->error_msg, "Risposta vuota dal server", sizeof(resp->error_msg));
    }
    return false;
  }
  raw[total] = '\0';

  // Separazione Header / Body
  size_t hdr_end = 0;
  for (size_t i = 0; i + 3 < total; i++) {
    if (raw[i] == '\r' && raw[i + 1] == '\n' && raw[i + 2] == '\r' && raw[i + 3] == '\n') {
      hdr_end = i + 4;
      raw[i] = '\0';
      break;
    }
  }

  // Parsing status code e salvataggio eventuali Set-Cookie
  const char *hdr = (const char *)raw;
  cookie_parse_headers(hdr, host);
  const char *sp = hdr;
  while (*sp && *sp != ' ') sp++;
  while (*sp == ' ') sp++;
  int code = 0;
  while (*sp >= '0' && *sp <= '9') {
    code = code * 10 + (*sp - '0');
    sp++;
  }
  resp->status_code = code;

  // Redirect Location:
  if (code == 301 || code == 302 || code == 303 || code == 307 || code == 308) {
    const char *loc = n_strcasestr(hdr, "\nLocation:");
    if (loc) {
      loc += 10;
      while (*loc == ' ' || *loc == '\t') loc++;
      size_t li = 0;
      while (*loc && *loc != '\r' && *loc != '\n' && li + 1 < max_loc)
        redirect_loc[li++] = *loc++;
      redirect_loc[li] = '\0';
    }
    api->kfree(raw);
    return true;
  }

  bool is_chunked = (n_strcasestr(hdr, "Transfer-Encoding: chunked") != NULL);
  bool is_gzip = (n_strcasestr(hdr, "Content-Encoding: gzip") != NULL) ||
                 (n_strcasestr(hdr, "Content-Encoding: deflate") != NULL);

  size_t body_len = (hdr_end > 0 && hdr_end <= total) ? (total - hdr_end) : total;
  uint8_t *body_ptr = (hdr_end > 0) ? (raw + hdr_end) : raw;

  if (is_chunked && body_len > 0) {
    body_len = decode_chunked(body_ptr, body_len);
  }

  char *out_buf = (char *)api->kmalloc(max_body + 1);
  if (!out_buf) {
    api->kfree(raw);
    n_strcpy(resp->error_msg, "Memoria insufficiente", sizeof(resp->error_msg));
    return false;
  }

  if (is_gzip && body_len > 0) {
    size_t unz_len = 0;
    if (gunzip_or_inflate(body_ptr, body_len, (uint8_t *)out_buf, max_body, &unz_len)) {
      out_buf[unz_len] = '\0';
      resp->body = out_buf;
      resp->body_len = (uint32_t)unz_len;
      api->kfree(raw);
      return true;
    }
  }

  size_t final_len = (body_len < max_body) ? body_len : max_body;
  n_memcpy(out_buf, body_ptr, final_len);
  out_buf[final_len] = '\0';
  resp->body = out_buf;
  resp->body_len = (uint32_t)final_len;
  api->kfree(raw);
  return true;
}

static int nettls_dispatch(const kernel_api_t *api, nettls_params_t *p) {
  if (!api || !p) return -1;
  if (p->cmd == NETTLS_CMD_INIT) return 0;

  if (p->cmd == NETTLS_CMD_HTTP_GET && p->url && p->resp) {
    char cur_url[512];
    char next_loc[512];
    n_strcpy(cur_url, p->url, sizeof(cur_url));

    for (int hop = 0; hop < 5; hop++) {
      next_loc[0] = '\0';
      if (!http_fetch_single(api, cur_url, p->resp->max_body_bytes, p->resp, next_loc, sizeof(next_loc)))
        return -1;

      if (next_loc[0] != '\0') {
        if (next_loc[0] == '/') {
          // Redirect relativo: ricostruisce schema + host
          bool hs = false;
          uint16_t pt = 80;
          char hst[128], pth[64];
          parse_url(cur_url, &hs, hst, sizeof(hst), &pt, pth, sizeof(pth));
          const char *sch = hs ? "https://" : "http://";
          size_t up = 0;
          for (size_t i = 0; sch[i]; i++) cur_url[up++] = sch[i];
          for (size_t i = 0; hst[i] && up < 200; i++) cur_url[up++] = hst[i];
          for (size_t i = 0; next_loc[i] && up + 1 < sizeof(cur_url); i++) cur_url[up++] = next_loc[i];
          cur_url[up] = '\0';
        } else {
          n_strcpy(cur_url, next_loc, sizeof(cur_url));
        }
        continue;
      }
      return 0;
    }
    n_strcpy(p->resp->error_msg, "Troppi redirect HTTP", sizeof(p->resp->error_msg));
    return -1;
  }

  return -1;
}

