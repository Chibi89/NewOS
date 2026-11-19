#include "../WebBrowser/imgcodec.h"
#include <screen_modules.h>

__attribute__((section(".header"), used)) module_header_t g_imgcodec_header = {
    .magic = MODULE_MAGIC_NEWOS,
    .header_size = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type = MODULE_TYPE_SYSTEM_LIBRARY,
    .code_size = 0,
    .entry_offset = 64,
    .checksum = 0,
    .security_flags =
        SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name = "IMGCODEC",
    .signature = MODULE_AUTH_TAG};

static const kernel_api_t *g_ic_api = NULL;
static int imgcodec_dispatch(const kernel_api_t *api, imgcodec_params_t *p);

int module_main(const kernel_api_t *api, imgcodec_params_t *p) {
    return imgcodec_dispatch(api, p);
}

typedef struct {
    size_t size;
    size_t magic;
} ic_alloc_hdr_t;

static void *ic_malloc(size_t size) {
    if (!g_ic_api || !g_ic_api->kmalloc) return NULL;
    if (size == 0) size = 1;
    ic_alloc_hdr_t *h = (ic_alloc_hdr_t *)g_ic_api->kmalloc(sizeof(ic_alloc_hdr_t) + size);
    if (!h) return NULL;
    h->size = size;
    h->magic = 0x494D4743u;
    return (void *)(h + 1);
}

static void ic_free(void *ptr) {
    if (!ptr || !g_ic_api || !g_ic_api->kfree) return;
    ic_alloc_hdr_t *h = ((ic_alloc_hdr_t *)ptr) - 1;
    if (h->magic == 0x494D4743u) {
        h->magic = 0;
        g_ic_api->kfree(h);
    }
}

static void *ic_realloc_sized(void *ptr, size_t oldsz, size_t newsz) {
    (void)oldsz;
    if (!ptr) return ic_malloc(newsz);
    if (newsz == 0) {
        ic_free(ptr);
        return NULL;
    }
    ic_alloc_hdr_t *h = ((ic_alloc_hdr_t *)ptr) - 1;
    if (newsz <= h->size) return ptr;
    void *np = ic_malloc(newsz);
    if (!np) return NULL;
    uint8_t *d = (uint8_t *)np;
    const uint8_t *s = (const uint8_t *)ptr;
    for (size_t i = 0; i < h->size; i++) d[i] = s[i];
    ic_free(ptr);
    return np;
}

static void *ic_realloc(void *ptr, size_t newsz) {
    return ic_realloc_sized(ptr, 0, newsz);
}

void *malloc(size_t size) { return ic_malloc(size); }
void free(void *ptr) { ic_free(ptr); }
void *calloc(size_t nmemb, size_t size) {
    size_t tot = nmemb * size;
    void *p = ic_malloc(tot);
    if (p) {
        uint8_t *b = (uint8_t *)p;
        for (size_t i = 0; i < tot; i++) b[i] = 0;
    }
    return p;
}
void *realloc(void *ptr, size_t size) { return ic_realloc(ptr, size); }

// Funzioni minime libc richieste da stb_image in freestanding
void *memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dest;
}

void *memset(void *dest, int c, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    for (size_t i = 0; i < n; i++) d[i] = (uint8_t)c;
    return dest;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const uint8_t *a = (const uint8_t *)s1;
    const uint8_t *b = (const uint8_t *)s2;
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return (int)a[i] - (int)b[i];
    }
    return 0;
}

void *memmove(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    if (d < s) {
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    } else if (d > s) {
        for (size_t i = n; i > 0; i--) d[i - 1] = s[i - 1];
    }
    return dest;
}

void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *)) {
    if (!base || nmemb < 2 || size == 0 || !compar) return;
    uint8_t *b = (uint8_t *)base;
    for (size_t i = 1; i < nmemb; i++) {
        for (size_t j = i; j > 0 && compar(b + (j - 1) * size, b + j * size) > 0; j--) {
            uint8_t *p1 = b + (j - 1) * size;
            uint8_t *p2 = b + j * size;
            for (size_t k = 0; k < size; k++) {
                uint8_t t = p1[k]; p1[k] = p2[k]; p2[k] = t;
            }
        }
    }
}

#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_FAILURE_STRINGS
#define STBI_NO_THREAD_LOCALS
#define STBI_NO_SIMD
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_GIF
#define STBI_ONLY_BMP
#define STBI_ASSERT(x) ((void)(x))
#define STBI_MALLOC(sz) ic_malloc(sz)
#define STBI_REALLOC(p, newsz) ic_realloc((p), (newsz))
#define STBI_REALLOC_SIZED(p, oldsz, newsz) ic_realloc_sized((p), (oldsz), (newsz))
#define STBI_FREE(p) ic_free(p)
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "webp/decode.h"
#include "src/utils/random_utils.h"

void VP8InitRandom(VP8Random *const rg, float dithering) {
    (void)dithering;
    if (!rg) return;
    rg->index1_ = 0;
    rg->index2_ = 31;
    rg->amp_ = 0;
    for (int i = 0; i < VP8_RANDOM_TABLE_SIZE; i++) rg->tab_[i] = (uint32_t)(i * 1103515245u + 12345u) & 0x7FFFFFFFu;
}

