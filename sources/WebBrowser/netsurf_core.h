#ifndef WEBBROWSER_NETSURF_CORE_H
#define WEBBROWSER_NETSURF_CORE_H

#include <types.h>
#include <gui/screen_mgr.h>

#define NS_CMD_INIT       0
#define NS_CMD_PARSE_HTML 1

typedef struct {
    uint32_t    cmd;
    const char* html_in;
    uint32_t    html_len;
    char*       out_html;      // HTML normalizzato prodotto dall'albero DOM HTML5 di LibHubbub
    uint32_t    out_html_cap;
    uint32_t    out_html_len;
    char        out_title[64];
    bool        has_title;
    uint32_t    node_count;
} netsurf_core_params_t;

bool netsurf_core_init(void);
bool netsurf_parse_html5(const char* html, uint32_t html_len,
                         char* out_html, uint32_t out_cap, uint32_t* out_len,
                         char* out_title, bool* has_title, uint32_t* node_count);

#endif
