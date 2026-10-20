/**
 * WebBrowser/webbrowser.c
 * NewSoft Web Browser per NewOS V1 (64-bit)
 * (C) Copyright Newspoint Software - NewSoft - NewOS V1
 * @credits: Basato sui principi e architettura del motore NetSurf (C89/C99)
 * @thanks: NetSurf Project (http://www.netsurf-browser.org/)
 */

#include "webbrowser.h"
#include <gui/wm.h>
#include <gui/graphics.h>
#include <gui/icons.h>
#include <fs/vfs.h>
#include <net/socket.h>
#include <net/dns.h>
#include <drivers/net/net_nic.h>
#include <drivers/timer.h>
#include <mm/heap.h>
#include <string.h>
#include <arch/x86_64/io.h>

#define MAX_HISTORY     8
#define MAX_URL_LEN     256
#define MAX_TITLE_LEN   64
#define MAX_STATUS_LEN  96
#define MAX_HTML_SIZE   16384
#define MAX_ELEMENTS    128

typedef enum {
    ELEM_TEXT = 0,
    ELEM_H1,
    ELEM_H2,
    ELEM_H3,
    ELEM_LINK,
    ELEM_HR,
    ELEM_LIST_ITEM,
    ELEM_PRE,
    ELEM_BLOCKQUOTE,
    ELEM_HEADER,
    ELEM_NAV,
    ELEM_VIDEO,
    ELEM_BUTTON
} elem_type_t;

typedef struct {
    elem_type_t type;
    char        text[120];
    char        href[128];
    uint32_t    color;
    int32_t     x;
    int32_t     y;
    int32_t     w;
    int32_t     h;
} browser_elem_t;

typedef struct {
    char           current_url[MAX_URL_LEN];
    char           edit_url[MAX_URL_LEN];
    char           page_title[MAX_TITLE_LEN];
    char           status_msg[MAX_STATUS_LEN];
    bool           url_focused;
    int            cursor_pos;

    // Cronologia di navigazione
    char           history_back[MAX_HISTORY][MAX_URL_LEN];
    int            history_back_count;
    char           history_fwd[MAX_HISTORY][MAX_URL_LEN];
    int            history_fwd_count;

    // Buffer pagina
    char           raw_html[MAX_HTML_SIZE];
    uint32_t       raw_html_len;

    // Elementi di layout calcolati
    browser_elem_t elems[MAX_ELEMENTS];
    int            num_elems;
    int32_t        content_height;
    int32_t        scroll_y;
    int            hover_elem;

    // Stato UI
    bool           is_loading;
    bool           is_secure_local;
    int            quick_fav_hover;
    bool           is_offline;
} browser_state_t;

static void serial_log(const char* s) {
    if (!s) return;
    while (*s) outb(0x3F8, *s++);
}

// Built-in Home Page HTML (Fallback compatto)
static const char* g_builtin_home_html =
"<html><title>Home</title><body><h1>NewSoft Web Browser</h1>"
"<p><a href=\"http://youtube.it\">YouTube</a> | "
"<a href=\"about:network\">Rete</a> | "
"<a href=\"about:browser\">Info</a></p></body></html>";

static const char* g_builtin_about_html =
"<html><title>Info</title><body><h1>NewSoft Web Browser</h1>"
"<p>NetSurf (C89/C99)</p>"
"<p><a href=\"about:home\">Home</a></p></body></html>";

// Funzioni di utilità per il parsing HTML
static int browser_strncasecmp(const char* s1, const char* s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        char c1 = s1[i];
        char c2 = s2[i];
        if (c1 >= 'a' && c1 <= 'z') c1 -= 32;
        if (c2 >= 'a' && c2 <= 'z') c2 -= 32;
        if (c1 != c2 || c1 == '\0') return (unsigned char)c1 - (unsigned char)c2;
    }
    return 0;
}
static void str_strip_tags(char* s) {
    if (!s) return;
    char* src = s;
    char* dst = s;
    bool in_tag = false;
    while (*src) {
        if (*src == '<') in_tag = true;
        else if (*src == '>') in_tag = false;
        else if (!in_tag) *dst++ = *src;
        src++;
    }
    *dst = '\0';
}

static void extract_attribute(const char* tag_start, const char* attr_name, char* out, size_t max_len) {
    out[0] = '\0';
    if (!tag_start || !attr_name) return;

    const char* p = strstr(tag_start, attr_name);
    if (!p) return;

    p += strlen(attr_name);
    while (*p == ' ' || *p == '=') p++;

    char quote = 0;
    if (*p == '"' || *p == '\'') {
        quote = *p;
        p++;
    }

    size_t i = 0;
    while (*p && i < max_len - 1) {
        if (quote && *p == quote) break;
        if (!quote && (*p == ' ' || *p == '>')) break;
        out[i++] = *p++;
    }
    out[i] = '\0';
}

static uint32_t parse_hex_color(const char* s) {
    if (!s || !*s) return 0;
    if (s[0] == '#') {
        uint32_t val = 0;
        for (int i = 1; i <= 6 && s[i]; i++) {
            char c = s[i];
            uint32_t nibble = 0;
            if (c >= '0' && c <= '9') nibble = c - '0';
            else if (c >= 'a' && c <= 'f') nibble = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') nibble = c - 'A' + 10;
            else break;
            val = (val << 4) | nibble;
        }
        return val;
    }
    if (browser_strncasecmp(s, "red", 3) == 0) return 0xCC181E;
    if (browser_strncasecmp(s, "blue", 4) == 0) return 0x0000CC;
    if (browser_strncasecmp(s, "green", 5) == 0) return 0x008000;
    if (browser_strncasecmp(s, "gray", 4) == 0) return 0x606060;
    if (browser_strncasecmp(s, "black", 5) == 0) return 0x000000;
    return 0;
}

// Genera una pagina dinamica con lo stato della rete (about:network)
static void generate_network_page(browser_state_t* state) {
    net_nic_t* nic = net_nic_get_active();
    char ip_s[20] = "0.0.0.0", mask_s[20] = "0.0.0.0", gw_s[20] = "0.0.0.0", dns_s[20] = "0.0.0.0", mac_s[24] = "00:00:00:00:00:00";

    if (nic && (nic->link_up || nic->ip != 0)) {
        net_format_ip(nic->ip, ip_s, sizeof(ip_s));
        net_format_ip(nic->netmask, mask_s, sizeof(mask_s));
        net_format_ip(nic->gateway, gw_s, sizeof(gw_s));
        net_format_ip(nic->dns, dns_s, sizeof(dns_s));
        net_format_mac(nic->mac, mac_s, sizeof(mac_s));
    }

    mini_snprintf(state->raw_html, sizeof(state->raw_html),
        "<html><head><title>Rete</title></head><body>"
        "<h1>Diagnostica Rete</h1><hr><ul>"
        "<li><b>NIC:</b> %s</li><li><b>MAC:</b> %s</li>"
        "<li><b>IP:</b> %s</li><li><b>Subnet:</b> %s</li>"
        "<li><b>GW:</b> %s</li><li><b>DNS:</b> %s</li>"
        "</ul><hr><p><a href=\"about:home\">&lt;&lt; Home</a></p></body></html>",
        (nic && nic->name[0]) ? nic->name : "E1000",
        mac_s, ip_s, mask_s, gw_s, dns_s);

    state->raw_html_len = (uint32_t)strlen(state->raw_html);
}

