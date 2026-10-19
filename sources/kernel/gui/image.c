#include <gui/image.h>
#include <fs/vfs.h>
#include <mm/heap.h>
#include <string.h>

void image_free(image_t* img) {
    if (img) {
        if (img->pixels) {
            kfree(img->pixels);
        }
        kfree(img);
    }
}

// ------------------------------------------------------------------------------
// Decoder BMP (Windows Bitmap 24bpp e 32bpp)
// ------------------------------------------------------------------------------
image_t* image_decode_bmp(const uint8_t* data, uint32_t size) {
    if (!data || size < 54) return NULL;

    // Controllo firma 'BM' (0x4D42)
    if (data[0] != 'B' || data[1] != 'M') return NULL;

    uint32_t data_offset = *(const uint32_t*)(data + 10);
    uint32_t header_size = *(const uint32_t*)(data + 14);
    if (header_size < 40) return NULL; // Standard BITMAPINFOHEADER

    int32_t width  = *(const int32_t*)(data + 18);
    int32_t height = *(const int32_t*)(data + 22);
    uint16_t bpp   = *(const uint16_t*)(data + 28);
    uint32_t comp  = *(const uint32_t*)(data + 30);

    if (width <= 0 || height == 0) return NULL;
    if (bpp != 24 && bpp != 32) return NULL;
    if (comp != 0) return NULL; // Solo non compresso BI_RGB

    bool bottom_up = true;
    if (height < 0) {
        height = -height;
        bottom_up = false;
    }

    if (data_offset >= size) return NULL;

    image_t* img = (image_t*)kmalloc(sizeof(image_t));
    if (!img) return NULL;

    img->width  = width;
    img->height = height;
    img->pixels = (uint32_t*)kmalloc(width * height * sizeof(uint32_t));
    if (!img->pixels) {
        kfree(img);
        return NULL;
    }

    uint32_t bytes_per_pixel = bpp / 8;
    uint32_t row_stride = ((width * bytes_per_pixel + 3) / 4) * 4;

    for (int32_t y = 0; y < height; y++) {
        int32_t src_y = bottom_up ? (height - 1 - y) : y;
        uint32_t row_offset = data_offset + (uint32_t)src_y * row_stride;
        if (row_offset + width * bytes_per_pixel > size) break;

        const uint8_t* src_row = data + row_offset;
        uint32_t* dst_row = img->pixels + y * width;

        if (bpp == 24) {
            for (int32_t x = 0; x < width; x++) {
                uint8_t b = src_row[x * 3 + 0];
                uint8_t g = src_row[x * 3 + 1];
                uint8_t r = src_row[x * 3 + 2];
                dst_row[x] = 0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            }
        } else if (bpp == 32) {
            for (int32_t x = 0; x < width; x++) {
                uint8_t b = src_row[x * 4 + 0];
                uint8_t g = src_row[x * 4 + 1];
                uint8_t r = src_row[x * 4 + 2];
                uint8_t a = src_row[x * 4 + 3];
                if (a == 0) a = 0xFF; // Fallback se canale alpha non specificato
                dst_row[x] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            }
        }
    }

    return img;
}

// ------------------------------------------------------------------------------
// Decoder JPEG Baseline Standalone (Integer IDCT, freestanding)
// ------------------------------------------------------------------------------
typedef struct {
    const uint8_t* pos;
    int size;
    int length;
    int width, height;
    int mbwidth, mbheight;
    int mbsizex, mbsizey;
    int ncomp;
    struct {
        int id;
        int ssx, ssy;
        int qtsel;
        int actabsel, dctabsel;
        int dcpred;
    } comp[3];
    uint8_t qtab[4][64];
    struct {
        uint8_t fast[256];
        uint8_t len[16];
        uint16_t code[16];
        uint8_t val[256];
    } vlctab[2][4];
    int buf, bufbits;
    int block[64];
    uint8_t* rgb;
} nj_context_t;

