#include "../WebBrowser/jsengine.h"
#include <screen_modules.h>
#include <stdarg.h>
#include "quickjs.h"

__attribute__((section(".header"), used)) module_header_t g_jsengine_header = {
    .magic = MODULE_MAGIC_NEWOS,
    .header_size = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type = MODULE_TYPE_SYSTEM_LIBRARY,
    .code_size = 0,
    .entry_offset = 64,
    .checksum = 0,
    .security_flags =
        SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name = "JSENGINE",
    .signature = MODULE_AUTH_TAG};

static volatile uint32_t g_reloc_info[2] = { 0x52454C4Fu, 0xCAFEBABEu };
static const kernel_api_t *g_js_api = NULL;
static js_params_t *g_js_params = NULL;
static int js_dispatch(const kernel_api_t *api, js_params_t *p);

static void qjs_apply_relocs(void) {
    if (g_reloc_info[0] != 0x52454C4Fu || g_reloc_info[1] == 0xCAFEBABEu) return;
    uintptr_t base = (uintptr_t)&g_jsengine_header;
    const uint32_t *tbl = (const uint32_t *)(base + g_reloc_info[1]);
    uint32_t count = tbl[0];
    for (uint32_t i = 0; i < count; i++) {
        uint32_t off = tbl[1 + i];
        *(uint64_t *)(base + off) += (uint64_t)base;
    }
    g_reloc_info[0] = 0;
}

__attribute__((force_align_arg_pointer))
int module_main(const kernel_api_t *api, js_params_t *p) {
    qjs_apply_relocs();
    return js_dispatch(api, p);
}

// ==============================================================================
// LIBC FREESTANDING SHIM PER QUICKJS (out-of-kernel)
// ==============================================================================
int errno = 0;

typedef struct {
    uintptr_t raw_ptr;
    size_t size;
    size_t magic;
    size_t pad;
} qjs_alloc_hdr_t;

void *malloc(size_t size) {
    if (!g_js_api || !g_js_api->kmalloc) return NULL;
    if (size == 0) size = 1;
    void *raw = g_js_api->kmalloc(sizeof(qjs_alloc_hdr_t) + 16 + size);
    if (!raw) return NULL;
    uintptr_t aligned = ((uintptr_t)raw + sizeof(qjs_alloc_hdr_t) + 15) & ~(uintptr_t)15;
    qjs_alloc_hdr_t *h = ((qjs_alloc_hdr_t *)aligned) - 1;
    h->raw_ptr = (uintptr_t)raw;
    h->size = size;
    h->magic = 0x514A5341u;
    h->pad = 0;
    return (void *)aligned;
}

void free(void *ptr) {
    if (!ptr || !g_js_api || !g_js_api->kfree) return;
    qjs_alloc_hdr_t *h = ((qjs_alloc_hdr_t *)ptr) - 1;
    if (h->magic == 0x514A5341u) {
        void *raw = (void *)h->raw_ptr;
        h->magic = 0;
        g_js_api->kfree(raw);
    }
}

void *calloc(size_t nmemb, size_t size) {
    size_t tot = nmemb * size;
    void *p = malloc(tot);
    if (p) {
        uint8_t *b = (uint8_t *)p;
        for (size_t i = 0; i < tot; i++) b[i] = 0;
    }
    return p;
}

void *realloc(void *ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) { free(ptr); return NULL; }
    qjs_alloc_hdr_t *h = ((qjs_alloc_hdr_t *)ptr) - 1;
    if (h->magic != 0x514A5341u) return NULL;
    if (size <= h->size) return ptr;
    void *np = malloc(size);
    if (!np) return NULL;
    uint8_t *d = (uint8_t *)np;
    const uint8_t *s = (const uint8_t *)ptr;
    for (size_t i = 0; i < h->size; i++) d[i] = s[i];
    free(ptr);
    return np;
}

void *memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dest;
}

void *memmove(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    if (d < s) { for (size_t i = 0; i < n; i++) d[i] = s[i]; }
    else if (d > s) { for (size_t i = n; i > 0; i--) d[i - 1] = s[i - 1]; }
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

void *memchr(const void *s, int c, size_t n) {
    const uint8_t *p = (const uint8_t *)s;
    for (size_t i = 0; i < n; i++) {
        if (p[i] == (uint8_t)c) return (void *)(p + i);
    }
    return NULL;
}

size_t strlen(const char *s) { size_t n = 0; while (s && s[n]) n++; return n; }
int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return (int)(unsigned char)*s1 - (int)(unsigned char)*s2;
}
int strncmp(const char *s1, const char *s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i] || !s1[i]) return (int)(unsigned char)s1[i] - (int)(unsigned char)s2[i];
    }
    return 0;
}
char *strcpy(char *dst, const char *src) {
    char *r = dst;
    while ((*dst++ = *src++));
    return r;
}
char *strncpy(char *dst, const char *src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = '\0';
    return dst;
}
char *strchr(const char *s, int c) {
    while (*s) { if (*s == (char)c) return (char *)s; s++; }
    return (c == '\0') ? (char *)s : NULL;
}
char *strrchr(const char *s, int c) {
    const char *last = NULL;
    while (*s) { if (*s == (char)c) last = s; s++; }
    return (c == '\0') ? (char *)s : (char *)last;
}
char *strstr(const char *h, const char *n) {
    if (!*n) return (char *)h;
    size_t nl = strlen(n);
    while (*h) { if (strncmp(h, n, nl) == 0) return (char *)h; h++; }
    return NULL;
}

static unsigned long long q_strtoull_impl(const char *nptr, char **endptr, int base, int is_signed, int *neg_out) {
    const char *s = nptr;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    if (base == 0) {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
        else if (s[0] == '0') { base = 8; s++; }
        else base = 10;
    } else if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }
    unsigned long long acc = 0;
    int any = 0;
    while (*s) {
        int d = -1;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'z') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z') d = *s - 'A' + 10;
        if (d < 0 || d >= base) break;
        acc = acc * (unsigned long long)base + (unsigned long long)d;
        any = 1;
        s++;
    }
    if (endptr) *endptr = (char *)(any ? s : nptr);
    if (neg_out) *neg_out = neg;
    (void)is_signed;
    return acc;
}
unsigned long strtoul(const char *nptr, char **endptr, int base) {
    int neg = 0;
    unsigned long long v = q_strtoull_impl(nptr, endptr, base, 0, &neg);
    return neg ? (unsigned long)(-(long)v) : (unsigned long)v;
}
long strtol(const char *nptr, char **endptr, int base) {
    int neg = 0;
    unsigned long long v = q_strtoull_impl(nptr, endptr, base, 1, &neg);
    return neg ? -(long)v : (long)v;
}
unsigned long long strtoull(const char *nptr, char **endptr, int base) {
    int neg = 0;
    unsigned long long v = q_strtoull_impl(nptr, endptr, base, 0, &neg);
    return neg ? (unsigned long long)(-(long long)v) : v;
}
long long strtoll(const char *nptr, char **endptr, int base) {
    int neg = 0;
    unsigned long long v = q_strtoull_impl(nptr, endptr, base, 1, &neg);
    return neg ? -(long long)v : (long long)v;
}
int atoi(const char *nptr) { return (int)strtol(nptr, NULL, 10); }

void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *)) {
    if (!base || nmemb < 2 || size == 0 || !compar) return;
    uint8_t *b = (uint8_t *)base;
    for (size_t i = 1; i < nmemb; i++) {
        for (size_t j = i; j > 0 && compar(b + (j - 1) * size, b + j * size) > 0; j--) {
            uint8_t *p1 = b + (j - 1) * size, *p2 = b + j * size;
            for (size_t k = 0; k < size; k++) { uint8_t t = p1[k]; p1[k] = p2[k]; p2[k] = t; }
        }
    }
}