// Genera una pagina con il riepilogo delle regole di sistema (about:rules)
static void generate_rules_page(browser_state_t* state) {
    mini_snprintf(state->raw_html, sizeof(state->raw_html),
        "<html><head><title>Regole</title></head><body>"
        "<h1>Regole NewOS V1</h1><hr><ul>"
        "<li>01: File System C:\\NewOS e C:\\ProgramFile</li>"
        "<li>02: Registro AES-256</li>"
        "<li>03: Policy Installatori</li>"
        "<li>04: Privilegi Utente</li>"
        "<li>05: Rete e Priorita' 95.141.32.1</li>"
        "</ul><hr><p><a href=\"about:home\">&lt;&lt; Home</a></p></body></html>");
    state->raw_html_len = (uint32_t)strlen(state->raw_html);
}

// Genera una pagina di errore HTTP o di connessione
static void generate_error_page(browser_state_t* state, const char* title, const char* msg, const char* target) {
    mini_snprintf(state->raw_html, sizeof(state->raw_html),
        "<html><title>%s</title><body><h1>%s</h1><p>%s</p><p>%s</p>"
        "<p><a href=\"about:home\">Home</a></p></body></html>",
        title, title, msg, target ? target : "");
    state->raw_html_len = (uint32_t)strlen(state->raw_html);
}

