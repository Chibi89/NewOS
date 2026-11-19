#ifndef WEBBROWSER_IMGCODEC_H
#define WEBBROWSER_IMGCODEC_H

#include <types.h>
#include <gui/screen_mgr.h>
#include <screen_modules.h>

#define IMGCODEC_CMD_INIT   0
#define IMGCODEC_CMD_DECODE 1
#define IMGCODEC_CMD_FREE   2

#define IMGCODEC_FMT_UNKNOWN 0
#define IMGCODEC_FMT_JPEG    1
#define IMGCODEC_FMT_PNG     2
#define IMGCODEC_FMT_GIF     3
#define IMGCODEC_FMT_BMP     4
#define IMGCODEC_FMT_WEBP    5

typedef struct {
    int            cmd;
    const uint8_t *data;
    uint32_t       data_len;
    int            max_w;
    int            max_h;
    uint32_t      *out_pixels;
    int            out_w;
    int            out_h;
    int            out_format;
} imgcodec_params_t;

bool imgcodec_init(void);
bool imgcodec_decode(const uint8_t *data, uint32_t len, int max_w, int max_h,
                     uint32_t **out_pixels, int *out_w, int *out_h, int *out_format);
void imgcodec_free(uint32_t *pixels);

#endif
