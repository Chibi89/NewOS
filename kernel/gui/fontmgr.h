#ifndef GUI_FONTMGR_H
#define GUI_FONTMGR_H

#include <types.h>
#include <gui/screen_mgr.h>

#define FONTMGR_CMD_INIT          0
#define FONTMGR_CMD_GET_GLYPH     1
#define FONTMGR_CMD_GET_TTF_GLYPH 2
#define FONTMGR_CMD_SET_TTF       3

typedef struct {
    uint32_t cmd;
    uint32_t codepoint;
    uint8_t  glyph[16]; // Bitmap 8x16 (1 byte per riga)
    bool     found;
    int      pixel_height;
    uint8_t *ttf_bitmap;
    int      ttf_cap;
    int      ttf_w;
    int      ttf_h;
    int      ttf_xoff;
    int      ttf_yoff;
    int      ttf_advance;
    const char *ttf_path;
} fontmgr_params_t;

bool fontmgr_init(void);
bool fontmgr_get_glyph(uint32_t codepoint, uint8_t out_glyph[16]);
bool fontmgr_get_ttf_glyph(uint32_t codepoint, int pixel_height,
                           uint8_t *out_alpha, int max_bytes,
                           int *w, int *h, int *xoff, int *yoff, int *advance);
bool fontmgr_set_ttf(const char *path);

#endif