// Motore di parsing e formattazione HTML
static void browser_parse_html(browser_state_t* state, int32_t viewport_w) {
    state->num_elems = 0;
    state->content_height = 0;
    state->scroll_y = 0;
    state->hover_elem = -1;

    // Titolo predefinito
    strcpy(state->page_title, "NewSoft Web Browser");

    const char* p = state->raw_html;
    if (!p || *p == '\0') return;

    // 1. Estrazione del tag <title>...</title>
    const char* t_start = strstr(p, "<title>");
    if (!t_start) t_start = strstr(p, "<TITLE>");
    if (t_start) {
        t_start += 7;
        const char* t_end = strstr(t_start, "</title>");
        if (!t_end) t_end = strstr(t_start, "</TITLE>");
        if (t_end) {
            size_t t_len = t_end - t_start;
            if (t_len >= sizeof(state->page_title)) t_len = sizeof(state->page_title) - 1;
            strncpy(state->page_title, t_start, t_len);
            state->page_title[t_len] = '\0';
        }
    }

    // 2. Tokenizzazione del body
    const char* body = strstr(p, "<body");
    if (!body) body = strstr(p, "<BODY");
    if (body) {
        while (*body && *body != '>') body++;
        if (*body == '>') body++;
    } else {
        body = p;
    }

    int32_t cur_y = 10;
    int32_t max_text_chars = (viewport_w - 40) / 8;
    if (max_text_chars < 20) max_text_chars = 20;

    const char* ptr = body;
    char text_buf[512];
    int  text_pos = 0;

    elem_type_t cur_type = ELEM_TEXT;
    char        cur_href[128] = "";
    uint32_t    cur_color = 0;

    // Macro interna per inserire righe formattate
    while (*ptr && state->num_elems < MAX_ELEMENTS - 4) {
        if (*ptr == '<') {
            // Se c'era del testo accumulato prima del tag, esegui il flush
            if (text_pos > 0) {
                text_buf[text_pos] = '\0';
                // Esegui il wrapping del testo accumulato
                int start_i = 0;
                while (start_i < text_pos && state->num_elems < MAX_ELEMENTS - 2) {
                    int len = text_pos - start_i;
                    if (len > max_text_chars) {
                        int split_at = start_i + max_text_chars;
                        while (split_at > start_i && text_buf[split_at] != ' ') split_at--;
                        if (split_at == start_i) split_at = start_i + max_text_chars;
                        len = split_at - start_i;
                    }

                    browser_elem_t* el = &state->elems[state->num_elems++];
                    el->type = cur_type;
                    strncpy(el->text, text_buf + start_i, len);
                    el->text[len] = '\0';
                    strncpy(el->href, cur_href, sizeof(el->href) - 1);
                    el->color = cur_color;
                    el->x = (cur_type == ELEM_LIST_ITEM) ? 26 : 12;
                    el->y = cur_y;
                    el->w = len * 8;
                    el->h = (cur_type == ELEM_H1) ? 22 : ((cur_type == ELEM_H2) ? 18 : 16);

                    cur_y += el->h + 2;
                    start_i += len;
                    while (start_i < text_pos && text_buf[start_i] == ' ') start_i++;
                }
                text_pos = 0;
            }

            // Analizza il tag
            const char* tag_start = ptr + 1;
            while (*ptr && *ptr != '>') ptr++;
            if (*ptr == '>') ptr++;

            if (browser_strncasecmp(tag_start, "h1", 2) == 0) {
                cur_type = ELEM_H1;
                cur_y += 6;
            } else if (browser_strncasecmp(tag_start, "/h1", 3) == 0) {
                cur_type = ELEM_TEXT;
                cur_y += 6;
            } else if (browser_strncasecmp(tag_start, "h2", 2) == 0) {
                cur_type = ELEM_H2;
                cur_y += 4;
            } else if (browser_strncasecmp(tag_start, "/h2", 3) == 0) {
                cur_type = ELEM_TEXT;
                cur_y += 4;
            } else if (browser_strncasecmp(tag_start, "h3", 2) == 0) {
                cur_type = ELEM_H3;
                cur_y += 2;
            } else if (browser_strncasecmp(tag_start, "/h3", 3) == 0) {
                cur_type = ELEM_TEXT;
                cur_y += 2;
            } else if (browser_strncasecmp(tag_start, "hr", 2) == 0) {
                browser_elem_t* el = &state->elems[state->num_elems++];
                el->type = ELEM_HR;
                el->text[0] = '\0';
                el->href[0] = '\0';
                el->color = 0;
                el->x = 10;
                el->y = cur_y;
                el->w = viewport_w - 36;
                el->h = 6;
                cur_y += 10;
            } else if (browser_strncasecmp(tag_start, "br", 2) == 0) {
                cur_y += 14;
            } else if (browser_strncasecmp(tag_start, "p", 1) == 0 && (tag_start[1] == ' ' || tag_start[1] == '>')) {
                cur_y += 6;
                cur_type = ELEM_TEXT;
            } else if (browser_strncasecmp(tag_start, "/p", 2) == 0) {
                cur_y += 4;
                cur_type = ELEM_TEXT;
            } else if (browser_strncasecmp(tag_start, "li", 2) == 0) {
                cur_type = ELEM_LIST_ITEM;
            } else if (browser_strncasecmp(tag_start, "/li", 3) == 0) {
                cur_type = ELEM_TEXT;
            } else if (browser_strncasecmp(tag_start, "a ", 2) == 0) {
                cur_type = ELEM_LINK;
                extract_attribute(tag_start, "href", cur_href, sizeof(cur_href));
            } else if (browser_strncasecmp(tag_start, "/a", 2) == 0) {
                cur_type = ELEM_TEXT;
                cur_href[0] = '\0';
            } else if (browser_strncasecmp(tag_start, "pre", 3) == 0) {
                cur_type = ELEM_PRE;
                cur_y += 4;
            } else if (browser_strncasecmp(tag_start, "/pre", 4) == 0) {
                cur_type = ELEM_TEXT;
                cur_y += 4;
            } else if (browser_strncasecmp(tag_start, "font ", 5) == 0) {
                char cbuf[32] = "";
                extract_attribute(tag_start, "color", cbuf, sizeof(cbuf));
                if (cbuf[0]) cur_color = parse_hex_color(cbuf);
            } else if (browser_strncasecmp(tag_start, "/font", 5) == 0) {
                cur_color = 0;
            } else if (browser_strncasecmp(tag_start, "video", 5) == 0) {
                browser_elem_t* el = &state->elems[state->num_elems++];
                el->type = ELEM_VIDEO;
                el->text[0] = '\0';
                el->href[0] = '\0';
                el->color = 0;
                el->x = 16;
                el->y = cur_y;
                el->w = (viewport_w > 540) ? 500 : (viewport_w - 40);
                el->h = 150;
                cur_y += el->h + 10;
            } else if (browser_strncasecmp(tag_start, "button", 6) == 0) {
                cur_type = ELEM_BUTTON;
            } else if (browser_strncasecmp(tag_start, "/button", 7) == 0) {
                cur_type = ELEM_TEXT;
            } else if (browser_strncasecmp(tag_start, "header", 6) == 0 ||
                       browser_strncasecmp(tag_start, "/header", 7) == 0 ||
                       browser_strncasecmp(tag_start, "nav", 3) == 0 ||
                       browser_strncasecmp(tag_start, "/nav", 4) == 0 ||
                       browser_strncasecmp(tag_start, "section", 7) == 0 ||
                       browser_strncasecmp(tag_start, "/section", 8) == 0 ||
                       browser_strncasecmp(tag_start, "article", 7) == 0 ||
                       browser_strncasecmp(tag_start, "/article", 8) == 0 ||
                       browser_strncasecmp(tag_start, "main", 4) == 0 ||
                       browser_strncasecmp(tag_start, "/main", 5) == 0 ||
                       browser_strncasecmp(tag_start, "footer", 6) == 0 ||
                       browser_strncasecmp(tag_start, "/footer", 7) == 0) {
                cur_y += 4;
            }
            continue;
        }

        // Gestione entita' HTML
        if (*ptr == '&') {
            if (browser_strncasecmp(ptr, "&nbsp;", 6) == 0) {
                if (text_pos < (int)sizeof(text_buf) - 1) text_buf[text_pos++] = ' ';
                ptr += 6;
                continue;
            } else if (browser_strncasecmp(ptr, "&lt;", 4) == 0) {
                if (text_pos < (int)sizeof(text_buf) - 1) text_buf[text_pos++] = '<';
                ptr += 4;
                continue;
            } else if (browser_strncasecmp(ptr, "&gt;", 4) == 0) {
                if (text_pos < (int)sizeof(text_buf) - 1) text_buf[text_pos++] = '>';
                ptr += 4;
                continue;
            } else if (browser_strncasecmp(ptr, "&amp;", 5) == 0) {
                if (text_pos < (int)sizeof(text_buf) - 1) text_buf[text_pos++] = '&';
                ptr += 5;
                continue;
            } else if (browser_strncasecmp(ptr, "&quot;", 6) == 0) {
                if (text_pos < (int)sizeof(text_buf) - 1) text_buf[text_pos++] = '"';
                ptr += 6;
                continue;
            }
        }

        // Carattere di testo normale
        if (*ptr == '\n' || *ptr == '\r') {
            if (cur_type == ELEM_PRE) {
                // Nel blocco preformattato la newline va a capo
                if (text_pos > 0) {
                    text_buf[text_pos] = '\0';
                    browser_elem_t* el = &state->elems[state->num_elems++];
                    el->type = ELEM_PRE;
                    strncpy(el->text, text_buf, sizeof(el->text) - 1);
                    el->href[0] = '\0';
                    el->x = 16;
                    el->y = cur_y;
                    el->w = text_pos * 8;
                    el->h = 16;
                    cur_y += 18;
                    text_pos = 0;
                }
            } else if (text_pos > 0 && text_buf[text_pos - 1] != ' ') {
                if (text_pos < (int)sizeof(text_buf) - 1) text_buf[text_pos++] = ' ';
            }
            ptr++;
            continue;
        }

        // Compattamento spazi multipli fuori da <pre>
        if (*ptr == ' ' && cur_type != ELEM_PRE && text_pos > 0 && text_buf[text_pos - 1] == ' ') {
            ptr++;
            continue;
        }

        if (text_pos < (int)sizeof(text_buf) - 1) {
            text_buf[text_pos++] = *ptr;
        }
        ptr++;
    }

    // Flush finale residuo
    if (text_pos > 0 && state->num_elems < MAX_ELEMENTS) {
        text_buf[text_pos] = '\0';
        browser_elem_t* el = &state->elems[state->num_elems++];
        el->type = cur_type;
        strncpy(el->text, text_buf, sizeof(el->text) - 1);
        strncpy(el->href, cur_href, sizeof(el->href) - 1);
        el->x = 12;
        el->y = cur_y;
        el->w = text_pos * 8;
        el->h = 16;
        cur_y += el->h + 4;
    }

    state->content_height = cur_y + 20;
}