int vsnprintf(char *str, size_t size, const char *fmt, va_list ap) {
    size_t pos = 0;
    #define PUTC(ch) do { if (str && pos + 1 < size) str[pos] = (ch); pos++; } while (0)
    while (fmt && *fmt) {
        if (*fmt != '%') { PUTC(*fmt++); continue; }
        fmt++;
        while (*fmt == '-' || *fmt == '+' || *fmt == ' ' || *fmt == '0' || *fmt == '#') fmt++;
        while (*fmt >= '0' && *fmt <= '9') fmt++;
        if (*fmt == '.') { fmt++; if (*fmt == '*') { (void)va_arg(ap, int); fmt++; } else while (*fmt >= '0' && *fmt <= '9') fmt++; }
        int is_long = 0;
        if (*fmt == 'l') { is_long = 1; fmt++; if (*fmt == 'l') { is_long = 2; fmt++; } }
        else if (*fmt == 'z') { is_long = 2; fmt++; }
        char spec = *fmt ? *fmt++ : '\0';
        if (spec == 's') {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            while (*s) PUTC(*s++);
        } else if (spec == 'c') {
            PUTC((char)va_arg(ap, int));
        } else if (spec == 'd' || spec == 'i') {
            long long v = (is_long == 2) ? va_arg(ap, long long) : (is_long == 1 ? va_arg(ap, long) : va_arg(ap, int));
            if (v < 0) { PUTC('-'); v = -v; }
            char buf[24]; int bi = 0;
            unsigned long long uv = (unsigned long long)v;
            do { buf[bi++] = '0' + (uv % 10); uv /= 10; } while (uv);
            while (bi > 0) PUTC(buf[--bi]);
        } else if (spec == 'u' || spec == 'x' || spec == 'X' || spec == 'p') {
            unsigned long long uv = (spec == 'p') ? (unsigned long long)(uintptr_t)va_arg(ap, void *) :
                ((is_long == 2) ? va_arg(ap, unsigned long long) : (is_long == 1 ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int)));
            unsigned base = (spec == 'u') ? 10 : 16;
            const char *digs = (spec == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
            if (spec == 'p') { PUTC('0'); PUTC('x'); }
            char buf[24]; int bi = 0;
            do { buf[bi++] = digs[uv % base]; uv /= base; } while (uv);
            while (bi > 0) PUTC(buf[--bi]);
        } else if (spec == '%') {
            PUTC('%');
        }
    }
    if (str && size > 0) str[pos < size ? pos : size - 1] = '\0';
    return (int)pos;
    #undef PUTC
}

int snprintf(char *str, size_t size, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = vsnprintf(str, size, fmt, ap);
    va_end(ap); return r;
}
int sprintf(char *str, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = vsnprintf(str, 65536, fmt, ap);
    va_end(ap); return r;
}

// ==============================================================================
// UTILITA' FREESTANDING
// ==============================================================================
static void j_memset(void *dst, int v, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    for (size_t i = 0; i < n; i++) d[i] = (uint8_t)v;
}

static size_t j_strlen(const char *s) {
    size_t n = 0;
    while (s && s[n]) n++;
    return n;
}

static void j_strcpy(char *dst, const char *src, size_t max_len) {
    if (!dst || max_len == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t i = 0;
    while (src[i] && i + 1 < max_len) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static char j_tolower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

static bool j_isspace(char c) {
    return (c == ' ' || c == '\t' || c == '\r' || c == '\n');
}

static int j_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static int j_strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
        if (a[i] == '\0') return 0;
    }
    return 0;
}

static int j_strncasecmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        char ca = j_tolower(a[i]), cb = j_tolower(b[i]);
        if (ca != cb) return (int)(unsigned char)ca - (int)(unsigned char)cb;
        if (ca == '\0') return 0;
    }
    return 0;
}

static const char *j_strstr_n(const char *hay, size_t hay_len, const char *needle) {
    size_t nl = j_strlen(needle);
    if (nl == 0 || hay_len < nl) return NULL;
    for (size_t i = 0; i + nl <= hay_len; i++) {
        size_t k = 0;
        while (k < nl && hay[i + k] == needle[k]) k++;
        if (k == nl) return hay + i;
    }
    return NULL;
}

static const char *j_strcasestr(const char *hay, const char *needle) {
    if (!hay || !needle || !*needle) return hay;
    for (const char *p = hay; *p; p++) {
        const char *h = p, *n = needle;
        while (*h && *n && j_tolower(*h) == j_tolower(*n)) { h++; n++; }
        if (!*n) return p;
    }
    return NULL;
}

static void out_append(js_params_t *p, const char *s) {
    if (!p->out_html || p->out_html_cap == 0 || !s) return;
    while (*s && p->out_html_len + 1 < p->out_html_cap) {
        p->out_html[p->out_html_len++] = *s++;
    }
    p->out_html[p->out_html_len] = '\0';
}

// ==============================================================================
// MINI-VM JAVASCRIPT (VARIABILI + ESPRESSIONI + MINI-DOM BRIDGE)
// ==============================================================================
#define MAX_JS_VARS 48

typedef struct {
    char name[40];
    char val[256];
    char tag[16]; // Per variabili createElement("...")
} js_var_t;

typedef struct {
    const kernel_api_t *api;
    js_params_t        *params;
    js_var_t            vars[MAX_JS_VARS];
    int                 num_vars;
} js_vm_t;

static js_var_t *vm_find_var(js_vm_t *vm, const char *name) {
    for (int i = 0; i < vm->num_vars; i++) {
        if (j_strcmp(vm->vars[i].name, name) == 0) return &vm->vars[i];
    }
    return NULL;
}

static js_var_t *vm_set_var(js_vm_t *vm, const char *name, const char *val, const char *tag) {
    js_var_t *v = vm_find_var(vm, name);
    if (!v) {
        if (vm->num_vars >= MAX_JS_VARS) return NULL;
        v = &vm->vars[vm->num_vars++];
        j_memset(v, 0, sizeof(*v));
        j_strcpy(v->name, name, sizeof(v->name));
    }
    if (val) j_strcpy(v->val, val, sizeof(v->val));
    if (tag) j_strcpy(v->tag, tag, sizeof(v->tag));
    return v;
}