static int detect_format(const uint8_t *d, uint32_t len) {
    if (!d || len < 4) return IMGCODEC_FMT_UNKNOWN;
    if (d[0] == 0xFF && d[1] == 0xD8) return IMGCODEC_FMT_JPEG;
    if (d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G') return IMGCODEC_FMT_PNG;
    if (d[0] == 'G' && d[1] == 'I' && d[2] == 'F' && d[3] == '8') return IMGCODEC_FMT_GIF;
    if (d[0] == 'B' && d[1] == 'M') return IMGCODEC_FMT_BMP;
    if (len >= 12 && d[0] == 'R' && d[1] == 'I' && d[2] == 'F' && d[3] == 'F' &&
        d[8] == 'W' && d[9] == 'E' && d[10] == 'B' && d[11] == 'P') return IMGCODEC_FMT_WEBP;
    return IMGCODEC_FMT_UNKNOWN;
}

static int imgcodec_dispatch(const kernel_api_t *api, imgcodec_params_t *p) {
    if (!api || !p) return -1;
    g_ic_api = api;

    if (p->cmd == IMGCODEC_CMD_INIT) {
        return 0;
    }

    if (p->cmd == IMGCODEC_CMD_FREE) {
        if (p->out_pixels && api->kfree) {
            api->kfree(p->out_pixels);
            p->out_pixels = NULL;
        }
        return 0;
    }

    if (p->cmd == IMGCODEC_CMD_DECODE) {
        if (!p->data || p->data_len == 0 || p->data_len > 8 * 1024 * 1024) return -1;

        p->out_format = detect_format(p->data, p->data_len);
        int w = 0, h = 0, comp = 0;
        uint8_t *rgba = NULL;
        bool is_webp = (p->out_format == IMGCODEC_FMT_WEBP);
        if (is_webp) {
            rgba = WebPDecodeRGBA(p->data, (size_t)p->data_len, &w, &h);
        } else {
            rgba = stbi_load_from_memory(p->data, (int)p->data_len, &w, &h, &comp, 4);
        }
        if (!rgba || w <= 0 || h <= 0 || w > 4096 || h > 4096) {
            if (rgba) {
                if (is_webp) WebPFree(rgba);
                else stbi_image_free(rgba);
            }
            return -2;
        }

        int dst_w = w;
        int dst_h = h;
        if (p->max_w > 0 && p->max_h > 0 && (w > p->max_w || h > p->max_h)) {
            if (w * p->max_h >= h * p->max_w) {
                dst_w = p->max_w;
                dst_h = (h * p->max_w) / w;
            } else {
                dst_h = p->max_h;
                dst_w = (w * p->max_h) / h;
            }
            if (dst_w < 1) dst_w = 1;
            if (dst_h < 1) dst_h = 1;
        }

        uint32_t *out = (uint32_t *)api->kmalloc((size_t)dst_w * (size_t)dst_h * sizeof(uint32_t));
        if (!out) {
            if (is_webp) WebPFree(rgba);
            else stbi_image_free(rgba);
            return -3;
        }

        if (dst_w == w && dst_h == h) {
            size_t total = (size_t)w * (size_t)h;
            for (size_t i = 0; i < total; i++) {
                uint8_t r = rgba[i * 4 + 0];
                uint8_t g = rgba[i * 4 + 1];
                uint8_t b = rgba[i * 4 + 2];
                uint8_t a = rgba[i * 4 + 3];
                out[i] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            }
        } else {
            // Downscale bilineare 16.16 fixed-point
            for (int y = 0; y < dst_h; y++) {
                int sy_fp = (y * (h - 1) * 256) / (dst_h > 1 ? (dst_h - 1) : 1);
                int sy0 = sy_fp >> 8;
                int fy = sy_fp & 0xFF;
                int sy1 = (sy0 + 1 < h) ? (sy0 + 1) : sy0;

                for (int x = 0; x < dst_w; x++) {
                    int sx_fp = (x * (w - 1) * 256) / (dst_w > 1 ? (dst_w - 1) : 1);
                    int sx0 = sx_fp >> 8;
                    int fx = sx_fp & 0xFF;
                    int sx1 = (sx0 + 1 < w) ? (sx0 + 1) : sx0;

                    const uint8_t *p00 = &rgba[(sy0 * w + sx0) * 4];
                    const uint8_t *p10 = &rgba[(sy0 * w + sx1) * 4];
                    const uint8_t *p01 = &rgba[(sy1 * w + sx0) * 4];
                    const uint8_t *p11 = &rgba[(sy1 * w + sx1) * 4];

                    int w00 = (256 - fx) * (256 - fy);
                    int w10 = fx * (256 - fy);
                    int w01 = (256 - fx) * fy;
                    int w11 = fx * fy;

                    uint32_t r = (uint32_t)((p00[0]*w00 + p10[0]*w10 + p01[0]*w01 + p11[0]*w11) >> 16);
                    uint32_t g = (uint32_t)((p00[1]*w00 + p10[1]*w10 + p01[1]*w01 + p11[1]*w11) >> 16);
                    uint32_t b = (uint32_t)((p00[2]*w00 + p10[2]*w10 + p01[2]*w01 + p11[2]*w11) >> 16);
                    uint32_t a = (uint32_t)((p00[3]*w00 + p10[3]*w10 + p01[3]*w01 + p11[3]*w11) >> 16);
                    out[y * dst_w + x] = (a << 24) | (r << 16) | (g << 8) | b;
                }
            }
        }

        if (is_webp) WebPFree(rgba);
        else stbi_image_free(rgba);
        p->out_pixels = out;
        p->out_w = dst_w;
        p->out_h = dst_h;
        return 0;
    }

    return -1;
}