// Funzione di caricamento URL (about:, file:/// o http://)
static void browser_load_url(browser_state_t* state, const char* url, int32_t viewport_w) {
    if (!url || url[0] == '\0') url = BROWSER_DEFAULT_HOME;

    serial_log("[BROWSER] Caricamento URL: ");
    serial_log(url);
    serial_log("\n");

    strncpy(state->current_url, url, sizeof(state->current_url) - 1);
    strncpy(state->edit_url, url, sizeof(state->edit_url) - 1);
    state->is_loading = true;
    state->is_secure_local = false;
    state->is_offline = false;
    mini_snprintf(state->status_msg, sizeof(state->status_msg), "Connessione in corso a %s...", url);

    // 1. Schemi interni about:
    if (strncmp(url, "about:", 6) == 0) {
        state->is_secure_local = true;
        const char* page = url + 6;
        if (strcmp(page, "network") == 0) {
            generate_network_page(state);
        } else if (strcmp(page, "rules") == 0) {
            generate_rules_page(state);
        } else if (strcmp(page, "browser") == 0 || strcmp(page, "info") == 0) {
            strncpy(state->raw_html, g_builtin_about_html, sizeof(state->raw_html) - 1);
            state->raw_html_len = (uint32_t)strlen(state->raw_html);
        } else { // about:home o default
            // Prova a caricare C:\NewOS\Resources\Web\HOME.HTM se presente
            uint32_t fsz = 0;
            char* disk_home = (char*)vfs_read_entire_file("C:\\NewOS\\Resources\\Web\\HOME.HTM", &fsz);
            if (!disk_home) disk_home = (char*)vfs_read_entire_file("/NewOS/Resources/Web/HOME.HTM", &fsz);
            if (!disk_home) disk_home = (char*)vfs_read_entire_file("WebBrowser/home.htm", &fsz);

            if (disk_home && fsz > 0) {
                if (fsz >= sizeof(state->raw_html)) fsz = sizeof(state->raw_html) - 1;
                memcpy(state->raw_html, disk_home, fsz);
                state->raw_html[fsz] = '\0';
                state->raw_html_len = fsz;
                kfree(disk_home);
            } else {
                strncpy(state->raw_html, g_builtin_home_html, sizeof(state->raw_html) - 1);
                state->raw_html_len = (uint32_t)strlen(state->raw_html);
            }
        }

        browser_parse_html(state, viewport_w);
        state->is_loading = false;
        mini_snprintf(state->status_msg, sizeof(state->status_msg), "Pronto");
        return;
    }

    // 2. File locali file:/// oppure percorsi Windows C:
    if (strncmp(url, "file:///", 8) == 0 || (url[1] == ':' && (url[2] == '\\' || url[2] == '/'))) {
        state->is_secure_local = true;
        const char* fpath = url;
        if (strncmp(url, "file:///", 8) == 0) fpath = url + 8;

        uint32_t fsz = 0;
        char* file_data = (char*)vfs_read_entire_file(fpath, &fsz);
        if (file_data && fsz > 0) {
            if (fsz >= sizeof(state->raw_html)) fsz = sizeof(state->raw_html) - 1;
            memcpy(state->raw_html, file_data, fsz);
            state->raw_html[fsz] = '\0';
            state->raw_html_len = fsz;
            kfree(file_data);

            // Se non e' HTML ma testo semplice, impacchetta in <pre>
            if (!strstr(state->raw_html, "<html") && !strstr(state->raw_html, "<HTML")) {
                char temp[MAX_HTML_SIZE];
                mini_snprintf(temp, sizeof(temp),
                    "<html><head><title>%s</title></head>"
                    "<body bgcolor=\"#FFFFFF\"><pre>%s</pre></body></html>",
                    fpath, state->raw_html);
                strncpy(state->raw_html, temp, sizeof(state->raw_html) - 1);
                state->raw_html_len = (uint32_t)strlen(state->raw_html);
            }

            browser_parse_html(state, viewport_w);
            state->is_loading = false;
            mini_snprintf(state->status_msg, sizeof(state->status_msg), "File caricato");
        } else {
            generate_error_page(state, "404", "File non trovato", fpath);
            browser_parse_html(state, viewport_w);
            state->is_loading = false;
            mini_snprintf(state->status_msg, sizeof(state->status_msg), "Non trovato");
        }
        return;
    }

    // 2.5 Gestione diretta URL YouTube (youtube.it, www.youtube.it, youtube.com)
    if (strstr(url, "youtube") != NULL) {
        state->is_secure_local = false;
        uint32_t fsz = 0;
        char* file_data = (char*)vfs_read_entire_file("C:\\NewOS\\Resources\\Web\\YOUTUBE.HTM", &fsz);
        if (!file_data) file_data = (char*)vfs_read_entire_file("/SYSTEM/WEB/YOUTUBE.HTM", &fsz);
        if (!file_data) file_data = (char*)vfs_read_entire_file("WebBrowser/youtube.htm", &fsz);

        if (file_data && fsz > 0) {
            if (fsz >= sizeof(state->raw_html)) fsz = sizeof(state->raw_html) - 1;
            memcpy(state->raw_html, file_data, fsz);
            state->raw_html[fsz] = '\0';
            state->raw_html_len = fsz;
            kfree(file_data);
        } else {
            mini_snprintf(state->raw_html, sizeof(state->raw_html),
                "<html><title>YouTube</title><body>"
                "<header><h1><font color=\"#CC181E\">> YouTube</font> IT</h1>"
                "<p><button>Cerca</button></p></header><hr>"
                "<main><article><video width=\"520\" height=\"150\"></video>"
                "<h2>YouTube HTML5</h2>"
                "<p>Canale: <font color=\"#0000CC\">NewSoft</font></p>"
                "</article></main></body></html>");
            state->raw_html_len = (uint32_t)strlen(state->raw_html);
        }

        strncpy(state->current_url, "http://www.youtube.it/", sizeof(state->current_url) - 1);
        strncpy(state->edit_url, "http://www.youtube.it/", sizeof(state->edit_url) - 1);
        browser_parse_html(state, viewport_w);
        state->is_loading = false;
        mini_snprintf(state->status_msg, sizeof(state->status_msg), "YouTube Italia caricato con successo (HTML5)");
        return;
    }

    // 3. Connessione remota HTTP (porta 80 standard)
    const char* http_prefix = "http://";
    const char* host_start = url;
    if (strncmp(url, http_prefix, 7) == 0) {
        host_start = url + 7;
    }

    char host_str[64] = "";
    char path_str[128] = "/";
    uint16_t port = 80;

    const char* slash = strchr(host_start, '/');
    if (slash) {
        size_t hlen = slash - host_start;
        if (hlen >= sizeof(host_str)) hlen = sizeof(host_str) - 1;
        strncpy(host_str, host_start, hlen);
        host_str[hlen] = '\0';
        strncpy(path_str, slash, sizeof(path_str) - 1);
    } else {
        strncpy(host_str, host_start, sizeof(host_str) - 1);
    }

    char* colon = strchr(host_str, ':');
    if (colon) {
        *colon = '\0';
        // Port parsing semplice
        port = (uint16_t)net_parse_ip(colon + 1); // fallback o porta base
        if (port == 0) port = 80;
    }

    // Risoluzione indirizzo IP target tramite servizio DNS interno (RFC 1035)
    mini_snprintf(state->status_msg, sizeof(state->status_msg), "DNS: %s", host_str);
    uint32_t target_ip = dns_resolve(host_str);

    if (target_ip == 0) {
        generate_error_page(state, "DNS", "Host non risolto", host_str);
        browser_parse_html(state, viewport_w);
        state->is_loading = false;
        mini_snprintf(state->status_msg, sizeof(state->status_msg), "Errore DNS");
        return;
    }

    mini_snprintf(state->status_msg, sizeof(state->status_msg), "Connessione: %s", host_str);

    int sock = net_socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        generate_error_page(state, "Socket", "Errore socket", host_str);
        browser_parse_html(state, viewport_w);
        state->is_loading = false;
        return;
    }

    sockaddr_in_t saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = net_htons(port);
    saddr.sin_addr = target_ip;

    if (net_connect(sock, &saddr) != 0) {
        net_close(sock);
        generate_error_page(state, "TCP", "Connessione fallita", host_str);
        browser_parse_html(state, viewport_w);
        state->is_loading = false;
        mini_snprintf(state->status_msg, sizeof(state->status_msg), "TCP fallito");
        return;
    }

    // Invio richiesta HTTP GET 1.0 standard
    char req[256];
    mini_snprintf(req, sizeof(req), "GET %s HTTP/1.0\r\nHost: %s\r\n\r\n", path_str, host_str);

    net_send(sock, req, strlen(req), 0);
    mini_snprintf(state->status_msg, sizeof(state->status_msg), "In attesa...");

    // Ricezione risposta HTTP con timeout
    uint32_t rx_total = 0;
    int wait_loops = 0;
    while (wait_loops < 40 && rx_total < sizeof(state->raw_html) - 1) {
        net_poll();
        int r = net_recv(sock, state->raw_html + rx_total, sizeof(state->raw_html) - 1 - rx_total, 0);
        if (r > 0) {
            rx_total += (uint32_t)r;
            wait_loops = 0; // Reset timeout se stiamo ricevendo dati
        } else {
            timer_sleep(10);
            wait_loops++;
        }
    }
    net_close(sock);

    if (rx_total > 0) {
        state->raw_html[rx_total] = '\0';
        state->raw_html_len = rx_total;

        // Estrazione header HTTP (trova \r\n\r\n)
        char* body_start = strstr(state->raw_html, "\r\n\r\n");
        if (body_start) {
            body_start += 4;
            // Sposta il payload HTML all'inizio del buffer
            size_t body_len = strlen(body_start);
            memmove(state->raw_html, body_start, body_len + 1);
            state->raw_html_len = (uint32_t)body_len;
        }

        browser_parse_html(state, viewport_w);
        state->is_loading = false;
        mini_snprintf(state->status_msg, sizeof(state->status_msg), "Caricato (%u B)", rx_total);
    } else {
        generate_error_page(state, "HTTP", "Nessun dato", host_str);
        browser_parse_html(state, viewport_w);
        state->is_loading = false;
        mini_snprintf(state->status_msg, sizeof(state->status_msg), "Timeout dati");
    }
}