// Valuta un'espressione semplice (stringhe quotate, numeri, variabili, concatenazione '+')
static void vm_eval_expr(js_vm_t *vm, const char *expr, size_t len, char *out, size_t max_out) {
    out[0] = '\0';
    size_t op = 0;
    size_t i = 0;
    long num_acc = 0;
    bool has_num = false;
    bool is_str = false;

    while (i < len) {
        while (i < len && j_isspace(expr[i])) i++;
        if (i >= len) break;

        if (expr[i] == '"' || expr[i] == '\'' || expr[i] == '`') {
            is_str = true;
            char q = expr[i++];
            while (i < len && expr[i] != q) {
                char c = expr[i++];
                if (c == '\\' && i < len) {
                    char esc = expr[i++];
                    if (esc == 'n') c = '\n';
                    else if (esc == 't') c = ' ';
                    else c = esc;
                }
                if (op + 1 < max_out) out[op++] = c;
            }
            if (i < len && expr[i] == q) i++;
        } else if ((expr[i] >= '0' && expr[i] <= '9') || (expr[i] == '-' && i + 1 < len && expr[i + 1] >= '0' && expr[i + 1] <= '9')) {
            long sign = 1;
            if (expr[i] == '-') { sign = -1; i++; }
            long n = 0;
            while (i < len && expr[i] >= '0' && expr[i] <= '9') {
                n = n * 10 + (expr[i] - '0');
                i++;
            }
            n *= sign;
            if (!is_str) {
                num_acc += n;
                has_num = true;
            } else {
                char nbuf[24];
                int np = 0;
                if (n < 0) { nbuf[np++] = '-'; n = -n; }
                char rev[20]; int rp = 0;
                if (n == 0) rev[rp++] = '0';
                while (n > 0) { rev[rp++] = (char)('0' + (n % 10)); n /= 10; }
                while (rp > 0) nbuf[np++] = rev[--rp];
                nbuf[np] = '\0';
                for (int k = 0; nbuf[k] && op + 1 < max_out; k++) out[op++] = nbuf[k];
            }
        } else {
            char ident[64];
            size_t ii = 0;
            while (i < len && !j_isspace(expr[i]) && expr[i] != '+' && expr[i] != ',' && expr[i] != ')') {
                if (ii + 1 < sizeof(ident)) ident[ii++] = expr[i];
                i++;
            }
            ident[ii] = '\0';
            if (j_strcmp(ident, "window.location.href") == 0 || j_strcmp(ident, "document.URL") == 0) {
                is_str = true;
                const char *u = vm->params->current_url ? vm->params->current_url : "";
                for (size_t k = 0; u[k] && op + 1 < max_out; k++) out[op++] = u[k];
            } else if (j_strcmp(ident, "document.title") == 0) {
                is_str = true;
                const char *t = vm->params->out_title;
                for (size_t k = 0; t[k] && op + 1 < max_out; k++) out[op++] = t[k];
            } else {
                js_var_t *v = vm_find_var(vm, ident);
                if (v) {
                    is_str = true;
                    for (size_t k = 0; v->val[k] && op + 1 < max_out; k++) out[op++] = v->val[k];
                }
            }
        }

        while (i < len && j_isspace(expr[i])) i++;
        if (i < len && expr[i] == '+') {
            if (has_num && !is_str) {
                // Continua somma numerica finche' non incontra una stringa
            }
            i++;
        } else {
            break;
        }
    }

    if (has_num && !is_str && op == 0) {
        if (num_acc < 0 && op + 1 < max_out) { out[op++] = '-'; num_acc = -num_acc; }
        char rev[24]; int rp = 0;
        if (num_acc == 0) rev[rp++] = '0';
        while (num_acc > 0 && rp < 23) { rev[rp++] = (char)('0' + (num_acc % 10)); num_acc /= 10; }
        while (rp > 0 && op + 1 < max_out) out[op++] = rev[--rp];
    }
    out[op] = '\0';
}

static void vm_exec_script(js_vm_t *vm, const char *code, size_t len) {
    // Salta script enormi/minificati di framework (> 32 KB) per non perdere tempo su codice non-DOM
    if (len > 32768) return;

    size_t i = 0;
    char val_buf[512];

    while (i < len) {
        while (i < len && (j_isspace(code[i]) || code[i] == ';')) i++;
        if (i >= len) break;

        // Commenti // e /* ... */
        if (i + 1 < len && code[i] == '/' && code[i + 1] == '/') {
            while (i < len && code[i] != '\n') i++;
            continue;
        }
        if (i + 1 < len && code[i] == '/' && code[i + 1] == '*') {
            i += 2;
            while (i + 1 < len && !(code[i] == '*' && code[i + 1] == '/')) i++;
            i += 2;
            continue;
        }

        // Trova fine statement ';' o fine riga (rispettando stringhe e parentesi)
        size_t st_start = i;
        char in_q = 0;
        int paren = 0;
        while (i < len) {
            char c = code[i];
            if (in_q) {
                if (c == '\\' && i + 1 < len) { i += 2; continue; }
                if (c == in_q) in_q = 0;
            } else {
                if (c == '"' || c == '\'' || c == '`') in_q = c;
                else if (c == '(') paren++;
                else if (c == ')' && paren > 0) paren--;
                else if ((c == ';' || c == '{' || c == '}') && paren == 0) break;
            }
            i++;
        }
        size_t st_len = i - st_start;
        if (i < len) i++;

        if (st_len == 0) continue;
        const char *st = code + st_start;

        // 1. document.write(...) / document.writeln(...)
        if (j_strncmp(st, "document.write(", 15) == 0 || j_strncmp(st, "document.writeln(", 17) == 0) {
            size_t off = (st[14] == '(') ? 15 : 17;
            size_t end = st_len;
            while (end > off && st[end - 1] != ')') end--;
            if (end > off) end--;
            vm_eval_expr(vm, st + off, end - off, val_buf, sizeof(val_buf));
            out_append(vm->params, val_buf);
            if (off == 17) out_append(vm->params, "<br>");
            vm->params->dom_ops++;
            continue;
        }

        // 2. document.title = ...
        if (j_strncmp(st, "document.title", 14) == 0) {
            const char *eq = j_strstr_n(st, st_len, "=");
            if (eq) {
                size_t off = (size_t)(eq - st) + 1;
                vm_eval_expr(vm, st + off, st_len - off, val_buf, sizeof(val_buf));
                j_strcpy(vm->params->out_title, val_buf, sizeof(vm->params->out_title));
                vm->params->has_title = true;
                vm->params->dom_ops++;
            }
            continue;
        }

        // 3. document.getElementById("...").innerHTML / .innerText / .textContent = ...
        if (j_strncmp(st, "document.getElementById(", 24) == 0) {
            const char *eq = j_strstr_n(st, st_len, "=");
            if (eq && (j_strstr_n(st, (size_t)(eq - st), ".innerHTML") ||
                       j_strstr_n(st, (size_t)(eq - st), ".innerText") ||
                       j_strstr_n(st, (size_t)(eq - st), ".textContent"))) {
                size_t off = (size_t)(eq - st) + 1;
                vm_eval_expr(vm, st + off, st_len - off, val_buf, sizeof(val_buf));
                out_append(vm->params, "<div>");
                out_append(vm->params, val_buf);
                out_append(vm->params, "</div>");
                vm->params->dom_ops++;
            }
            continue;
        }

        // 4. var / let / const name = expr
        if (j_strncmp(st, "var ", 4) == 0 || j_strncmp(st, "let ", 4) == 0 || j_strncmp(st, "const ", 6) == 0) {
            size_t k = (st[0] == 'c') ? 6 : 4;
            while (k < st_len && j_isspace(st[k])) k++;
            char vname[40]; size_t vi = 0;
            while (k < st_len && !j_isspace(st[k]) && st[k] != '=') {
                if (vi + 1 < sizeof(vname)) vname[vi++] = st[k];
                k++;
            }
            vname[vi] = '\0';
            while (k < st_len && st[k] != '=') k++;
            if (k < st_len && st[k] == '=') {
                k++;
                while (k < st_len && j_isspace(st[k])) k++;
                if (j_strncmp(st + k, "document.createElement(", 23) == 0) {
                    char tag[16];
                    vm_eval_expr(vm, st + k + 23, st_len - k - 23, tag, sizeof(tag));
                    vm_set_var(vm, vname, "", tag);
                } else {
                    vm_eval_expr(vm, st + k, st_len - k, val_buf, sizeof(val_buf));
                    vm_set_var(vm, vname, val_buf, NULL);
                }
            }
            continue;
        }

        // 5. el.innerHTML = ... / el.textContent = ... / document.body.appendChild(el)
        const char *dot_inner = j_strstr_n(st, st_len, ".innerHTML");
        if (!dot_inner) dot_inner = j_strstr_n(st, st_len, ".textContent");
        if (!dot_inner) dot_inner = j_strstr_n(st, st_len, ".innerText");
        if (dot_inner) {
            char vname[40];
            size_t vlen = (size_t)(dot_inner - st);
            if (vlen > 0 && vlen < sizeof(vname)) {
                for (size_t k = 0; k < vlen; k++) vname[k] = st[k];
                vname[vlen] = '\0';
                const char *eq = j_strstr_n(dot_inner, st_len - (size_t)(dot_inner - st), "=");
                if (eq) {
                    size_t off = (size_t)(eq - st) + 1;
                    vm_eval_expr(vm, st + off, st_len - off, val_buf, sizeof(val_buf));
                    vm_set_var(vm, vname, val_buf, NULL);
                    js_var_t *v = vm_find_var(vm, vname);
                    if (v && v->tag[0]) {
                        out_append(vm->params, "<"); out_append(vm->params, v->tag); out_append(vm->params, ">");
                        out_append(vm->params, val_buf);
                        out_append(vm->params, "</"); out_append(vm->params, v->tag); out_append(vm->params, ">");
                        vm->params->dom_ops++;
                    }
                }
            }
            continue;
        }

        // 6. console.log(...)
        if (j_strncmp(st, "console.log(", 12) == 0) {
            size_t end = st_len;
            while (end > 12 && st[end - 1] != ')') end--;
            if (end > 12) end--;
            vm_eval_expr(vm, st + 12, end - 12, val_buf, sizeof(val_buf));
            (void)val_buf;
            vm->params->dom_ops++;
        }
    }
}

