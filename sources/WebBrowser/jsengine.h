#ifndef WEBBROWSER_JSENGINE_H
#define WEBBROWSER_JSENGINE_H

#include <types.h>
#include <gui/screen_mgr.h>

#define JS_CMD_INIT         0
#define JS_CMD_RUN_PAGE     1

typedef struct {
    uint32_t    cmd;
    const char* html_in;
    const char* current_url;
    char*       out_html;       // Buffer HTML generato da JS / DOM / SPA hydration
    uint32_t    out_html_cap;
    uint32_t    out_html_len;
    char        out_title[64];  // Eventuale document.title aggiornato da JS
    bool        has_title;
    uint32_t    scripts_run;
    uint32_t    dom_ops;
} js_params_t;

bool js_engine_init(void);
bool js_run_page_scripts(const char* html, const char* url,
                         char* out_html, uint32_t out_cap, uint32_t* out_len,
                         char* out_title, bool* has_title,
                         uint32_t* scripts_run, uint32_t* dom_ops);

#endif