// Navigazione a un URL con aggiornamento dello stack storico
static void browser_navigate(window_t* win, const char* target_url, bool push_history) {
    browser_state_t* state = (browser_state_t*)win->app_data;
    if (!state || !target_url) return;

    // Normalizzazione URL: pulizia spazi e auto-prefisso http:// se mancante
    char norm_url[MAX_URL_LEN];
    const char* u = target_url;
    while (*u == ' ') u++;
    if (*u == '\0') u = BROWSER_DEFAULT_HOME;

    if (strncmp(u, "http://", 7) == 0 ||
        strncmp(u, "https://", 8) == 0 ||
        strncmp(u, "about:", 6) == 0 ||
        strncmp(u, "file:///", 8) == 0 ||
        (u[1] == ':' && (u[2] == '\\' || u[2] == '/'))) {
        strncpy(norm_url, u, sizeof(norm_url) - 1);
    } else {
        mini_snprintf(norm_url, sizeof(norm_url), "http://%s", u);
    }
    norm_url[sizeof(norm_url) - 1] = '\0';

    if (push_history && state->current_url[0] != '\0') {
        if (state->history_back_count < MAX_HISTORY) {
            strncpy(state->history_back[state->history_back_count++], state->current_url, MAX_URL_LEN - 1);
        } else {
            for (int i = 0; i < MAX_HISTORY - 1; i++) {
                strcpy(state->history_back[i], state->history_back[i + 1]);
            }
            strncpy(state->history_back[MAX_HISTORY - 1], state->current_url, MAX_URL_LEN - 1);
        }
        state->history_fwd_count = 0;
    }

    int32_t vp_w = win->width - 12;
    browser_load_url(state, norm_url, vp_w);

    // Aggiorna titolo della finestra del Window Manager
    if (strstr(state->page_title, "Web Browser")) {
        strncpy(win->title, state->page_title, sizeof(win->title) - 1);
    } else {
        mini_snprintf(win->title, sizeof(win->title), "%s - NewSoft Web Browser", state->page_title);
    }
}