// ==============================================================================
// SPA HYDRATION EXTRACTOR (YouTube ytInitialData / JSON-LD / Next.js)
// ==============================================================================
static void extract_json_str(const char *p, const char *end, const char *key, char *out, size_t max_out) {
    out[0] = '\0';
    size_t kl = j_strlen(key);
    for (const char *cur = p; cur + kl + 4 < end; cur++) {
        if (*cur == '"' && j_strncmp(cur + 1, key, kl) == 0 && cur[1 + kl] == '"') {
            const char *q = cur + 2 + kl;
            while (q < end && (*q == ' ' || *q == ':' || *q == '\t')) q++;
            if (q < end && *q == '"') {
                q++;
                size_t oi = 0;
                while (q < end && *q != '"' && oi + 4 < max_out) {
                    if (*q == '\\' && q + 1 < end) {
                        q++;
                        if (*q == 'n' || *q == 'r') { out[oi++] = ' '; q++; }
                        else if (*q == 'u' && q + 4 < end) {
                            uint32_t cp = 0;
                            bool ok = true;
                            for (int h = 1; h <= 4; h++) {
                                char c = q[h];
                                cp <<= 4;
                                if (c >= '0' && c <= '9') cp |= (uint32_t)(c - '0');
                                else if (c >= 'a' && c <= 'f') cp |= (uint32_t)(c - 'a' + 10);
                                else if (c >= 'A' && c <= 'F') cp |= (uint32_t)(c - 'A' + 10);
                                else { ok = false; break; }
                            }
                            if (ok) {
                                if (cp < 0x80) {
                                    out[oi++] = (char)cp;
                                } else if (cp < 0x800) {
                                    out[oi++] = (char)(0xC0 | (cp >> 6));
                                    out[oi++] = (char)(0x80 | (cp & 0x3F));
                                } else {
                                    out[oi++] = (char)(0xE0 | (cp >> 12));
                                    out[oi++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                                    out[oi++] = (char)(0x80 | (cp & 0x3F));
                                }
                                q += 5;
                            } else {
                                out[oi++] = *q++;
                            }
                        } else {
                            out[oi++] = *q++;
                        }
                    } else {
                        out[oi++] = *q++;
                    }
                }
                out[oi] = '\0';
                return;
            }
        }
    }
}

static void extract_spa_hydration(js_params_t *p, const char *script, size_t slen) {
    // 1. YouTube ytInitialData: estrae fino a 9 video reali con thumbnail, durata, views, canale e link
    if (j_strstr_n(script, slen, "\"videoRenderer\"")) {
        const char *cur = script;
        const char *end = script + slen;
        int count = 0;
        char seen_vids[9][32];
        while (cur < end && count < 9) {
            const char *vr = j_strstr_n(cur, (size_t)(end - cur), "\"videoRenderer\"");
            if (!vr) break;
            const char *win_end = (vr + 3200 < end) ? (vr + 3200) : end;
            char vid[32], title[128], chan[64], dur[24], views[40];
            extract_json_str(vr, win_end, "videoId", vid, sizeof(vid));
            bool dup = false;
            for (int sv = 0; sv < count; sv++) {
                if (j_strcmp(seen_vids[sv], vid) == 0) { dup = true; break; }
            }
            if (dup) { cur = vr + 16; continue; }
            const char *tpos = j_strstr_n(vr, (size_t)(win_end - vr), "\"title\"");
            title[0] = '\0';
            if (tpos) extract_json_str(tpos, win_end, "text", title, sizeof(title));
            if (!title[0] && tpos) extract_json_str(tpos, win_end, "simpleText", title, sizeof(title));
            const char *cpos = j_strstr_n(vr, (size_t)(win_end - vr), "\"ownerText\"");
            if (!cpos) cpos = j_strstr_n(vr, (size_t)(win_end - vr), "\"longBylineText\"");
            chan[0] = '\0';
            if (cpos) extract_json_str(cpos, win_end, "text", chan, sizeof(chan));
            dur[0] = '\0';
            const char *dpos = j_strstr_n(vr, (size_t)(win_end - vr), "\"lengthText\"");
            if (dpos) extract_json_str(dpos, win_end, "simpleText", dur, sizeof(dur));
            views[0] = '\0';
            const char *vpos = j_strstr_n(vr, (size_t)(win_end - vr), "\"shortViewCountText\"");
            if (vpos) extract_json_str(vpos, win_end, "simpleText", views, sizeof(views));
            char pub[48] = "";
            const char *ppos = j_strstr_n(vr, (size_t)(win_end - vr), "\"publishedTimeText\"");
            if (ppos) extract_json_str(ppos, win_end, "simpleText", pub, sizeof(pub));

            if (vid[0] && title[0]) {
                if (count == 0) {
                    out_append(p, "<div class=\"yt-topbar\" style=\"display:flex;\">"
                                  "<span style=\"background:#FF0000;color:#FFFFFF;border-radius:4px;font-weight:bold;\">&gt;</span>"
                                  "<b style=\"color:#0F0F0F;font-size:16px;\">YouTube IT</b>"
                                  "<form action=\"https://www.youtube.com/results\" method=\"get\">"
                                  "<input type=\"text\" name=\"search_query\" placeholder=\"Cerca su YouTube...\" style=\"border:1px solid #CCCCCC;border-radius:16px;\">"
                                  "<input type=\"submit\" value=\"Cerca\">"
                                  "</form>"
                                  "<span style=\"background:#F2F2F2;color:#0F0F0F;border-radius:16px;font-weight:bold;\">+Crea</span>"
                                  "</div>");
                    out_append(p, "<div class=\"yt-layout\" style=\"display:flex;\">");
                    out_append(p, "<div class=\"yt-sidebar\">"
                                  "<a href=\"https://www.youtube.com\" style=\"background:#F2F2F2;color:#0F0F0F;font-weight:bold;border-radius:8px;text-decoration:none;\">Home</a>"
                                  "<a href=\"https://www.youtube.com/shorts\" style=\"color:#0F0F0F;text-decoration:none;\">Shorts</a>"
                                  "<a href=\"https://www.youtube.com/feed/subscriptions\" style=\"color:#0F0F0F;text-decoration:none;\">Iscrizioni</a>"
                                  "<hr>"
                                  "<b style=\"color:#0F0F0F;\">Tu&gt;</b>"
                                  "<a href=\"https://www.youtube.com/feed/history\" style=\"color:#0F0F0F;text-decoration:none;\">Cronologia</a>"
                                  "<a href=\"https://www.youtube.com/feed/playlists\" style=\"color:#0F0F0F;text-decoration:none;\">Playlist</a>"
                                  "<a href=\"https://www.youtube.com/results?search_query=music\" style=\"color:#0F0F0F;text-decoration:none;\">Musica</a>"
                                  "<a href=\"https://www.youtube.com/results?search_query=gaming\" style=\"color:#0F0F0F;text-decoration:none;\">Giochi</a>"
                                  "</div>");
                    out_append(p, "<div class=\"yt-main\">");
                    out_append(p, "<div class=\"yt-chips\">"
                                  "<a href=\"https://www.youtube.com\" style=\"background:#0F0F0F;color:#FFFFFF;font-weight:bold;border-radius:8px;text-decoration:none;\">Tutti</a>"
                                  "<a href=\"https://www.youtube.com/results?search_query=gaming\" style=\"background:#F2F2F2;color:#0F0F0F;border-radius:8px;text-decoration:none;\">Giochi</a>"
                                  "<a href=\"https://www.youtube.com/results?search_query=music\" style=\"background:#F2F2F2;color:#0F0F0F;border-radius:8px;text-decoration:none;\">Musica</a>"
                                  "<a href=\"https://www.youtube.com/results?search_query=podcast\" style=\"background:#F2F2F2;color:#0F0F0F;border-radius:8px;text-decoration:none;\">Podcast</a>"
                                  "<a href=\"https://www.youtube.com/results?search_query=mix\" style=\"background:#F2F2F2;color:#0F0F0F;border-radius:8px;text-decoration:none;\">Mix</a>"
                                  "<a href=\"https://www.youtube.com/results?search_query=news\" style=\"background:#F2F2F2;color:#0F0F0F;border-radius:8px;text-decoration:none;\">Notizie</a>"
                                  "<a href=\"https://www.youtube.com/results?search_query=live\" style=\"background:#F2F2F2;color:#0F0F0F;border-radius:8px;text-decoration:none;\">Live</a>"
                                  "<a href=\"https://www.youtube.com/results?search_query=animation\" style=\"background:#F2F2F2;color:#0F0F0F;border-radius:8px;text-decoration:none;\">Animazione</a>"
                                  "</div>");
                    out_append(p, "<div class=\"yt-grid\">");
                }
                char av_ch[2] = { 'Y', '\0' };
                if (chan[0] >= 'a' && chan[0] <= 'z') av_ch[0] = (char)(chan[0] - 32);
                else if ((chan[0] >= 'A' && chan[0] <= 'Z') || (chan[0] >= '0' && chan[0] <= '9')) av_ch[0] = chan[0];
                const char *av_bg = (count % 3 == 0) ? "#CC0000" : ((count % 3 == 1) ? "#065FD4" : "#2E7D32");
                out_append(p, "<div class=\"yt-card\"><a href=\"https://www.youtube.com/watch?v=");
                out_append(p, vid);
                out_append(p, "\"><img src=\"https://i.ytimg.com/vi/");
                out_append(p, vid);
                out_append(p, "/mqdefault.jpg\" width=\"220\" height=\"124\"");
                if (dur[0]) {
                    out_append(p, " data-dur=\"");
                    out_append(p, dur);
                    out_append(p, "\"");
                }
                out_append(p, "></a><div class=\"yt-meta\">"
                              "<div class=\"yt-avatar\" style=\"background:");
                out_append(p, av_bg);
                out_append(p, ";color:#FFFFFF;border-radius:50%;font-weight:bold;\">");
                out_append(p, av_ch);
                out_append(p, "</div><div class=\"yt-info\">"
                              "<a href=\"https://www.youtube.com/watch?v=");
                out_append(p, vid);
                out_append(p, "\" style=\"color:#0F0F0F;font-weight:bold;text-decoration:none;\">");
                out_append(p, title);
                out_append(p, "</a>");
                if (chan[0] || views[0] || pub[0]) {
                    out_append(p, "<div style=\"color:#606060;font-size:12px;\">");
                    if (chan[0]) out_append(p, chan);
                    if (chan[0] && views[0]) out_append(p, " - ");
                    if (views[0]) out_append(p, views);
                    if ((chan[0] || views[0]) && pub[0]) out_append(p, " - ");
                    if (pub[0]) out_append(p, pub);
                    out_append(p, "</div>");
                }
                out_append(p, "</div></div></div>");
                j_strcpy(seen_vids[count], vid, sizeof(seen_vids[count]));
                p->dom_ops++;
                count++;
            }
            cur = vr + 16;
        }
        if (count > 0) {
            out_append(p, "</div></div></div>");
        }
    }
}

// ==============================================================================
// QUICKJS DOM BINDINGS (window, document, navigator, location, console, timer)
// ==============================================================================
static JSValue qjs_doc_write(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    if (!g_js_params) return JS_UNDEFINED;
    for (int i = 0; i < argc; i++) {
        const char *s = JS_ToCString(ctx, argv[i]);
        if (s) {
            out_append(g_js_params, s);
            g_js_params->dom_ops++;
            JS_FreeCString(ctx, s);
        }
    }
    return JS_UNDEFINED;
}

static JSValue qjs_noop(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)ctx; (void)this_val; (void)argc; (void)argv;
    return JS_UNDEFINED;
}

