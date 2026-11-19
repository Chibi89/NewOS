#ifndef WEBBROWSER_CSSENGINE_H
#define WEBBROWSER_CSSENGINE_H

#include <types.h>
#include <gui/screen_mgr.h>

#define CSS_CMD_INIT          0
#define CSS_CMD_PARSE_HTML    1
#define CSS_CMD_RESOLVE_TAG   2
#define CSS_CMD_PARSE_INLINE  3

#define CSS_DISPLAY_INLINE       0
#define CSS_DISPLAY_BLOCK        1
#define CSS_DISPLAY_FLEX         2
#define CSS_DISPLAY_NONE         3
#define CSS_DISPLAY_INLINE_BLOCK 4
#define CSS_DISPLAY_INLINE_FLEX  5
#define CSS_DISPLAY_GRID         6
#define CSS_DISPLAY_INLINE_GRID  7
#define CSS_DISPLAY_TABLE        8
#define CSS_DISPLAY_LIST_ITEM    9

#define CSS_ALIGN_LEFT        0
#define CSS_ALIGN_CENTER      1
#define CSS_ALIGN_RIGHT       2
#define CSS_ALIGN_JUSTIFY     3

#define CSS_POS_STATIC        0
#define CSS_POS_RELATIVE      1
#define CSS_POS_ABSOLUTE      2
#define CSS_POS_FIXED         3
#define CSS_POS_STICKY        4

#define CSS_FLOAT_NONE        0
#define CSS_FLOAT_LEFT        1
#define CSS_FLOAT_RIGHT       2

#define CSS_OVERFLOW_VISIBLE  0
#define CSS_OVERFLOW_HIDDEN   1
#define CSS_OVERFLOW_SCROLL   2
#define CSS_OVERFLOW_AUTO     3

#define CSS_TEXT_DECO_DEFAULT     0
#define CSS_TEXT_DECO_NONE        1
#define CSS_TEXT_DECO_UNDERLINE   2
#define CSS_TEXT_DECO_LINE_THRU   3
#define CSS_TEXT_DECO_OVERLINE    4

#define CSS_TEXT_TRANSFORM_NONE       0
#define CSS_TEXT_TRANSFORM_UPPERCASE  1
#define CSS_TEXT_TRANSFORM_LOWERCASE  2
#define CSS_TEXT_TRANSFORM_CAPITALIZE 3

#define CSS_FLEX_DIR_ROW        0
#define CSS_FLEX_DIR_COL        1

#define CSS_JUSTIFY_START       0
#define CSS_JUSTIFY_CENTER      1
#define CSS_JUSTIFY_END         2
#define CSS_JUSTIFY_BETWEEN     3

typedef struct {
    uint32_t color;
    uint32_t bg_color;
    uint32_t border_color;
    uint32_t outline_color;
    bool     has_color;
    bool     has_bg;
    bool     has_border;
    bool     has_outline;
    bool     has_shadow;
    bool     bg_transparent;
    uint8_t  display;        // CSS_DISPLAY_*
    uint8_t  text_align;     // CSS_ALIGN_*
    uint8_t  position;       // CSS_POS_*
    uint8_t  float_mode;     // CSS_FLOAT_*
    uint8_t  overflow;       // CSS_OVERFLOW_*
    uint8_t  text_deco;      // CSS_TEXT_DECO_*
    uint8_t  text_transform; // CSS_TEXT_TRANSFORM_*
    uint8_t  flex_direction; // CSS_FLEX_DIR_*
    uint8_t  justify_content;// CSS_JUSTIFY_*
    uint8_t  grid_cols;      // 0 = auto, >0 da grid-template-columns
    uint8_t  opacity;        // 0..255 (default 255)
    bool     bold;
    bool     italic;
    bool     no_underline;
    bool     nowrap;
    bool     flex_wrap;
    bool     box_sizing_border;
    bool     list_style_none;
    bool     cursor_pointer;
    int16_t  font_size;      // px (0 = default)
    int16_t  line_height;    // px (0 = default)
    int16_t  letter_spacing; // px
    int16_t  text_indent;    // px
    int16_t  width;
    int16_t  height;
    int16_t  min_width;
    int16_t  max_width;
    int16_t  border_width;
    int16_t  border_radius;  // px o 50 per 50%
    int16_t  margin_top;
    int16_t  margin_right;
    int16_t  margin_bottom;
    int16_t  margin_left;
    int16_t  padding_top;
    int16_t  padding_right;
    int16_t  padding_bottom;
    int16_t  padding_left;
    int16_t  gap;
    int16_t  z_index;
} css_style_t;

typedef struct {
    uint32_t    cmd;
    const char* html_or_css;
    const char* tag_name;
    const char* id_attr;
    const char* class_attr;
    const char* inline_style;
    void*       sheet_handle;
    css_style_t out_style;
} css_params_t;

bool css_engine_init(void);
void* css_parse_from_html(const char* html);
void css_free_sheet(void* sheet);
void css_resolve_style(void* sheet, const char* tag, const char* id_attr,
                       const char* class_attr, const char* inline_style,
                       css_style_t* out_style);

#endif