// Rendering dell'interfaccia grafica del Web Browser
static void browser_draw(window_t* win) {
    browser_state_t* state = (browser_state_t*)win->app_data;
    if (!state) return;

    int32_t x = win->x;
    int32_t y = win->y;
    int32_t w = win->width;
    int32_t h = win->height;

    // 1. Sfondo generale della finestra in grigio pulsanti Classic 2000
    gfx_fill_rect(x + 2, y + 22, w - 4, h - 24, COLOR_BTNFACE);

    // 2. Barra dei Menu (File, Modifica, Visualizza, Preferiti, ?)
    int32_t menu_y = y + 22;
    gfx_fill_rect(x + 2, menu_y, w - 4, 18, COLOR_BTNFACE);
    gfx_draw_string(x + 10,  menu_y + 3, "File", COLOR_WINDOWTEXT, 0, true);
    gfx_draw_string(x + 56,  menu_y + 3, "Modifica", COLOR_WINDOWTEXT, 0, true);
    gfx_draw_string(x + 130, menu_y + 3, "Visualizza", COLOR_WINDOWTEXT, 0, true);
    gfx_draw_string(x + 226, menu_y + 3, "Preferiti", COLOR_WINDOWTEXT, 0, true);
    gfx_draw_string(x + 308, menu_y + 3, "?", COLOR_WINDOWTEXT, 0, true);
    gfx_draw_bevel_rect(x + 2, menu_y + 18, w - 4, 2, true);

    // 3. Barra degli Strumenti di Navigazione
    int32_t tb_y = menu_y + 20;
    gfx_fill_rect(x + 2, tb_y, w - 4, 30, COLOR_BTNFACE);

    // [ < Indietro ]
    bool can_back = (state->history_back_count > 0);
    gfx_fill_rect(x + 6, tb_y + 3, 26, 22, COLOR_BTNFACE);
    gfx_draw_bevel_rect(x + 6, tb_y + 3, 26, 22, false);
    gfx_draw_string(x + 14, tb_y + 6, "<", can_back ? 0x000000 : 0x808080, 0, true);

    // [ > Avanti ]
    bool can_fwd = (state->history_fwd_count > 0);
    gfx_fill_rect(x + 36, tb_y + 3, 26, 22, COLOR_BTNFACE);
    gfx_draw_bevel_rect(x + 36, tb_y + 3, 26, 22, false);
    gfx_draw_string(x + 44, tb_y + 6, ">", can_fwd ? 0x000000 : 0x808080, 0, true);

    // [ X Stop ]
    gfx_fill_rect(x + 66, tb_y + 3, 26, 22, COLOR_BTNFACE);
    gfx_draw_bevel_rect(x + 66, tb_y + 3, 26, 22, false);
    gfx_draw_string(x + 74, tb_y + 6, "x", 0xA00000, 0, true);

    // [ R Aggiorna ]
    gfx_fill_rect(x + 96, tb_y + 3, 26, 22, COLOR_BTNFACE);
    gfx_draw_bevel_rect(x + 96, tb_y + 3, 26, 22, false);
    gfx_draw_string(x + 104, tb_y + 6, "R", 0x006000, 0, true);

    // [ Home ]
    gfx_fill_rect(x + 126, tb_y + 3, 50, 22, COLOR_BTNFACE);
    gfx_draw_bevel_rect(x + 126, tb_y + 3, 50, 22, false);
    gfx_draw_string(x + 134, tb_y + 6, "Home", COLOR_WINDOWTEXT, 0, true);

    // Icona del globo / connessione animata sulla destra della toolbar
    gfx_draw_bevel_rect(x + w - 34, tb_y + 3, 26, 22, true);
    gfx_draw_icon_16(x + w - 29, tb_y + 6, ICON_NET);

    gfx_draw_bevel_rect(x + 2, tb_y + 28, w - 4, 2, true);

    // 4. Barra degli Indirizzi (URL Address Bar)
    int32_t ab_y = tb_y + 30;
    gfx_fill_rect(x + 2, ab_y, w - 4, 28, COLOR_BTNFACE);
    gfx_draw_string(x + 8, ab_y + 7, "Indirizzo:", COLOR_WINDOWTEXT, 0, true);

    int32_t url_box_x = x + 84;
    int32_t url_box_w = w - 156;
    gfx_fill_rect(url_box_x, ab_y + 4, url_box_w, 20, COLOR_WINDOW);
    gfx_draw_bevel_rect(url_box_x, ab_y + 4, url_box_w, 20, true);

    // Testo dell'URL
    const char* show_url = state->url_focused ? state->edit_url : state->current_url;
    gfx_draw_string(url_box_x + 6, ab_y + 6, show_url, COLOR_WINDOWTEXT, 0, true);
    if (state->url_focused) {
        int cur_x = url_box_x + 6 + (int)strlen(show_url) * 8;
        if (cur_x < url_box_x + url_box_w - 6) {
            gfx_fill_rect(cur_x, ab_y + 6, 2, 14, 0x000000);
        }
    }

    // Pulsante [ Vai ]
    int32_t go_btn_x = x + w - 66;
    gfx_fill_rect(go_btn_x, ab_y + 4, 58, 20, COLOR_BTNFACE);
    gfx_draw_bevel_rect(go_btn_x, ab_y + 4, 58, 20, false);
    gfx_draw_string(go_btn_x + 16, ab_y + 6, "Vai", 0x006000, 0, true);

    // 5. Barra dei Preferiti Rapidi
    int32_t fav_y = ab_y + 26;
    gfx_fill_rect(x + 2, fav_y, w - 4, 22, COLOR_BTNFACE);
    gfx_draw_string(x + 8, fav_y + 4, "Link:", 0x505050, 0, true);

    const char* fav_labels[] = { "Home", "YouTube", "Server 95.141.32.1", "Gateway", "Regole" };
    int32_t fav_x = x + 52;
    for (int fi = 0; fi < 5; fi++) {
        int32_t fw = (int32_t)strlen(fav_labels[fi]) * 8 + 12;
        gfx_fill_rect(fav_x, fav_y + 2, fw, 18, COLOR_BTNFACE);
        gfx_draw_bevel_rect(fav_x, fav_y + 2, fw, 18, false);
        gfx_draw_string(fav_x + 6, fav_y + 3, fav_labels[fi], (fi == 1) ? 0xCC181E : COLOR_WINDOWTEXT, 0, true);
        fav_x += fw + 6;
        if (fav_x > x + w - 40) break;
    }
    gfx_draw_bevel_rect(x + 2, fav_y + 21, w - 4, 2, true);

    // 6. Area di Rendering Contenuto HTML (Sunken White Viewport)
    int32_t vp_x = x + 4;
    int32_t vp_y = fav_y + 24;
    int32_t vp_w = w - 8;
    int32_t vp_h = h - (vp_y - y) - 24;

    gfx_fill_rect(vp_x, vp_y, vp_w, vp_h, COLOR_WINDOW);
    gfx_draw_bevel_rect(vp_x, vp_y, vp_w, vp_h, true);

    // Disegna la scrollbar verticale a destra
    int32_t sb_x = vp_x + vp_w - 16;
    int32_t sb_y = vp_y + 2;
    int32_t sb_w = 14;
    int32_t sb_h = vp_h - 4;
    gfx_fill_rect(sb_x, sb_y, sb_w, sb_h, COLOR_BTNFACE);
    gfx_draw_bevel_rect(sb_x, sb_y, sb_w, 14, false); // Pulsante Su
    gfx_draw_string(sb_x + 3, sb_y - 2, "^", 0x000000, 0, true);

    gfx_draw_bevel_rect(sb_x, sb_y + sb_h - 14, sb_w, 14, false); // Pulsante Giu
    gfx_draw_string(sb_x + 3, sb_y + sb_h - 16, "v", 0x000000, 0, true);

    // Calcolo posizione thumb scrollbar
    int32_t thumb_h = 24;
    int32_t thumb_track = sb_h - 28 - thumb_h;
    int32_t thumb_y = sb_y + 14;
    if (state->content_height > vp_h && thumb_track > 0) {
        int max_scroll = state->content_height - vp_h;
        thumb_y += (state->scroll_y * thumb_track) / max_scroll;
    }
    gfx_fill_rect(sb_x + 1, thumb_y, sb_w - 2, thumb_h, COLOR_BTNFACE);
    gfx_draw_bevel_rect(sb_x + 1, thumb_y, sb_w - 2, thumb_h, false);

    // Rendering degli elementi HTML visibili nella viewport
    for (int i = 0; i < state->num_elems; i++) {
        browser_elem_t* el = &state->elems[i];
        int32_t draw_y = vp_y + el->y - state->scroll_y;

        // Clipping verticale rapido
        if (draw_y + el->h < vp_y + 2 || draw_y > vp_y + vp_h - 4) continue;

        if (el->type == ELEM_H1) {
            uint32_t col = el->color ? el->color : 0x0A246A;
            gfx_draw_string(vp_x + el->x, draw_y, el->text, col, 0, true);
        } else if (el->type == ELEM_H2) {
            uint32_t col = el->color ? el->color : 0x003399;
            gfx_draw_string(vp_x + el->x, draw_y, el->text, col, 0, true);
        } else if (el->type == ELEM_H3) {
            uint32_t col = el->color ? el->color : 0x1A365D;
            gfx_draw_string(vp_x + el->x, draw_y, el->text, col, 0, true);
        } else if (el->type == ELEM_VIDEO) {
            int32_t vw = el->w;
            int32_t vh = el->h;
            if (vw > vp_w - 36) vw = vp_w - 36;
            // Sfondo scuro del player video HTML5
            gfx_fill_rect(vp_x + el->x, draw_y, vw, vh, 0x111111);
            gfx_draw_bevel_rect(vp_x + el->x, draw_y, vw, vh, true);

            // Pulsante Play centrale stile YouTube
            int32_t btn_cx = vp_x + el->x + (vw / 2);
            int32_t btn_cy = draw_y + (vh / 2) - 10;
            gfx_fill_rect(btn_cx - 24, btn_cy - 16, 48, 32, 0xCC181E);
            gfx_draw_bevel_rect(btn_cx - 24, btn_cy - 16, 48, 32, false);
            gfx_draw_string(btn_cx - 4, btn_cy - 7, ">", 0xFFFFFF, 0, true);

            // Barra di controllo inferiore del video HTML5
            int32_t ctrl_y = draw_y + vh - 24;
            gfx_fill_rect(vp_x + el->x, ctrl_y, vw, 24, 0x222222);
            gfx_fill_rect(vp_x + el->x + 6, ctrl_y + 4, vw - 12, 4, 0x444444);
            gfx_fill_rect(vp_x + el->x + 6, ctrl_y + 4, (vw - 12) / 3, 4, 0xCC181E);
            gfx_draw_string(vp_x + el->x + 8, ctrl_y + 10, ">  ||   0:42 / 4:18    [HD]  [Audio]  [HTML5]", 0xDDDDDD, 0, true);
        } else if (el->type == ELEM_BUTTON) {
            gfx_fill_rect(vp_x + el->x, draw_y, el->w + 14, 20, COLOR_BTNFACE);
            gfx_draw_bevel_rect(vp_x + el->x, draw_y, el->w + 14, 20, false);
            gfx_draw_string(vp_x + el->x + 6, draw_y + 3, el->text, COLOR_WINDOWTEXT, 0, true);
        } else if (el->type == ELEM_HR) {
            gfx_draw_bevel_rect(vp_x + el->x, draw_y + 2, vp_w - 32, 2, true);
        } else if (el->type == ELEM_LINK) {
            bool is_hov = (i == state->hover_elem);
            uint32_t link_col = is_hov ? 0xCC0000 : 0x0000CC;
            gfx_draw_string(vp_x + el->x, draw_y, el->text, link_col, 0, true);
            // Linea di sottolineatura link
            for (int lx = 0; lx < el->w; lx++) {
                gfx_draw_pixel(vp_x + el->x + lx, draw_y + 14, link_col);
            }
        } else if (el->type == ELEM_LIST_ITEM) {
            gfx_fill_rect(vp_x + el->x - 10, draw_y + 6, 4, 4, 0x000000);
            gfx_draw_string(vp_x + el->x, draw_y, el->text, COLOR_WINDOWTEXT, 0, true);
        } else if (el->type == ELEM_PRE) {
            gfx_fill_rect(vp_x + el->x - 2, draw_y - 1, el->w + 4, el->h + 2, 0xF4F4F4);
            gfx_draw_string(vp_x + el->x, draw_y, el->text, 0x222222, 0, true);
        } else {
            uint32_t col = el->color ? el->color : COLOR_WINDOWTEXT;
            gfx_draw_string(vp_x + el->x, draw_y, el->text, col, 0, true);
        }
    }

    // 7. Barra di Stato Inferiore
    int32_t stat_y = y + h - 22;
    gfx_fill_rect(x + 2, stat_y, w - 4, 20, COLOR_BTNFACE);

    // Pannello 1: Messaggio di stato
    int32_t sp1_w = w - 190;
    gfx_draw_bevel_rect(x + 4, stat_y + 2, sp1_w, 16, true);
    gfx_draw_string(x + 8, stat_y + 4, state->status_msg, COLOR_WINDOWTEXT, 0, true);

    // Pannello 2: Zona di sicurezza
    int32_t sp2_x = x + 4 + sp1_w + 4;
    gfx_draw_bevel_rect(sp2_x, stat_y + 2, 140, 16, true);
    if (state->is_secure_local) {
        gfx_draw_string(sp2_x + 6, stat_y + 4, "Computer locale", 0x005500, 0, true);
    } else {
        gfx_draw_string(sp2_x + 6, stat_y + 4, "Internet (Protetto)", 0x000088, 0, true);
    }

    // Pannello 3: Icona status
    int32_t sp3_x = sp2_x + 144;
    gfx_draw_bevel_rect(sp3_x, stat_y + 2, 28, 16, true);
    gfx_draw_icon_16(sp3_x + 6, stat_y + 2, ICON_NET);
}