static JSValue qjs_doc_get_title(JSContext *ctx, JSValueConst this_val) {
    (void)this_val;
    if (g_js_params && g_js_params->has_title) return JS_NewString(ctx, g_js_params->out_title);
    return JS_NewString(ctx, "");
}

static JSValue qjs_doc_set_title(JSContext *ctx, JSValueConst this_val, JSValueConst val) {
    (void)this_val;
    if (g_js_params) {
        const char *s = JS_ToCString(ctx, val);
        if (s) {
            j_strcpy(g_js_params->out_title, s, sizeof(g_js_params->out_title));
            g_js_params->has_title = true;
            JS_FreeCString(ctx, s);
        }
    }
    return JS_UNDEFINED;
}

static JSValue qjs_set_timeout(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    if (argc >= 1 && JS_IsFunction(ctx, argv[0])) {
        int32_t ms = 0;
        if (argc >= 2) JS_ToInt32(ctx, &ms, argv[1]);
        if (ms <= 50) {
            JSValue r = JS_Call(ctx, argv[0], JS_UNDEFINED, 0, NULL);
            JS_FreeValue(ctx, r);
        }
    }
    return JS_NewInt32(ctx, 1);
}

static void qjs_setup_env(JSContext *ctx, const char *url) {
    JSValue global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "window", JS_DupValue(ctx, global));
    JS_SetPropertyStr(ctx, global, "self", JS_DupValue(ctx, global));
    JS_SetPropertyStr(ctx, global, "globalThis", JS_DupValue(ctx, global));
    JS_SetPropertyStr(ctx, global, "_newos_emit", JS_NewCFunction(ctx, qjs_doc_write, "_newos_emit", 1));
    JS_SetPropertyStr(ctx, global, "_newos_url", JS_NewString(ctx, url ? url : ""));

    JSValue nav = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, nav, "userAgent", JS_NewString(ctx, "Mozilla/5.0 (NewOS V1 x86_64) NewSoft/1.0"));
    JS_SetPropertyStr(ctx, nav, "language", JS_NewString(ctx, "it-IT"));
    JS_SetPropertyStr(ctx, nav, "platform", JS_NewString(ctx, "NewOS x86_64"));
    JS_SetPropertyStr(ctx, nav, "onLine", JS_NewBool(ctx, 1));
    JS_SetPropertyStr(ctx, nav, "cookieEnabled", JS_NewBool(ctx, 1));
    JS_SetPropertyStr(ctx, nav, "hardwareConcurrency", JS_NewInt32(ctx, 4));
    JS_SetPropertyStr(ctx, global, "navigator", nav);

    JSValue con = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, con, "log", JS_NewCFunction(ctx, qjs_noop, "log", 1));
    JS_SetPropertyStr(ctx, con, "info", JS_NewCFunction(ctx, qjs_noop, "info", 1));
    JS_SetPropertyStr(ctx, con, "debug", JS_NewCFunction(ctx, qjs_noop, "debug", 1));
    JS_SetPropertyStr(ctx, con, "warn", JS_NewCFunction(ctx, qjs_noop, "warn", 1));
    JS_SetPropertyStr(ctx, con, "error", JS_NewCFunction(ctx, qjs_noop, "error", 1));
    JS_SetPropertyStr(ctx, global, "console", con);

    JS_SetPropertyStr(ctx, global, "innerWidth", JS_NewInt32(ctx, 840));
    JS_SetPropertyStr(ctx, global, "innerHeight", JS_NewInt32(ctx, 560));
    JS_SetPropertyStr(ctx, global, "devicePixelRatio", JS_NewInt32(ctx, 1));
    JS_SetPropertyStr(ctx, global, "setTimeout", JS_NewCFunction(ctx, qjs_set_timeout, "setTimeout", 2));
    JS_SetPropertyStr(ctx, global, "setInterval", JS_NewCFunction(ctx, qjs_noop, "setInterval", 2));
    JS_SetPropertyStr(ctx, global, "clearInterval", JS_NewCFunction(ctx, qjs_noop, "clearInterval", 1));
    JS_SetPropertyStr(ctx, global, "requestAnimationFrame", JS_NewCFunction(ctx, qjs_set_timeout, "requestAnimationFrame", 1));
    JS_SetPropertyStr(ctx, global, "cancelAnimationFrame", JS_NewCFunction(ctx, qjs_noop, "cancelAnimationFrame", 1));
    JS_SetPropertyStr(ctx, global, "requestIdleCallback", JS_NewCFunction(ctx, qjs_set_timeout, "requestIdleCallback", 1));
    JS_SetPropertyStr(ctx, global, "clearTimeout", JS_NewCFunction(ctx, qjs_noop, "clearTimeout", 1));

    // Runtime Web API + DOM Tree completo in JS (DOM, URL, Base64, Eventi, Observer, Storage, Fetch)
    const char *web_apis_polyfill =
        "(function(){"
        "var u=_newos_url||'';"
        "function parseUrl(s){var m=/^(https?:)\\/\\/([^\\/?#]+)([^?#]*)(\\?[^#]*)?(#.*)?$/i.exec(String(s||''));"
        "if(!m)return{href:String(s||''),protocol:'https:',host:'',hostname:'',pathname:'/',search:'',hash:'',origin:''};"
        "var hn=m[2].split(':')[0];return{href:m[0],protocol:m[1],host:m[2],hostname:hn,pathname:m[3]||'/',search:m[4]||'',hash:m[5]||'',origin:m[1]+'//'+m[2]};}"
        "window.location=parseUrl(u);window.location.assign=function(){};window.location.replace=function(){};window.location.reload=function(){};"
        "window.URL=function(s,b){var p=parseUrl((b&&!/^https?:/i.test(s))?(parseUrl(b).origin+(s[0]=='/'?s:'/'+s)):s);for(var k in p)this[k]=p[k];this.searchParams=new window.URLSearchParams(this.search);this.toString=function(){return this.href;};};"
        "window.URLSearchParams=function(s){this._m={};var q=String(s||'').replace(/^\\?/,'');if(q){var a=q.split('&');for(var i=0;i<a.length;i++){var kv=a[i].split('=');if(kv[0])this._m[decodeURIComponent(kv[0])]=decodeURIComponent(kv[1]||'');}}};"
        "URLSearchParams.prototype.get=function(k){return this._m.hasOwnProperty(k)?this._m[k]:null;};"
        "URLSearchParams.prototype.has=function(k){return this._m.hasOwnProperty(k);};"
        "URLSearchParams.prototype.set=function(k,v){this._m[k]=String(v);};"
        "URLSearchParams.prototype.toString=function(){var r=[];for(var k in this._m)r.push(encodeURIComponent(k)+'='+encodeURIComponent(this._m[k]));return r.join('&');};"
        "var b64c='ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=';"
        "window.btoa=function(s){s=String(s);var o='',i=0;while(i<s.length){var c1=s.charCodeAt(i++),c2=s.charCodeAt(i++),c3=s.charCodeAt(i++);"
        "var e1=c1>>2,e2=((c1&3)<<4)|(c2>>4),e3=((c2&15)<<2)|(c3>>6),e4=c3&63;if(isNaN(c2))e3=e4=64;else if(isNaN(c3))e4=64;"
        "o+=b64c.charAt(e1)+b64c.charAt(e2)+b64c.charAt(e3)+b64c.charAt(e4);}return o;};"
        "window.atob=function(s){s=String(s).replace(/[^A-Za-z0-9+\\/=]/g,'');var o='',i=0;while(i<s.length){var e1=b64c.indexOf(s[i++]),e2=b64c.indexOf(s[i++]),e3=b64c.indexOf(s[i++]),e4=b64c.indexOf(s[i++]);"
        "var c1=(e1<<2)|(e2>>4),c2=((e2&15)<<4)|(e3>>2),c3=((e3&3)<<6)|e4;o+=String.fromCharCode(c1);if(e3!=64&&e3>=0)o+=String.fromCharCode(c2);if(e4!=64&&e4>=0)o+=String.fromCharCode(c3);}return o;};"
        "window.TextEncoder=function(){};TextEncoder.prototype.encode=function(s){var a=new Uint8Array(s.length);for(var i=0;i<s.length;i++)a[i]=s.charCodeAt(i)&255;return a;};"
        "window.TextDecoder=function(){};TextDecoder.prototype.decode=function(a){if(!a)return'';var s='';for(var i=0;i<a.length;i++)s+=String.fromCharCode(a[i]);return s;};"
        "window.queueMicrotask=function(fn){Promise.resolve().then(fn);};"
        "window.structuredClone=function(x){try{return JSON.parse(JSON.stringify(x));}catch(e){return x;}};"
        "window.performance={now:function(){return Date.now();},timing:{navigationStart:Date.now()},mark:function(){},measure:function(){}};"
        "window.crypto={getRandomValues:function(a){for(var i=0;i<a.length;i++)a[i]=(Math.random()*256)|0;return a;}};"
        "window.Event=function(t,o){this.type=t;this.bubbles=!!(o&&o.bubbles);this.cancelable=!!(o&&o.cancelable);this.preventDefault=function(){};this.stopPropagation=function(){};};"
        "window.CustomEvent=function(t,o){window.Event.call(this,t,o);this.detail=o?o.detail:null;};"
        "var _readyQ=[],_winQ=[];"
        "window.addEventListener=function(t,fn){if(t=='load'&&typeof fn=='function')_winQ.push(fn);};"
        "window.removeEventListener=function(){};window.dispatchEvent=function(){return true;};"
        "var _all=[],_byId={};"
        "function makeEl(tag){"
        "var el={tagName:String(tag||'DIV').toUpperCase(),nodeName:String(tag||'DIV').toUpperCase(),nodeType:1,"
        "id:'',className:'',_attrs:{},_cls:[],children:[],childNodes:[],parentNode:null,_ih:'',_tc:'',_emitted:false,"
        "style:{cssText:''},dataset:{}};"
        "el.classList={add:function(){for(var i=0;i<arguments.length;i++){if(el._cls.indexOf(arguments[i])<0)el._cls.push(arguments[i]);}el.className=el._cls.join(' ');},"
        "remove:function(){for(var i=0;i<arguments.length;i++){var x=el._cls.indexOf(arguments[i]);if(x>=0)el._cls.splice(x,1);}el.className=el._cls.join(' ');},"
        "toggle:function(c){if(el._cls.indexOf(c)>=0)this.remove(c);else this.add(c);},"
        "contains:function(c){return el._cls.indexOf(c)>=0;}};"
        "el.setAttribute=function(k,v){v=String(v);el._attrs[k]=v;if(k=='id'){el.id=v;_byId[v]=el;}else if(k=='class'){el.className=v;el._cls=v.split(/\\s+/);}};"
        "el.getAttribute=function(k){return el._attrs.hasOwnProperty(k)?el._attrs[k]:null;};"
        "el.hasAttribute=function(k){return el._attrs.hasOwnProperty(k);};"
        "el.removeAttribute=function(k){delete el._attrs[k];};"
        "el.addEventListener=function(){};el.removeEventListener=function(){};el.focus=function(){};el.blur=function(){};"
        "el.getBoundingClientRect=function(){return{top:0,left:0,width:100,height:24,right:100,bottom:24};};"
        "el.toHTML=function(){if(el.nodeType==3)return el._tc;if(el.nodeType==8)return'';var t=el.tagName.toLowerCase();if(t=='#fragment'){var s='';for(var i=0;i<el.children.length;i++)s+=el.children[i].toHTML();return s;}"
        "var a='';if(el.id)a+=' id=\"'+el.id+'\"';if(el.className)a+=' class=\"'+el.className+'\"';"
        "for(var k in el._attrs){if(k!='id'&&k!='class')a+=' '+k+'=\"'+el._attrs[k]+'\"';}"
        "var st=el.style.cssText||'';if(!st){var sk=[];for(var k in el.style){if(k!='cssText'&&typeof el.style[k]=='string'&&el.style[k]){sk.push(k.replace(/[A-Z]/g,function(m){return'-'+m.toLowerCase();})+':'+el.style[k]);}}st=sk.join(';');}"
        "if(st)a+=' style=\"'+st+'\"';var inner=el._ih||'';if(!inner){if(el._tc)inner=el._tc;for(var i=0;i<el.children.length;i++)inner+=el.children[i].toHTML();}"
        "if(t=='img'||t=='br'||t=='hr'||t=='input')return'<'+t+a+'>';return'<'+t+a+'>'+inner+'</'+t+'>';};"
        "Object.defineProperty(el,'innerHTML',{get:function(){return el._ih;},set:function(v){el._ih=String(v);if(el._root)_newos_emit(el._ih);}});"
        "Object.defineProperty(el,'textContent',{get:function(){return el._tc;},set:function(v){el._tc=String(v);if(el._root)_newos_emit(el._tc);}});"
        "Object.defineProperty(el,'innerText',{get:function(){return el._tc;},set:function(v){el._tc=String(v);}});"
        "el.appendChild=function(c){if(!c)return c;c.parentNode=el;el.children.push(c);el.childNodes.push(c);"
        "if(el._root||el.tagName=='BODY'){_newos_emit(c.toHTML());c._emitted=true;}return c;};"
        "el.insertBefore=function(c){return el.appendChild(c);};"
        "el.removeChild=function(c){var i=el.children.indexOf(c);if(i>=0)el.children.splice(i,1);return c;};"
        "el.remove=function(){if(el.parentNode)el.parentNode.removeChild(el);};"
        "el.querySelector=function(s){return matchOne(s,el)||makeEl('div');};"
        "el.querySelectorAll=function(s){return matchAll(s,el);};"
        "_all.push(el);return el;}"
        "function matchOne(s){if(!s)return null;if(s[0]=='#'&&_byId[s.slice(1)])return _byId[s.slice(1)];"
        "var c=(s[0]=='.')?s.slice(1):null;for(var i=0;i<_all.length;i++){if(c&&_all[i].classList.contains(c))return _all[i];if(!c&&_all[i].tagName.toLowerCase()==s.toLowerCase())return _all[i];}return null;}"
        "function matchAll(s){var r=[];if(!s)return r;var c=(s[0]=='.')?s.slice(1):null;for(var i=0;i<_all.length;i++){if((c&&_all[i].classList.contains(c))||(!c&&_all[i].tagName.toLowerCase()==s.toLowerCase()))r.push(_all[i]);}return r;}"
        "var body=makeEl('body');body._root=true;var head=makeEl('head');var docEl=makeEl('html');"
        "window.document={body:body,head:head,documentElement:docEl,readyState:'complete',cookie:'',referrer:'',URL:window.location.href,domain:window.location.hostname,"
        "write:function(s){_newos_emit(String(s));},writeln:function(s){_newos_emit(String(s)+'\\n');},"
        "createElement:function(t){return makeEl(t);},"
        "createTextNode:function(t){var n=makeEl('#text');n.nodeType=3;n._tc=String(t);return n;},"
        "createDocumentFragment:function(){return makeEl('#fragment');},"
        "createComment:function(){var n=makeEl('#comment');n.nodeType=8;return n;},"
        "getElementById:function(id){return _byId[id]||makeEl('div');},"
        "querySelector:function(s){return matchOne(s)||makeEl('div');},"
        "querySelectorAll:function(s){return matchAll(s);},"
        "getElementsByTagName:function(t){return matchAll(t);},"
        "getElementsByClassName:function(c){return matchAll('.'+c);},"
        "addEventListener:function(t,fn){if(t=='DOMContentLoaded'&&typeof fn=='function')_readyQ.push(fn);},"
        "removeEventListener:function(){}};"
        "window.getComputedStyle=function(el){return(el&&el.style)?el.style:{};};"
        "window.matchMedia=function(q){return{matches:false,media:q,addListener:function(){},addEventListener:function(){}};};"
        "window.history={pushState:function(){},replaceState:function(){},back:function(){},forward:function(){}};"
        "window.customElements={define:function(){},get:function(){},whenDefined:function(){return Promise.resolve();}};"
        "window.localStorage=(function(){var s={};return{getItem:function(k){return s.hasOwnProperty(k)?s[k]:null;},setItem:function(k,v){s[k]=String(v);},removeItem:function(k){delete s[k];},clear:function(){s={};}};})();"
        "window.sessionStorage=window.localStorage;"
        "window.MutationObserver=function(){this.observe=function(){};this.disconnect=function(){};this.takeRecords=function(){return[];};};"
        "window.ResizeObserver=function(){this.observe=function(){};this.unobserve=function(){};this.disconnect=function(){};};"
        "window.IntersectionObserver=function(){this.observe=function(){};this.unobserve=function(){};this.disconnect=function(){};};"
        "window.AbortController=function(){this.signal={aborted:false,addEventListener:function(){}};this.abort=function(){this.signal.aborted=true;};};"
        "window.Headers=function(){this._h={};this.set=function(k,v){this._h[k]=v;};this.get=function(k){return this._h[k]||null;};};"
        "window.FormData=function(){this._d={};this.append=function(k,v){this._d[k]=v;};this.get=function(k){return this._d[k]||null;};};"
        "window.fetch=function(){return Promise.resolve({ok:true,status:200,headers:new window.Headers(),text:function(){return Promise.resolve('');},json:function(){return Promise.resolve({});}});};"
        "window.XMLHttpRequest=function(){this.open=function(){};this.send=function(){};this.setRequestHeader=function(){};this.addEventListener=function(){};this.status=200;this.readyState=4;this.responseText='';};"
        "window.__newos_fire_ready=function(){for(var i=0;i<_readyQ.length;i++){try{_readyQ[i]();}catch(e){}}for(var j=0;j<_winQ.length;j++){try{_winQ[j]();}catch(e){}}};"
        "})();";
    JSValue pf_res = JS_Eval(ctx, web_apis_polyfill, strlen(web_apis_polyfill), "<webapi>", JS_EVAL_TYPE_GLOBAL);
    JS_FreeValue(ctx, pf_res);

    JSValue doc = JS_GetPropertyStr(ctx, global, "document");
    JSAtom prop_t = JS_NewAtom(ctx, "title");
    JS_DefinePropertyGetSet(ctx, doc, prop_t,
        JS_NewCFunction2(ctx, (JSCFunction *)qjs_doc_get_title, "get_title", 0, JS_CFUNC_getter, 0),
        JS_NewCFunction2(ctx, (JSCFunction *)qjs_doc_set_title, "set_title", 1, JS_CFUNC_setter, 0),
        JS_PROP_C_W_E);
    JS_FreeAtom(ctx, prop_t);
    JS_FreeValue(ctx, doc);
    JS_FreeValue(ctx, global);
}