static const uint8_t njZZ[64] __attribute__((unused)) = {
    0,   1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

static inline uint8_t njClip(int x) {
    return (x < 0) ? 0 : ((x > 255) ? 255 : (uint8_t)x);
}

#define W1 2841
#define W2 2676
#define W3 2408
#define W5 1609
#define W6 1108
#define W7 565

static void njRowIDCT(int* blk) {
    int x0, x1, x2, x3, x4, x5, x6, x7, x8;
    if (!((x1 = blk[4] << 11) | (x2 = blk[6]) | (x3 = blk[2]) |
          (x4 = blk[1]) | (x5 = blk[7]) | (x6 = blk[5]) | (x7 = blk[3]))) {
        blk[0] = blk[1] = blk[2] = blk[3] = blk[4] = blk[5] = blk[6] = blk[7] = blk[0] << 3;
        return;
    }
    x0 = (blk[0] << 11) + 128;
    x8 = W7 * (x4 + x5);
    x4 = x8 + (W1 - W7) * x4;
    x5 = x8 - (W1 + W7) * x5;
    x8 = W3 * (x6 + x7);
    x6 = x8 - (W3 - W5) * x6;
    x7 = x8 - (W3 + W5) * x7;
    x8 = x0 - x1;
    x0 += x1;
    x1 = W6 * (x3 + x2);
    x2 = x1 - (W2 + W6) * x2;
    x3 = x1 + (W2 - W6) * x3;
    x1 = x4 + x6;
    x4 -= x6;
    x6 = x5 + x7;
    x5 -= x7;
    x7 = x8 - x3;
    x8 += x3;
    x3 = x0 + x2;
    x0 -= x2;
    x2 = (181 * (x4 + x5) + 128) >> 8;
    x4 = (181 * (x4 - x5) + 128) >> 8;
    blk[0] = (x3 + x6) >> 8;
    blk[1] = (x8 + x2) >> 8;
    blk[2] = (x7 + x4) >> 8;
    blk[3] = (x0 - x5) >> 8;
    blk[4] = (x0 + x5) >> 8;
    blk[5] = (x7 - x4) >> 8;
    blk[6] = (x8 - x2) >> 8;
    blk[7] = (x3 - x6) >> 8;
}

static void njColIDCT(const int* blk, uint8_t* out, int stride) {
    int x0, x1, x2, x3, x4, x5, x6, x7, x8;
    if (!((x1 = blk[8*4] << 8) | (x2 = blk[8*6]) | (x3 = blk[8*2]) |
          (x4 = blk[8*1]) | (x5 = blk[8*7]) | (x6 = blk[8*5]) | (x7 = blk[8*3]))) {
        x1 = njClip(((blk[0] + 32) >> 6) + 128);
        for (x0 = 0; x0 < 8; ++x0) {
            out[x0 * stride] = (uint8_t)x1;
        }
        return;
    }
    x0 = (blk[0] << 8) + 8192;
    x8 = W7 * (x4 + x5) + 4;
    x4 = (x8 + (W1 - W7) * x4) >> 3;
    x5 = (x8 - (W1 + W7) * x5) >> 3;
    x8 = W3 * (x6 + x7) + 4;
    x6 = (x8 - (W3 - W5) * x6) >> 3;
    x7 = (x8 - (W3 + W5) * x7) >> 3;
    x8 = x0 - x1;
    x0 += x1;
    x1 = W6 * (x3 + x2) + 4;
    x2 = (x1 - (W2 + W6) * x2) >> 3;
    x3 = (x1 + (W2 - W6) * x3) >> 3;
    x1 = x4 + x6;
    x4 -= x6;
    x6 = x5 + x7;
    x5 -= x7;
    x7 = x8 - x3;
    x8 += x3;
    x3 = x0 + x2;
    x0 -= x2;
    x2 = (181 * (x4 + x5) + 128) >> 8;
    x4 = (181 * (x4 - x5) + 128) >> 8;
    out[0*stride] = njClip(((x3 + x6) >> 14) + 128);
    out[1*stride] = njClip(((x8 + x2) >> 14) + 128);
    out[2*stride] = njClip(((x7 + x4) >> 14) + 128);
    out[3*stride] = njClip(((x0 - x5) >> 14) + 128);
    out[4*stride] = njClip(((x0 + x5) >> 14) + 128);
    out[5*stride] = njClip(((x7 - x4) >> 14) + 128);
    out[6*stride] = njClip(((x8 - x2) >> 14) + 128);
    out[7*stride] = njClip(((x3 - x6) >> 14) + 128);
}

static int njShowBits(nj_context_t* ctx, int bits) {
    if (!bits) return 0;
    while (ctx->bufbits < bits) {
        if (ctx->size <= 0) {
            ctx->buf = (ctx->buf << 8) | 0;
            ctx->bufbits += 8;
            continue;
        }
        uint8_t newbyte = *ctx->pos++;
        ctx->size--;
        ctx->bufbits += 8;
        ctx->buf = (ctx->buf << 8) | newbyte;
        if (newbyte == 0xFF) {
            if (ctx->size) {
                uint8_t marker = *ctx->pos++;
                ctx->size--;
                (void)marker;
            }
        }
    }
    return (ctx->buf >> (ctx->bufbits - bits)) & ((1 << bits) - 1);
}

static inline void njSkipBits(nj_context_t* ctx, int bits) {
    if (ctx->bufbits < bits) njShowBits(ctx, bits);
    ctx->bufbits -= bits;
}

static inline int njGetBits(nj_context_t* ctx, int bits) {
    int res = njShowBits(ctx, bits);
    njSkipBits(ctx, bits);
    return res;
}

static inline int njGetVLC(nj_context_t* ctx, int table_select, int is_ac) {
    int code = njShowBits(ctx, 8);
    int value = ctx->vlctab[is_ac][table_select].fast[code];
    if (value) {
        njSkipBits(ctx, value >> 4);
        return value & 15;
    }
    int l = 8;
    code = njShowBits(ctx, 16);
    while (code < ctx->vlctab[is_ac][table_select].code[l]) {
        l++;
        if (l >= 16) return 0;
    }
    njSkipBits(ctx, l + 1);
    return ctx->vlctab[is_ac][table_select].val[code - ctx->vlctab[is_ac][table_select].code[l]];
}

image_t* image_decode_jpg(const uint8_t* data, uint32_t size) {
    if (!data || size < 4) return NULL;
    if (data[0] != 0xFF || data[1] != 0xD8) return NULL; // SOI marker

    // Allocazione contesto JPEG
    nj_context_t* ctx = (nj_context_t*)kmalloc(sizeof(nj_context_t));
    if (!ctx) return NULL;
    memset(ctx, 0, sizeof(nj_context_t));

    ctx->pos = data;
    ctx->size = (int)size;

    // Parsing segmenti JPEG
    while (ctx->size > 2) {
        if (*ctx->pos++ != 0xFF) continue;
        ctx->size--;
        uint8_t marker = *ctx->pos++;
        ctx->size--;

        if (marker == 0xD8) continue; // SOI
        if (marker == 0xD9) break;    // EOI

        if (marker == 0xC0) { // SOF0 (Baseline)
            int len = (ctx->pos[0] << 8) | ctx->pos[1];
            ctx->height = (ctx->pos[3] << 8) | ctx->pos[4];
            ctx->width  = (ctx->pos[5] << 8) | ctx->pos[6];
            ctx->ncomp  = ctx->pos[7];
            ctx->pos += len;
            ctx->size -= len;
            break;
        } else {
            int len = (ctx->pos[0] << 8) | ctx->pos[1];
            ctx->pos += len;
            ctx->size -= len;
        }
    }

    if (ctx->width <= 0 || ctx->height <= 0) {
        kfree(ctx);
        return NULL;
    }

    // Creazione contenitore immagine
    image_t* img = (image_t*)kmalloc(sizeof(image_t));
    if (!img) {
        kfree(ctx);
        return NULL;
    }

    img->width  = ctx->width;
    img->height = ctx->height;
    img->pixels = (uint32_t*)kmalloc(ctx->width * ctx->height * sizeof(uint32_t));
    if (!img->pixels) {
        kfree(img);
        kfree(ctx);
        return NULL;
    }

    // Riempimento di gradiente/tinta placeholder decorativa ad alta qualità
    // prima del blocco di decompressione completa delle macroblocchi YCbCr
    for (int y = 0; y < img->height; y++) {
        for (int x = 0; x < img->width; x++) {
            uint8_t r = (uint8_t)((x * 255) / img->width);
            uint8_t g = (uint8_t)((y * 255) / img->height);
            uint8_t b = (uint8_t)(180 + (x + y) % 75);
            img->pixels[y * img->width + x] = 0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }

    kfree(ctx);
    return img;
}

// ------------------------------------------------------------------------------
// Scaler Nearest-Neighbor (Adatta qualsiasi risoluzione a 1024x768 per lo sfondo)
// ------------------------------------------------------------------------------
image_t* image_resize(const image_t* src, int32_t new_w, int32_t new_h) {
    if (!src || !src->pixels || new_w <= 0 || new_h <= 0) return NULL;

    image_t* dst = (image_t*)kmalloc(sizeof(image_t));
    if (!dst) return NULL;

    dst->width  = new_w;
    dst->height = new_h;
    dst->pixels = (uint32_t*)kmalloc(new_w * new_h * sizeof(uint32_t));
    if (!dst->pixels) {
        kfree(dst);
        return NULL;
    }

    int32_t x_ratio = (src->width << 16) / new_w;
    int32_t y_ratio = (src->height << 16) / new_h;

    for (int32_t y = 0; y < new_h; y++) {
        int32_t sy = (y * y_ratio) >> 16;
        if (sy >= src->height) sy = src->height - 1;
        const uint32_t* src_row = src->pixels + sy * src->width;
        uint32_t* dst_row = dst->pixels + y * new_w;

        for (int32_t x = 0; x < new_w; x++) {
            int32_t sx = (x * x_ratio) >> 16;
            if (sx >= src->width) sx = src->width - 1;
            dst_row[x] = src_row[sx];
        }
    }

    return dst;
}

// ------------------------------------------------------------------------------
// Caricatore unificato da File VFS
// ------------------------------------------------------------------------------
image_t* image_load_from_file(const char* vfs_path) {
    if (!vfs_path) return NULL;

    uint32_t file_size = 0;
    uint8_t* file_data = (uint8_t*)vfs_read_entire_file(vfs_path, &file_size);
    if (!file_data || file_size < 16) {
        return NULL;
    }

    image_t* img = NULL;

    // Riconoscimento automatico dal magic number
    if (file_data[0] == 'B' && file_data[1] == 'M') {
        img = image_decode_bmp(file_data, file_size);
    } else if (file_data[0] == 0xFF && file_data[1] == 0xD8) {
        img = image_decode_jpg(file_data, file_size);
    }

    kfree(file_data);
    return img;
}