// Gestione eventi mouse
static void browser_mouse(window_t* win, int32_t local_x, int32_t local_y, bool clicked) {
    browser_state_t* state = (browser_state_t*)win->app_data;
    if (!state) return;

    int32_t w = win->width;
    int32_t h = win->height;

    int32_t tb_y = 42;
    int32_t ab_y = tb_y + 30;
    int32_t fav_y = ab_y + 26;
    int32_t vp_x = 4;
    int32_t vp_y = fav_y + 24;
    int32_t vp_w = w - 8;
    int32_t vp_h = h - vp_y - 24;

    // Rileva hover su collegamenti ipertestuali nella viewport
    state->hover_elem = -1;
    if (local_x >= vp_x && local_x <= vp_x + vp_w - 18 && local_y >= vp_y && local_y <= vp_y + vp_h) {
        for (int i = 0; i < state->num_elems; i++) {
            browser_elem_t* el = &state->elems[i];
            if (el->type != ELEM_LINK) continue;
            int32_t ey = vp_y + el->y - state->scroll_y;
            if (local_x >= vp_x + el->x && local_x <= vp_x + el->x + el->w &&
                local_y >= ey && local_y <= ey + el->h) {
                state->hover_elem = i;
                mini_snprintf(state->status_msg, sizeof(state->status_msg), "Collega a: %s", el->href);
                break;
            }
        }
    }

    if (!clicked) return;

    // Clic sulla barra degli strumenti
    if (local_y >= tb_y + 3 && local_y <= tb_y + 25) {
        // [ < Indietro ]
        if (local_x >= 6 && local_x <= 32 && state->history_back_count > 0) {
            char prev_url[MAX_URL_LEN];
            strcpy(prev_url, state->history_back[--state->history_back_count]);
            if (state->history_fwd_count < MAX_HISTORY) {
                strcpy(state->history_fwd[state->history_fwd_count++], state->current_url);
            }
            browser_load_url(state, prev_url, vp_w);
            return;
        }

        // [ > Avanti ]
        if (local_x >= 36 && local_x <= 62 && state->history_fwd_count > 0) {
            char next_url[MAX_URL_LEN];
            strcpy(next_url, state->history_fwd[--state->history_fwd_count]);
            if (state->history_back_count < MAX_HISTORY) {
                strcpy(state->history_back[state->history_back_count++], state->current_url);
            }
            browser_load_url(state, next_url, vp_w);
            return;
        }

        // [ X Stop ]
        if (local_x >= 66 && local_x <= 92) {
            state->is_loading = false;
            mini_snprintf(state->status_msg, sizeof(state->status_msg), "Operazione interrotta dall'utente");
            return;
        }

        // [ R Aggiorna ]
        if (local_x >= 96 && local_x <= 122) {
            browser_load_url(state, state->current_url, vp_w);
            return;
        }

        // [ Home ]
        if (local_x >= 126 && local_x <= 176) {
            browser_navigate(win, BROWSER_DEFAULT_HOME, true);
            return;
        }
    }

    // Clic sulla barra degli indirizzi o pulsante [ Vai ]
    if (local_y >= ab_y + 4 && local_y <= ab_y + 24) {
        int32_t url_box_x = 84;
        int32_t url_box_w = w - 156;
        int32_t go_btn_x = w - 66;

        if (local_x >= url_box_x && local_x <= url_box_x + url_box_w) {
            state->url_focused = true;
            return;
        }

        if (local_x >= go_btn_x && local_x <= go_btn_x + 58) {
            state->url_focused = false;
            browser_navigate(win, state->edit_url, true);
            return;
        }
    }

    // Clic sulla barra dei Preferiti
    if (local_y >= fav_y + 2 && local_y <= fav_y + 20) {
        const char* fav_urls[] = {
            "about:home",
            "http://youtube.it",
            "http://95.141.32.1",
            "http://10.0.2.2",
            "about:rules"
        };
        const char* fav_labels[] = { "Home", "YouTube", "Server 95.141.32.1", "Gateway", "Regole" };
        int32_t fx = 52;
        for (int fi = 0; fi < 5; fi++) {
            int32_t fw = (int32_t)strlen(fav_labels[fi]) * 8 + 12;
            if (local_x >= fx && local_x <= fx + fw) {
                state->url_focused = false;
                browser_navigate(win, fav_urls[fi], true);
                return;
            }
            fx += fw + 6;
        }
    }

    // Clic nella Viewport HTML (collegamenti ipertestuali o scroll)
    if (local_x >= vp_x && local_x <= vp_x + vp_w && local_y >= vp_y && local_y <= vp_y + vp_h) {
        state->url_focused = false;

        // Clic sulla scrollbar
        int32_t sb_x = vp_x + vp_w - 16;
        if (local_x >= sb_x) {
            if (local_y <= vp_y + 16) {
                if (state->scroll_y > 0) state->scroll_y -= 32;
                if (state->scroll_y < 0) state->scroll_y = 0;
            } else if (local_y >= vp_y + vp_h - 16) {
                if (state->scroll_y + vp_h < state->content_height) state->scroll_y += 32;
            } else {
                // Salto proporzionale
                int32_t rel_y = local_y - (vp_y + 16);
                int32_t track_h = vp_h - 32;
                if (track_h > 0 && state->content_height > vp_h) {
                    state->scroll_y = (rel_y * (state->content_height - vp_h)) / track_h;
                }
            }
            return;
        }

        // Clic su un link
        if (state->hover_elem >= 0 && state->hover_elem < state->num_elems) {
            browser_elem_t* el = &state->elems[state->hover_elem];
            if (el->type == ELEM_LINK && el->href[0] != '\0') {
                browser_navigate(win, el->href, true);
                return;
            }
        }
    }
}