static void js_dbg(const char *s) {
    while (*s) {
        unsigned char al = (unsigned char)*s++;
        __asm__ volatile("outb %0, %1" :: "a"(al), "Nd"((unsigned short)0x3F8));
    }
}

static int js_dispatch(const kernel_api_t *api, js_params_t *p) {
    if (!api || !p) return -1;
    g_js_api = api;
    g_js_params = p;
    if (p->cmd == JS_CMD_INIT) return 0;

    if (p->cmd == JS_CMD_RUN_PAGE && p->html_in && p->out_html && p->out_html_cap > 0) {
        p->out_html[0] = '\0';
        p->out_html_len = 0;
        p->has_title = false;
        p->out_title[0] = '\0';
        p->scripts_run = 0;
        p->dom_ops = 0;

        JSRuntime *rt = JS_NewRuntime();
        JSContext *ctx = NULL;
        if (rt) {
            JS_SetMemoryLimit(rt, 8 * 1024 * 1024);
            JS_SetMaxStackSize(rt, 128 * 1024);
            ctx = JS_NewContextRaw(rt);
            if (ctx) {
                JS_AddIntrinsicBaseObjects(ctx);
                JS_AddIntrinsicDate(ctx);
                JS_AddIntrinsicEval(ctx);
                JS_AddIntrinsicRegExp(ctx);
                JS_AddIntrinsicJSON(ctx);
                JS_AddIntrinsicProxy(ctx);
                JS_AddIntrinsicMapSet(ctx);
                JS_AddIntrinsicTypedArrays(ctx);
                JS_AddIntrinsicPromise(ctx);
                JS_AddIntrinsicBigInt(ctx);
                JS_AddIntrinsicWeakRef(ctx);
                qjs_setup_env(ctx, p->current_url);
            }
        }

        const char *cur = p->html_in;
        while (*cur) {
            while (*cur && !(*cur == '<' && (cur[1] == 's' || cur[1] == 'S') &&
                             j_strncasecmp(cur, "<script", 7) == 0)) {
                cur++;
            }
            if (!*cur) break;
            const char *st = cur;
            const char *tag_end = st;
            while (*tag_end && *tag_end != '>') tag_end++;
            if (*tag_end == '>') tag_end++;

            const char *en = tag_end;
            while (*en && !(*en == '<' && en[1] == '/' &&
                            j_strncasecmp(en, "</script>", 9) == 0)) {
                en++;
            }
            if (!*en) break;

            bool has_src = false;
            bool is_json_type = false;
            for (const char *a = st; a < tag_end - 4; a++) {
                if (j_strncasecmp(a, "src=", 4) == 0) has_src = true;
                if (j_strncasecmp(a, "application/json", 16) == 0 ||
                    j_strncasecmp(a, "application/ld+json", 19) == 0) is_json_type = true;
            }
            size_t slen = (size_t)(en - tag_end);
            if (!has_src && slen > 0) {
                p->scripts_run++;
                if (ctx && !is_json_type && slen < 131072 &&
                    !j_strstr_n(tag_end, slen, "ytInitialData")) {
                    char *code = (char *)malloc(slen + 1);
                    if (code) {
                        memcpy(code, tag_end, slen);
                        code[slen] = '\0';
                        js_dbg("[JS] eval\n");
                        JSValue res = JS_Eval(ctx, code, slen, "<inline>", JS_EVAL_TYPE_GLOBAL);
                        js_dbg("[JS] eval_done\n");
                        if (JS_IsException(res)) {
                            JSValue ex = JS_GetException(ctx);
                            JS_FreeValue(ctx, ex);
                        }
                        JS_FreeValue(ctx, res);
                        JSContext *ctx1 = NULL;
                        int jobs = 0;
                        while (jobs < 32 && JS_ExecutePendingJob(rt, &ctx1) > 0) jobs++;
                        free(code);
                    }
                }
                extract_spa_hydration(p, tag_end, slen);
            }
            cur = en + 9;
        }

        if (ctx) {
            const char *fire_ready = "if(window.__newos_fire_ready)window.__newos_fire_ready();";
            JSValue fr = JS_Eval(ctx, fire_ready, strlen(fire_ready), "<ready>", JS_EVAL_TYPE_GLOBAL);
            JS_FreeValue(ctx, fr);
            JSContext *ctx1 = NULL;
            int jobs = 0;
            while (jobs < 32 && JS_ExecutePendingJob(rt, &ctx1) > 0) jobs++;
        }

        if (ctx) JS_FreeContext(ctx);
        if (rt) JS_FreeRuntime(rt);
        js_dbg("[JS] page_done\n");

        // Fallback meta/SPA se nessun script ha prodotto output DOM ma la pagina e' vuota/SPA
        if (p->out_html_len == 0) {
            const char *og_t = j_strcasestr(p->html_in, "property=\"og:title\"");
            const char *m_desc = j_strcasestr(p->html_in, "name=\"description\"");
            if (og_t || m_desc || j_strcasestr(p->html_in, "ytInitialData")) {
                out_append(p, "<h2>");
                if (p->has_title && p->out_title[0]) out_append(p, p->out_title);
                else out_append(p, "Pagina Web Dinamica (SPA)");
                out_append(p, "</h2><hr>");
                if (m_desc) {
                    const char *c = j_strcasestr(m_desc, "content=\"");
                    if (c) {
                        c += 9;
                        const char *ce = c;
                        while (*ce && *ce != '"' && (size_t)(ce - c) < 280) ce++;
                        if (ce > c) {
                            char dbuf[284];
                            size_t dl = (size_t)(ce - c);
                            for (size_t k = 0; k < dl; k++) dbuf[k] = c[k];
                            dbuf[dl] = '\0';
                            out_append(p, "<p>");
                            out_append(p, dbuf);
                            out_append(p, "</p>");
                        }
                    }
                }
                if (j_strcasestr(p->html_in, "youtube.com") || j_strcasestr(p->html_in, "ytInitialData")) {
                    out_append(p, "<p>JSENGINE.DLL: Pagina YouTube SPA caricata via HTTPS TLS 1.3.</p>");
                    out_append(p, "<p><a href=\"https://www.youtube.com/results?search_query=newos\">Cerca 'newos' su YouTube</a></p>");
                }
            }
        }
        return 0;
    }

    return -1;
}