// Gestione tastiera
static void browser_key(window_t* win, char c) {
    browser_state_t* state = (browser_state_t*)win->app_data;
    if (!state) return;

    if (state->url_focused) {
        if (c == '\r' || c == '\n') {
            state->url_focused = false;
            browser_navigate(win, state->edit_url, true);
            return;
        }
        if (c == 27) { // Escape
            state->url_focused = false;
            strncpy(state->edit_url, state->current_url, sizeof(state->edit_url) - 1);
            return;
        }
        if (c == '\b') { // Backspace
            size_t len = strlen(state->edit_url);
            if (len > 0) {
                state->edit_url[len - 1] = '\0';
            }
            return;
        }
        if (c >= 32 && c <= 126) {
            size_t len = strlen(state->edit_url);
            if (len < sizeof(state->edit_url) - 2) {
                state->edit_url[len] = c;
                state->edit_url[len + 1] = '\0';
            }
            return;
        }
    } else {
        // Tasti di scorrimento pagina
        int32_t vp_h = win->height - 128 - 24;
        if (c == 'w' || c == 'W' || c == 0x11) { // Up
            if (state->scroll_y > 0) state->scroll_y -= 24;
            if (state->scroll_y < 0) state->scroll_y = 0;
        } else if (c == 's' || c == 'S' || c == 0x12) { // Down
            if (state->scroll_y + vp_h < state->content_height) state->scroll_y += 24;
        } else if (c == ' ') { // Space = Page Down
            state->scroll_y += vp_h - 40;
            if (state->scroll_y + vp_h > state->content_height) {
                state->scroll_y = state->content_height - vp_h;
            }
            if (state->scroll_y < 0) state->scroll_y = 0;
        } else if (c == '\b') { // Backspace = Indietro nella cronologia
            if (state->history_back_count > 0) {
                char prev_url[MAX_URL_LEN];
                strcpy(prev_url, state->history_back[--state->history_back_count]);
                browser_load_url(state, prev_url, win->width - 12);
            }
        }
    }
}

// Funzione di creazione finestra Web Browser
static window_t* browser_create_window(const char* initial_url) {
    uint32_t sw = gfx_get_width();
    uint32_t sh = gfx_get_height();

    int32_t bw = 780;
    int32_t bh = 540;
    if (bw > (int32_t)sw - 20) bw = sw - 20;
    if (bh > (int32_t)sh - 40) bh = sh - 40;

    int32_t bx = (sw - bw) / 2;
    int32_t by = (sh - bh) / 2 - 10;
    if (by < 10) by = 10;

    window_t* win = wm_create_window("NewSoft Web Browser", bx, by, bw, bh, APP_BROWSER);
    if (!win) return NULL;

    browser_state_t* state = (browser_state_t*)kmalloc(sizeof(browser_state_t));
    if (!state) return NULL;

    memset(state, 0, sizeof(browser_state_t));
    state->hover_elem = -1;

    win->app_data = state;
    win->on_draw = browser_draw;
    win->on_mouse = browser_mouse;
    win->on_key = browser_key;

    browser_navigate(win, (initial_url && initial_url[0]) ? initial_url : BROWSER_DEFAULT_HOME, false);
    return win;
}

void app_browser_launch(void) {
    browser_create_window(BROWSER_DEFAULT_HOME);
}

void app_browser_open_url(const char* url) {
    browser_create_window(url);
}
