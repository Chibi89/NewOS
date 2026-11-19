#include "../WebBrowser/netsurf_core.h"
#include <screen_modules.h>
#include <stdarg.h>
#include <hubbub/hubbub.h>
#include <hubbub/parser.h>
#include <hubbub/tree.h>
#include <libcss/libcss.h>

__attribute__((section(".header"), used)) module_header_t g_netsurf_header = {
    .magic = MODULE_MAGIC_NEWOS,
    .header_size = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type = MODULE_TYPE_SYSTEM_LIBRARY,
    .code_size = 0,
    .entry_offset = 64,
    .checksum = 0,
    .security_flags =
        SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name = "NETSURF_CORE",
    .signature = MODULE_AUTH_TAG};

static const kernel_api_t *g_ns_api = NULL;
static int netsurf_core_dispatch(const kernel_api_t *api, netsurf_core_params_t *p);

int module_main(const kernel_api_t *api, netsurf_core_params_t *p) {
    return netsurf_core_dispatch(api, p);
}

// ==============================================================================
// LIBC FREESTANDING SHIM PER LIBWAPCAPLET + LIBPARSERUTILS + LIBHUBBUB
// ==============================================================================
int errno = 0;

typedef struct {
    size_t size;
    size_t pad;
} ns_alloc_hdr_t;

void *malloc(size_t size) {
    if (!g_ns_api || !g_ns_api->kmalloc) return NULL;
    if (size == 0) size = 1;
    ns_alloc_hdr_t *h = (ns_alloc_hdr_t *)g_ns_api->kmalloc(sizeof(ns_alloc_hdr_t) + size);
    if (!h) return NULL;
    h->size = size;
    h->pad = 0x4E53414C;
    return (void *)(h + 1);
}

void free(void *ptr) {
    if (!ptr || !g_ns_api || !g_ns_api->kfree) return;
    ns_alloc_hdr_t *h = ((ns_alloc_hdr_t *)ptr) - 1;
    g_ns_api->kfree(h);
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
    ns_alloc_hdr_t *h = ((ns_alloc_hdr_t *)ptr) - 1;
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
    if (d < s) {
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    } else if (d > s) {
        for (size_t i = n; i > 0; i--) d[i - 1] = s[i - 1];
    }
    return dest;
}

void *memset(void *s, int c, size_t n) {
    uint8_t *d = (uint8_t *)s;
    for (size_t i = 0; i < n; i++) d[i] = (uint8_t)c;
    return s;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const uint8_t *a = (const uint8_t *)s1;
    const uint8_t *b = (const uint8_t *)s2;
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return (int)a[i] - (int)b[i];
    }
    return 0;
}

size_t strlen(const char *s) {
    size_t n = 0;
    while (s && s[n]) n++;
    return n;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && *s2 && *s1 == *s2) { s1++; s2++; }
    return (int)(unsigned char)*s1 - (int)(unsigned char)*s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i]) return (int)(unsigned char)s1[i] - (int)(unsigned char)s2[i];
        if (s1[i] == '\0') return 0;
    }
    return 0;
}

static char ns_tolower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

int strcasecmp(const char *s1, const char *s2) {
    while (*s1 && *s2 && ns_tolower(*s1) == ns_tolower(*s2)) { s1++; s2++; }
    return (int)(unsigned char)ns_tolower(*s1) - (int)(unsigned char)ns_tolower(*s2);
}

int strncasecmp(const char *s1, const char *s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        char a = ns_tolower(s1[i]), b = ns_tolower(s2[i]);
        if (a != b) return (int)(unsigned char)a - (int)(unsigned char)b;
        if (a == '\0') return 0;
    }
    return 0;
}

char *strcpy(char *dest, const char *src) {
    char *r = dest;
    while ((*dest++ = *src++));
    return r;
}

char *strncpy(char *dest, const char *src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i]; i++) dest[i] = src[i];
    for (; i < n; i++) dest[i] = '\0';
    return dest;
}

char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    return (c == 0) ? (char *)s : NULL;
}

char *strrchr(const char *s, int c) {
    const char *last = NULL;
    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    if (c == 0) return (char *)s;
    return (char *)last;
}

char *strstr(const char *hay, const char *needle) {
    if (!*needle) return (char *)hay;
    for (; *hay; hay++) {
        const char *h = hay, *n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return (char *)hay;
    }
    return NULL;
}

char *strdup(const char *s) {
    size_t l = strlen(s) + 1;
    char *d = (char *)malloc(l);
    if (d) memcpy(d, s, l);
    return d;
}

int atoi(const char *s) {
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    int sign = 1;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    int v = 0;
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return v * sign;
}

unsigned long strtoul(const char *nptr, char **endptr, int base) {
    const char *s = nptr;
    while (*s == ' ' || *s == '\t') s++;
    if (base == 0) {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
        else if (s[0] == '0') { base = 8; s++; }
        else base = 10;
    } else if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }
    unsigned long val = 0;
    while (*s) {
        int d = -1;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'z') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z') d = *s - 'A' + 10;
        if (d < 0 || d >= base) break;
        val = val * (unsigned long)base + (unsigned long)d;
        s++;
    }
    if (endptr) *endptr = (char *)s;
    return val;
}

long strtol(const char *nptr, char **endptr, int base) {
    const char *s = nptr;
    while (*s == ' ' || *s == '\t') s++;
    long sign = 1;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    unsigned long v = strtoul(s, endptr, base);
    return (long)v * sign;
}

void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *)) {
    uint8_t *arr = (uint8_t *)base;
    if (nmemb < 2 || size == 0) return;
    for (size_t i = 1; i < nmemb; i++) {
        for (size_t j = i; j > 0 && compar(arr + (j - 1) * size, arr + j * size) > 0; j--) {
            for (size_t k = 0; k < size; k++) {
                uint8_t t = arr[(j - 1) * size + k];
                arr[(j - 1) * size + k] = arr[j * size + k];
                arr[j * size + k] = t;
            }
        }
    }
}

void *bsearch(const void *key, const void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *)) {
    const uint8_t *arr = (const uint8_t *)base;
    size_t l = 0, r = nmemb;
    while (l < r) {
        size_t m = l + (r - l) / 2;
        int c = compar(key, arr + m * size);
        if (c == 0) return (void *)(arr + m * size);
        if (c < 0) r = m;
        else l = m + 1;
    }
    return NULL;
}

int vsnprintf(char *str, size_t size, const char *format, va_list ap) {
    (void)format; (void)ap;
    if (str && size > 0) str[0] = '\0';
    return 0;
}

int snprintf(char *str, size_t size, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int r = vsnprintf(str, size, format, ap);
    va_end(ap);
    return r;
}

// ==============================================================================
// ALBERO DOM LIBHUBBUB -> SERIALIZZAZIONE HTML5 NORMALIZZATA
// ==============================================================================
typedef enum {
    NS_NODE_DOC = 0,
    NS_NODE_ELEM,
    NS_NODE_TEXT,
    NS_NODE_COMMENT
} ns_node_kind_t;

typedef struct ns_node {
    ns_node_kind_t  kind;
    uint32_t        refcnt;
    char            tag[24];
    char           *text;
    char           *href;
    char           *alt;
    char           *id_attr;
    char           *class_attr;
    char           *style_attr;
    char           *type_attr;
    char           *val_attr;
    char           *src_attr;
    char           *width_attr;
    char           *height_attr;
    void           *libcss_data;
    lwc_string     *lwc_tag;
    lwc_string     *lwc_id;
    lwc_string     *lwc_classes[8];
    uint32_t        n_lwc_classes;
    struct ns_node *parent;
    struct ns_node *first_child;
    struct ns_node *last_child;
    struct ns_node *prev_sib;
    struct ns_node *next_sib;
    struct ns_node *alloc_next;
} ns_node_t;

typedef struct {
    ns_node_t *doc;
    ns_node_t *alloc_head;
    uint32_t   node_count;
} ns_tree_ctx_t;

static char *dup_hubbub_str(const uint8_t *ptr, size_t len) {
    char *s = (char *)malloc(len + 1);
    if (!s) return NULL;
    if (len > 0 && ptr) memcpy(s, ptr, len);
    s[len] = '\0';
    return s;
}

static ns_node_t *ns_alloc_node(ns_tree_ctx_t *tc, ns_node_kind_t kind) {
    ns_node_t *n = (ns_node_t *)calloc(1, sizeof(ns_node_t));
    if (!n) return NULL;
    n->kind = kind;
    n->refcnt = 1;
    n->alloc_next = tc->alloc_head;
    tc->alloc_head = n;
    tc->node_count++;
    return n;
}

static void ns_apply_attrs(ns_node_t *n, const hubbub_attribute *attrs, uint32_t n_attrs) {
    for (uint32_t i = 0; i < n_attrs; i++) {
        size_t nl = attrs[i].name.len;
        const uint8_t *np = attrs[i].name.ptr;
        if (nl == 4 && strncasecmp((const char *)np, "href", 4) == 0 && !n->href) {
            n->href = dup_hubbub_str(attrs[i].value.ptr, attrs[i].value.len);
        } else if (nl == 3 && strncasecmp((const char *)np, "alt", 3) == 0 && !n->alt) {
            n->alt = dup_hubbub_str(attrs[i].value.ptr, attrs[i].value.len);
        } else if (nl == 2 && strncasecmp((const char *)np, "id", 2) == 0 && !n->id_attr) {
            n->id_attr = dup_hubbub_str(attrs[i].value.ptr, attrs[i].value.len);
        } else if (nl == 5 && strncasecmp((const char *)np, "class", 5) == 0 && !n->class_attr) {
            n->class_attr = dup_hubbub_str(attrs[i].value.ptr, attrs[i].value.len);
        } else if (nl == 5 && strncasecmp((const char *)np, "style", 5) == 0 && !n->style_attr) {
            n->style_attr = dup_hubbub_str(attrs[i].value.ptr, attrs[i].value.len);
        } else if (nl == 4 && strncasecmp((const char *)np, "type", 4) == 0 && !n->type_attr) {
            n->type_attr = dup_hubbub_str(attrs[i].value.ptr, attrs[i].value.len);
        } else if (nl == 5 && strncasecmp((const char *)np, "value", 5) == 0 && !n->val_attr) {
            n->val_attr = dup_hubbub_str(attrs[i].value.ptr, attrs[i].value.len);
        } else if (nl == 11 && strncasecmp((const char *)np, "placeholder", 11) == 0 && !n->val_attr) {
            n->val_attr = dup_hubbub_str(attrs[i].value.ptr, attrs[i].value.len);
        } else if (nl == 3 && strncasecmp((const char *)np, "src", 3) == 0 && !n->src_attr) {
            n->src_attr = dup_hubbub_str(attrs[i].value.ptr, attrs[i].value.len);
        } else if (nl == 5 && strncasecmp((const char *)np, "width", 5) == 0 && !n->width_attr) {
            n->width_attr = dup_hubbub_str(attrs[i].value.ptr, attrs[i].value.len);
        } else if (nl == 6 && strncasecmp((const char *)np, "height", 6) == 0 && !n->height_attr) {
            n->height_attr = dup_hubbub_str(attrs[i].value.ptr, attrs[i].value.len);
        }
    }
}

static void ns_unlink(ns_node_t *child) {
    ns_node_t *p = child->parent;
    if (!p) return;
    if (child->prev_sib) child->prev_sib->next_sib = child->next_sib;
    else p->first_child = child->next_sib;
    if (child->next_sib) child->next_sib->prev_sib = child->prev_sib;
    else p->last_child = child->prev_sib;
    child->parent = NULL;
    child->prev_sib = NULL;
    child->next_sib = NULL;
}

static hubbub_error th_create_comment(void *ctx, const hubbub_string *data, void **result) {
    (void)data;
    ns_node_t *n = ns_alloc_node((ns_tree_ctx_t *)ctx, NS_NODE_COMMENT);
    if (!n) return HUBBUB_NOMEM;
    *result = n;
    return HUBBUB_OK;
}

static hubbub_error th_create_doctype(void *ctx, const hubbub_doctype *doctype, void **result) {
    (void)doctype;
    ns_node_t *n = ns_alloc_node((ns_tree_ctx_t *)ctx, NS_NODE_COMMENT);
    if (!n) return HUBBUB_NOMEM;
    *result = n;
    return HUBBUB_OK;
}

static hubbub_error th_create_element(void *ctx, const hubbub_tag *tag, void **result) {
    ns_node_t *n = ns_alloc_node((ns_tree_ctx_t *)ctx, NS_NODE_ELEM);
    if (!n) return HUBBUB_NOMEM;
    size_t tl = tag->name.len < sizeof(n->tag) - 1 ? tag->name.len : sizeof(n->tag) - 1;
    for (size_t i = 0; i < tl; i++) n->tag[i] = ns_tolower((char)tag->name.ptr[i]);
    n->tag[tl] = '\0';
    ns_apply_attrs(n, tag->attributes, tag->n_attributes);
    *result = n;
    return HUBBUB_OK;
}

static hubbub_error th_create_text(void *ctx, const hubbub_string *data, void **result) {
    ns_node_t *n = ns_alloc_node((ns_tree_ctx_t *)ctx, NS_NODE_TEXT);
    if (!n) return HUBBUB_NOMEM;
    n->text = dup_hubbub_str(data->ptr, data->len);
    *result = n;
    return HUBBUB_OK;
}

static hubbub_error th_ref_node(void *ctx, void *node) {
    (void)ctx;
    if (node) ((ns_node_t *)node)->refcnt++;
    return HUBBUB_OK;
}

static hubbub_error th_unref_node(void *ctx, void *node) {
    (void)ctx;
    if (node && ((ns_node_t *)node)->refcnt > 0) ((ns_node_t *)node)->refcnt--;
    return HUBBUB_OK;
}

static hubbub_error th_append_child(void *ctx, void *parent, void *child, void **result) {
    (void)ctx;
    ns_node_t *p = (ns_node_t *)parent;
    ns_node_t *c = (ns_node_t *)child;
    if (!p || !c) return HUBBUB_BADPARM;
    ns_unlink(c);
    c->parent = p;
    c->prev_sib = p->last_child;
    c->next_sib = NULL;
    if (p->last_child) p->last_child->next_sib = c;
    else p->first_child = c;
    p->last_child = c;
    c->refcnt++;
    *result = c;
    return HUBBUB_OK;
}

static hubbub_error th_insert_before(void *ctx, void *parent, void *child, void *ref_child, void **result) {
    (void)ctx;
    ns_node_t *p = (ns_node_t *)parent;
    ns_node_t *c = (ns_node_t *)child;
    ns_node_t *r = (ns_node_t *)ref_child;
    if (!p || !c) return HUBBUB_BADPARM;
    if (!r) return th_append_child(ctx, parent, child, result);
    ns_unlink(c);
    c->parent = p;
    c->prev_sib = r->prev_sib;
    c->next_sib = r;
    if (r->prev_sib) r->prev_sib->next_sib = c;
    else p->first_child = c;
    r->prev_sib = c;
    c->refcnt++;
    *result = c;
    return HUBBUB_OK;
}

static hubbub_error th_remove_child(void *ctx, void *parent, void *child, void **result) {
    (void)ctx; (void)parent;
    ns_node_t *c = (ns_node_t *)child;
    if (!c) return HUBBUB_BADPARM;
    ns_unlink(c);
    *result = c;
    return HUBBUB_OK;
}

static hubbub_error th_clone_node(void *ctx, void *node, bool deep, void **result) {
    (void)deep;
    ns_node_t *src = (ns_node_t *)node;
    ns_node_t *dst = ns_alloc_node((ns_tree_ctx_t *)ctx, src->kind);
    if (!dst) return HUBBUB_NOMEM;
    strcpy(dst->tag, src->tag);
    if (src->text) dst->text = strdup(src->text);
    if (src->href) dst->href = strdup(src->href);
    if (src->alt) dst->alt = strdup(src->alt);
    if (src->id_attr) dst->id_attr = strdup(src->id_attr);
    if (src->class_attr) dst->class_attr = strdup(src->class_attr);
    if (src->style_attr) dst->style_attr = strdup(src->style_attr);
    if (src->type_attr) dst->type_attr = strdup(src->type_attr);
    if (src->val_attr) dst->val_attr = strdup(src->val_attr);
    if (src->src_attr) dst->src_attr = strdup(src->src_attr);
    if (src->width_attr) dst->width_attr = strdup(src->width_attr);
    if (src->height_attr) dst->height_attr = strdup(src->height_attr);
    *result = dst;
    return HUBBUB_OK;
}

static hubbub_error th_reparent_children(void *ctx, void *node, void *new_parent) {
    (void)ctx;
    ns_node_t *src = (ns_node_t *)node;
    ns_node_t *dst = (ns_node_t *)new_parent;
    while (src->first_child) {
        void *res = NULL;
        th_append_child(ctx, dst, src->first_child, &res);
    }
    return HUBBUB_OK;
}

static hubbub_error th_get_parent(void *ctx, void *node, bool element_only, void **result) {
    (void)ctx; (void)element_only;
    ns_node_t *n = (ns_node_t *)node;
    *result = n ? n->parent : NULL;
    if (*result) ((ns_node_t *)(*result))->refcnt++;
    return HUBBUB_OK;
}

static hubbub_error th_has_children(void *ctx, void *node, bool *result) {
    (void)ctx;
    ns_node_t *n = (ns_node_t *)node;
    *result = (n && n->first_child != NULL);
    return HUBBUB_OK;
}

static hubbub_error th_form_associate(void *ctx, void *form, void *node) {
    (void)ctx; (void)form; (void)node;
    return HUBBUB_OK;
}

static hubbub_error th_add_attributes(void *ctx, void *node, const hubbub_attribute *attrs, uint32_t n_attrs) {
    (void)ctx;
    if (node) ns_apply_attrs((ns_node_t *)node, attrs, n_attrs);
    return HUBBUB_OK;
}

static hubbub_error th_set_quirks_mode(void *ctx, hubbub_quirks_mode mode) {
    (void)ctx; (void)mode;
    return HUBBUB_OK;
}

static hubbub_error th_encoding_change(void *ctx, const char *encname) {
    (void)ctx; (void)encname;
    return HUBBUB_OK;
}

static hubbub_error th_complete_script(void *ctx, void *script) {
    (void)ctx; (void)script;
    return HUBBUB_OK;
}

// ==============================================================================
// INTEGRAZIONE LIBCSS (SELETTORI CSS3 + CASCATA REALE NETSURF)
// ==============================================================================
static css_error ns_css_resolve_url(void *pw, const char *base, lwc_string *rel, lwc_string **abs) {
    (void)pw; (void)base;
    *abs = lwc_string_ref(rel);
    return CSS_OK;
}

static void ns_intern_node(ns_node_t *n) {
    if (!n || n->kind != NS_NODE_ELEM) return;
    if (!n->lwc_tag && n->tag[0]) {
        lwc_intern_string(n->tag, strlen(n->tag), &n->lwc_tag);
    }
    if (!n->lwc_id && n->id_attr && n->id_attr[0]) {
        lwc_intern_string(n->id_attr, strlen(n->id_attr), &n->lwc_id);
    }
    if (n->n_lwc_classes == 0 && n->class_attr && n->class_attr[0]) {
        const char *p = n->class_attr;
        while (*p && n->n_lwc_classes < 8) {
            while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
            if (!*p) break;
            const char *s = p;
            while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') p++;
            if (p > s) {
                lwc_string *cs = NULL;
                if (lwc_intern_string(s, (size_t)(p - s), &cs) == lwc_error_ok && cs) {
                    n->lwc_classes[n->n_lwc_classes++] = cs;
                }
            }
        }
    }
}

static css_error sel_node_name(void *pw, void *node, css_qname *qname) {
    (void)pw;
    ns_node_t *n = (ns_node_t *)node;
    ns_intern_node(n);
    qname->ns = NULL;
    qname->name = n->lwc_tag ? lwc_string_ref(n->lwc_tag) : NULL;
    return CSS_OK;
}

static css_error sel_node_classes(void *pw, void *node, lwc_string ***classes, uint32_t *n_classes) {
    (void)pw;
    ns_node_t *n = (ns_node_t *)node;
    ns_intern_node(n);
    *n_classes = n->n_lwc_classes;
    if (n->n_lwc_classes == 0) {
        *classes = NULL;
        return CSS_OK;
    }
    *classes = (lwc_string **)malloc(sizeof(lwc_string *) * n->n_lwc_classes);
    if (!*classes) { *n_classes = 0; return CSS_NOMEM; }
    for (uint32_t i = 0; i < n->n_lwc_classes; i++) {
        (*classes)[i] = lwc_string_ref(n->lwc_classes[i]);
    }
    return CSS_OK;
}

static css_error sel_node_id(void *pw, void *node, lwc_string **id) {
    (void)pw;
    ns_node_t *n = (ns_node_t *)node;
    ns_intern_node(n);
    *id = n->lwc_id ? lwc_string_ref(n->lwc_id) : NULL;
    return CSS_OK;
}

static css_error sel_named_ancestor_node(void *pw, void *node, const css_qname *qname, void **ancestor) {
    (void)pw;
    ns_node_t *n = ((ns_node_t *)node)->parent;
    *ancestor = NULL;
    while (n && n->kind == NS_NODE_ELEM) {
        ns_intern_node(n);
        bool eq = false;
        if (n->lwc_tag && lwc_string_caseless_isequal(n->lwc_tag, qname->name, &eq) == lwc_error_ok && eq) {
            *ancestor = n;
            return CSS_OK;
        }
        n = n->parent;
    }
    return CSS_OK;
}

static css_error sel_named_parent_node(void *pw, void *node, const css_qname *qname, void **parent) {
    (void)pw;
    ns_node_t *n = ((ns_node_t *)node)->parent;
    *parent = NULL;
    if (n && n->kind == NS_NODE_ELEM) {
        ns_intern_node(n);
        bool eq = false;
        if (n->lwc_tag && lwc_string_caseless_isequal(n->lwc_tag, qname->name, &eq) == lwc_error_ok && eq) {
            *parent = n;
        }
    }
    return CSS_OK;
}

static css_error sel_named_sibling_node(void *pw, void *node, const css_qname *qname, void **sibling) {
    (void)pw;
    ns_node_t *n = ((ns_node_t *)node)->prev_sib;
    *sibling = NULL;
    while (n && n->kind != NS_NODE_ELEM) n = n->prev_sib;
    if (n && n->kind == NS_NODE_ELEM) {
        ns_intern_node(n);
        bool eq = false;
        if (n->lwc_tag && lwc_string_caseless_isequal(n->lwc_tag, qname->name, &eq) == lwc_error_ok && eq) {
            *sibling = n;
        }
    }
    return CSS_OK;
}

static css_error sel_named_generic_sibling_node(void *pw, void *node, const css_qname *qname, void **sibling) {
    (void)pw;
    ns_node_t *n = ((ns_node_t *)node)->prev_sib;
    *sibling = NULL;
    while (n) {
        if (n->kind == NS_NODE_ELEM) {
            ns_intern_node(n);
            bool eq = false;
            if (n->lwc_tag && lwc_string_caseless_isequal(n->lwc_tag, qname->name, &eq) == lwc_error_ok && eq) {
                *sibling = n;
                return CSS_OK;
            }
        }
        n = n->prev_sib;
    }
    return CSS_OK;
}

static css_error sel_parent_node(void *pw, void *node, void **parent) {
    (void)pw;
    ns_node_t *p = ((ns_node_t *)node)->parent;
    *parent = (p && p->kind == NS_NODE_ELEM) ? p : NULL;
    return CSS_OK;
}

static css_error sel_sibling_node(void *pw, void *node, void **sibling) {
    (void)pw;
    ns_node_t *s = ((ns_node_t *)node)->prev_sib;
    while (s && s->kind != NS_NODE_ELEM) s = s->prev_sib;
    *sibling = s;
    return CSS_OK;
}

static css_error sel_node_has_name(void *pw, void *node, const css_qname *qname, bool *match) {
    (void)pw;
    ns_node_t *n = (ns_node_t *)node;
    *match = false;
    if (n->kind == NS_NODE_ELEM) {
        ns_intern_node(n);
        if (n->lwc_tag) lwc_string_caseless_isequal(n->lwc_tag, qname->name, match);
    }
    return CSS_OK;
}

static css_error sel_node_has_class(void *pw, void *node, lwc_string *name, bool *match) {
    (void)pw;
    ns_node_t *n = (ns_node_t *)node;
    ns_intern_node(n);
    *match = false;
    for (uint32_t i = 0; i < n->n_lwc_classes; i++) {
        bool eq = false;
        if (lwc_string_isequal(n->lwc_classes[i], name, &eq) == lwc_error_ok && eq) {
            *match = true;
            return CSS_OK;
        }
    }
    return CSS_OK;
}

static css_error sel_node_has_id(void *pw, void *node, lwc_string *name, bool *match) {
    (void)pw;
    ns_node_t *n = (ns_node_t *)node;
    ns_intern_node(n);
    *match = false;
    if (n->lwc_id) lwc_string_isequal(n->lwc_id, name, match);
    return CSS_OK;
}

static css_error sel_node_has_attr(void *pw, void *node, const css_qname *qname, bool *match) {
    (void)pw; (void)node; (void)qname;
    *match = false;
    return CSS_OK;
}

static css_error sel_node_has_attr_val(void *pw, void *node, const css_qname *qname, lwc_string *value, bool *match) {
    (void)pw; (void)node; (void)qname; (void)value;
    *match = false;
    return CSS_OK;
}

static css_error sel_node_is_root(void *pw, void *node, bool *match) {
    (void)pw;
    ns_node_t *n = (ns_node_t *)node;
    *match = (n->parent && n->parent->kind == NS_NODE_DOC);
    return CSS_OK;
}

static css_error sel_node_count_siblings(void *pw, void *node, bool same_name, bool after, int32_t *count) {
    (void)pw; (void)node; (void)same_name; (void)after;
    *count = 0;
    return CSS_OK;
}

static css_error sel_node_is_empty(void *pw, void *node, bool *match) {
    (void)pw;
    ns_node_t *n = (ns_node_t *)node;
    *match = (n->first_child == NULL);
    return CSS_OK;
}

static css_error sel_node_is_link(void *pw, void *node, bool *match) {
    (void)pw;
    ns_node_t *n = (ns_node_t *)node;
    *match = (n->kind == NS_NODE_ELEM && strcmp(n->tag, "a") == 0 && n->href != NULL);
    return CSS_OK;
}

static css_error sel_node_false(void *pw, void *node, bool *match) {
    (void)pw; (void)node;
    *match = false;
    return CSS_OK;
}

static css_error sel_node_is_lang(void *pw, void *node, lwc_string *lang, bool *match) {
    (void)pw; (void)node; (void)lang;
    *match = false;
    return CSS_OK;
}

static css_error sel_node_presentational_hint(void *pw, void *node, uint32_t *nhints, css_hint **hints) {
    (void)pw; (void)node;
    *nhints = 0;
    *hints = NULL;
    return CSS_OK;
}

static css_error sel_ua_default(void *pw, uint32_t property, css_hint *hint) {
    (void)pw;
    if (property == CSS_PROP_COLOR) {
        hint->data.color = 0xFF000000;
        hint->status = CSS_COLOR_COLOR;
        return CSS_OK;
    } else if (property == CSS_PROP_BACKGROUND_COLOR) {
        hint->data.color = 0x00000000;
        hint->status = CSS_BACKGROUND_COLOR_COLOR;
        return CSS_OK;
    } else if (property == CSS_PROP_DISPLAY) {
        hint->status = CSS_DISPLAY_INLINE;
        return CSS_OK;
    }
    return CSS_PROPERTY_NOT_SET;
}

static css_error sel_set_node_data(void *pw, void *node, void *libcss_node_data) {
    (void)pw;
    ((ns_node_t *)node)->libcss_data = libcss_node_data;
    return CSS_OK;
}

static css_error sel_get_node_data(void *pw, void *node, void **libcss_node_data) {
    (void)pw;
    *libcss_node_data = ((ns_node_t *)node)->libcss_data;
    return CSS_OK;
}

static css_select_handler g_css_handler = {
    .handler_version = CSS_SELECT_HANDLER_VERSION_1,
    .node_name = sel_node_name,
    .node_classes = sel_node_classes,
    .node_id = sel_node_id,
    .named_ancestor_node = sel_named_ancestor_node,
    .named_parent_node = sel_named_parent_node,
    .named_sibling_node = sel_named_sibling_node,
    .named_generic_sibling_node = sel_named_generic_sibling_node,
    .parent_node = sel_parent_node,
    .sibling_node = sel_sibling_node,
    .node_has_name = sel_node_has_name,
    .node_has_class = sel_node_has_class,
    .node_has_id = sel_node_has_id,
    .node_has_attribute = sel_node_has_attr,
    .node_has_attribute_equal = sel_node_has_attr_val,
    .node_has_attribute_dashmatch = sel_node_has_attr_val,
    .node_has_attribute_includes = sel_node_has_attr_val,
    .node_has_attribute_prefix = sel_node_has_attr_val,
    .node_has_attribute_suffix = sel_node_has_attr_val,
    .node_has_attribute_substring = sel_node_has_attr_val,
    .node_is_root = sel_node_is_root,
    .node_count_siblings = sel_node_count_siblings,
    .node_is_empty = sel_node_is_empty,
    .node_is_link = sel_node_is_link,
    .node_is_visited = sel_node_false,
    .node_is_hover = sel_node_false,
    .node_is_active = sel_node_false,
    .node_is_focus = sel_node_false,
    .node_is_enabled = sel_node_false,
    .node_is_disabled = sel_node_false,
    .node_is_checked = sel_node_false,
    .node_is_target = sel_node_false,
    .node_is_lang = sel_node_is_lang,
    .node_presentational_hint = sel_node_presentational_hint,
    .ua_default_for_property = sel_ua_default,
    .set_libcss_node_data = sel_set_node_data,
    .get_libcss_node_data = sel_get_node_data
};

static void fmt_hex_color(uint32_t argb, char out[8]) {
    static const char hx[] = "0123456789ABCDEF";
    out[0] = '#';
    out[1] = hx[(argb >> 20) & 0xF];
    out[2] = hx[(argb >> 16) & 0xF];
    out[3] = hx[(argb >> 12) & 0xF];
    out[4] = hx[(argb >> 8) & 0xF];
    out[5] = hx[(argb >> 4) & 0xF];
    out[6] = hx[argb & 0xF];
    out[7] = '\0';
}

#define MAX_NS_SHEETS 16

static void collect_css_sheets(ns_node_t *n, css_select_ctx *sel_ctx,
                               css_stylesheet **sheets, int *num_sheets) {
    if (!n || *num_sheets >= MAX_NS_SHEETS) return;
    if (n->kind == NS_NODE_ELEM && strcmp(n->tag, "style") == 0) {
        css_stylesheet_params params;
        memset(&params, 0, sizeof(params));
        params.params_version = CSS_STYLESHEET_PARAMS_VERSION_1;
        params.level = CSS_LEVEL_DEFAULT;
        params.charset = "UTF-8";
        params.url = "about:blank";
        params.allow_quirks = true;
        params.resolve = ns_css_resolve_url;

        css_stylesheet *sheet = NULL;
        if (css_stylesheet_create(&params, &sheet) == CSS_OK && sheet) {
            for (ns_node_t *c = n->first_child; c; c = c->next_sib) {
                if (c->kind == NS_NODE_TEXT && c->text) {
                    css_stylesheet_append_data(sheet, (const uint8_t *)c->text, strlen(c->text));
                }
            }
            css_stylesheet_data_done(sheet);
            css_select_ctx_append_sheet(sel_ctx, sheet, CSS_ORIGIN_AUTHOR, NULL);
            sheets[(*num_sheets)++] = sheet;
        }
        return;
    }
    for (ns_node_t *c = n->first_child; c; c = c->next_sib) {
        collect_css_sheets(c, sel_ctx, sheets, num_sheets);
    }
}

static void out_str(netsurf_core_params_t *p, const char *s) {
    if (!p->out_html || !s) return;
    while (*s && p->out_html_len + 1 < p->out_html_cap) {
        p->out_html[p->out_html_len++] = *s++;
    }
    p->out_html[p->out_html_len] = '\0';
}

static void out_escaped(netsurf_core_params_t *p, const char *s) {
    if (!s) return;
    while (*s && p->out_html_len + 6 < p->out_html_cap) {
        if (*s == '<') out_str(p, "&lt;");
        else if (*s == '>') out_str(p, "&gt;");
        else if (*s == '&') out_str(p, "&amp;");
        else {
            p->out_html[p->out_html_len++] = *s;
            p->out_html[p->out_html_len] = '\0';
        }
        s++;
    }
}

static void serialize_dom(ns_node_t *n, netsurf_core_params_t *p, int depth,
                          css_select_ctx *sel_ctx, const css_unit_ctx *uctx,
                          const css_media *media) {
    if (!n || depth > 48 || p->out_html_len + 64 >= p->out_html_cap) return;
    if (n->kind == NS_NODE_COMMENT) return;
    if (n->kind == NS_NODE_TEXT) {
        out_escaped(p, n->text);
        return;
    }

    if (n->kind == NS_NODE_ELEM) {
        if (strcmp(n->tag, "script") == 0 || strcmp(n->tag, "style") == 0 ||
            strcmp(n->tag, "svg") == 0 || strcmp(n->tag, "noscript") == 0 ||
            strcmp(n->tag, "template") == 0) {
            return;
        }
        if (strcmp(n->tag, "title") == 0) {
            if (!p->has_title && n->first_child && n->first_child->kind == NS_NODE_TEXT && n->first_child->text) {
                const char *t = n->first_child->text;
                while (*t == ' ' || *t == '\n' || *t == '\r' || *t == '\t') t++;
                size_t tl = strlen(t);
                while (tl > 0 && (t[tl - 1] == ' ' || t[tl - 1] == '\n' || t[tl - 1] == '\r' || t[tl - 1] == '\t')) tl--;
                if (tl >= sizeof(p->out_title)) tl = sizeof(p->out_title) - 1;
                if (tl > 0) {
                    memcpy(p->out_title, t, tl);
                    p->out_title[tl] = '\0';
                    p->has_title = true;
                }
            }
            return;
        }
        if (strcmp(n->tag, "head") == 0) {
            for (ns_node_t *c = n->first_child; c; c = c->next_sib) {
                if (c->kind == NS_NODE_ELEM && strcmp(c->tag, "title") == 0)
                    serialize_dom(c, p, depth + 1, sel_ctx, uctx, media);
            }
            return;
        }

        // Selezione stile CSS3 reale tramite NetSurf libcss
        char computed_style_buf[256] = "";
        if (sel_ctx && (strcmp(n->tag, "body") == 0 || strcmp(n->tag, "div") == 0 ||
                        strcmp(n->tag, "p") == 0 || strcmp(n->tag, "h1") == 0 ||
                        strcmp(n->tag, "h2") == 0 || strcmp(n->tag, "a") == 0 ||
                        strcmp(n->tag, "span") == 0 || strcmp(n->tag, "header") == 0 ||
                        strcmp(n->tag, "nav") == 0 || strcmp(n->tag, "section") == 0 ||
                        strcmp(n->tag, "article") == 0 || strcmp(n->tag, "main") == 0 ||
                        strcmp(n->tag, "footer") == 0 || strcmp(n->tag, "button") == 0 ||
                        strcmp(n->tag, "aside") == 0 || strcmp(n->tag, "input") == 0 ||
                        strcmp(n->tag, "strong") == 0 || strcmp(n->tag, "b") == 0 ||
                        strcmp(n->tag, "small") == 0 ||
                        strcmp(n->tag, "ul") == 0 || strcmp(n->tag, "li") == 0)) {
            css_select_results *sres = NULL;
            if (css_select_style(sel_ctx, n, uctx, media, NULL, &g_css_handler, NULL, &sres) == CSS_OK && sres) {
                const css_computed_style *st = sres->styles[CSS_PSEUDO_ELEMENT_NONE];
                if (st) {
                    uint8_t disp = css_computed_display(st, false);
                    if (disp == CSS_DISPLAY_NONE) {
                        css_select_results_destroy(sres);
                        return;
                    }
                    if (disp == CSS_DISPLAY_FLEX || disp == CSS_DISPLAY_INLINE_FLEX) {
                        strcpy(computed_style_buf, "display:flex;");
                    } else if (disp == CSS_DISPLAY_GRID || disp == CSS_DISPLAY_INLINE_GRID) {
                        strcpy(computed_style_buf, "display:grid;");
                    } else if (disp == CSS_DISPLAY_INLINE_BLOCK) {
                        strcpy(computed_style_buf, "display:inline-block;");
                    }
                    css_color col = 0, bg = 0;
                    if (css_computed_color(st, &col) == CSS_COLOR_COLOR && (col & 0xFFFFFF) != 0) {
                        char hx[8]; fmt_hex_color(col, hx);
                        size_t cl = strlen(computed_style_buf);
                        strcpy(computed_style_buf + cl, "color:");
                        strcpy(computed_style_buf + cl + 6, hx);
                        strcpy(computed_style_buf + cl + 13, ";");
                    }
                    if (css_computed_background_color(st, &bg) == CSS_BACKGROUND_COLOR_COLOR &&
                        ((bg >> 24) & 0xFF) != 0) {
                        char hx[8]; fmt_hex_color(bg, hx);
                        size_t cl = strlen(computed_style_buf);
                        strcpy(computed_style_buf + cl, "background-color:");
                        strcpy(computed_style_buf + cl + 17, hx);
                        strcpy(computed_style_buf + cl + 24, ";");
                    }
                    uint8_t tdec = css_computed_text_decoration(st);
                    if (tdec == CSS_TEXT_DECORATION_NONE && strcmp(n->tag, "a") == 0) {
                        strcpy(computed_style_buf + strlen(computed_style_buf), "text-decoration:none;");
                    }
                    uint8_t fw = css_computed_font_weight(st);
                    if (fw == CSS_FONT_WEIGHT_BOLD || fw == CSS_FONT_WEIGHT_BOLDER ||
                        fw == CSS_FONT_WEIGHT_600 || fw == CSS_FONT_WEIGHT_700 ||
                        fw == CSS_FONT_WEIGHT_800 || fw == CSS_FONT_WEIGHT_900) {
                        strcpy(computed_style_buf + strlen(computed_style_buf), "font-weight:bold;");
                    }
                    css_fixed fs_val = 0;
                    css_unit fs_unit = CSS_UNIT_PX;
                    if (css_computed_font_size(st, &fs_val, &fs_unit) == CSS_FONT_SIZE_DIMENSION &&
                        fs_unit == CSS_UNIT_PX) {
                        int px = (int)(fs_val >> 10);
                        if (px >= 10 && px <= 36) {
                            size_t cl = strlen(computed_style_buf);
                            strcpy(computed_style_buf + cl, "font-size:");
                            cl += 10;
                            computed_style_buf[cl++] = (char)('0' + (px / 10));
                            computed_style_buf[cl++] = (char)('0' + (px % 10));
                            strcpy(computed_style_buf + cl, "px;");
                        }
                    }
                    uint8_t bstyle = css_computed_border_top_style(st);
                    if (bstyle != CSS_BORDER_STYLE_NONE && bstyle != CSS_BORDER_STYLE_HIDDEN) {
                        css_color bc = 0;
                        if (css_computed_border_top_color(st, &bc) == CSS_BORDER_COLOR_COLOR) {
                            char hx[8]; fmt_hex_color(bc, hx);
                            size_t cl = strlen(computed_style_buf);
                            strcpy(computed_style_buf + cl, "border:1px solid ");
                            strcpy(computed_style_buf + cl + 17, hx);
                            strcpy(computed_style_buf + cl + 24, ";");
                        }
                    }
                }
                css_select_results_destroy(sres);
            }
        }

        out_str(p, "<");
        out_str(p, n->tag);
        if (n->id_attr && n->id_attr[0]) { out_str(p, " id=\""); out_str(p, n->id_attr); out_str(p, "\""); }
        if (n->class_attr && n->class_attr[0]) { out_str(p, " class=\""); out_str(p, n->class_attr); out_str(p, "\""); }
        if (computed_style_buf[0] || (n->style_attr && n->style_attr[0])) {
            out_str(p, " style=\"");
            if (computed_style_buf[0]) out_str(p, computed_style_buf);
            if (n->style_attr && n->style_attr[0]) out_str(p, n->style_attr);
            out_str(p, "\"");
        }
        if (n->href && n->href[0]) { out_str(p, " href=\""); out_str(p, n->href); out_str(p, "\""); }
        if (n->alt && n->alt[0]) { out_str(p, " alt=\""); out_str(p, n->alt); out_str(p, "\""); }
        if (n->type_attr && n->type_attr[0]) { out_str(p, " type=\""); out_str(p, n->type_attr); out_str(p, "\""); }
        if (n->val_attr && n->val_attr[0]) { out_str(p, " value=\""); out_str(p, n->val_attr); out_str(p, "\""); }
        if (n->src_attr && n->src_attr[0]) { out_str(p, " src=\""); out_str(p, n->src_attr); out_str(p, "\""); }
        if (n->width_attr && n->width_attr[0]) { out_str(p, " width=\""); out_str(p, n->width_attr); out_str(p, "\""); }
        if (n->height_attr && n->height_attr[0]) { out_str(p, " height=\""); out_str(p, n->height_attr); out_str(p, "\""); }
        out_str(p, ">");

        if (strcmp(n->tag, "br") == 0 || strcmp(n->tag, "hr") == 0 ||
            strcmp(n->tag, "img") == 0 || strcmp(n->tag, "input") == 0) {
            return;
        }

        for (ns_node_t *c = n->first_child; c; c = c->next_sib) {
            serialize_dom(c, p, depth + 1, sel_ctx, uctx, media);
        }

        out_str(p, "</");
        out_str(p, n->tag);
        out_str(p, ">");
        return;
    }

    for (ns_node_t *c = n->first_child; c; c = c->next_sib) {
        serialize_dom(c, p, depth + 1, sel_ctx, uctx, media);
    }
}

static int netsurf_core_dispatch(const kernel_api_t *api, netsurf_core_params_t *p) {
    g_ns_api = api;
    if (!api || !p) return -1;
    if (p->cmd == NS_CMD_INIT) return 0;

    if (p->cmd == NS_CMD_PARSE_HTML && p->html_in && p->out_html && p->out_html_cap > 0) {
        p->out_html[0] = '\0';
        p->out_html_len = 0;
        p->has_title = false;
        p->out_title[0] = '\0';
        p->node_count = 0;

        hubbub_parser *parser = NULL;
        if (hubbub_parser_create("UTF-8", false, &parser) != HUBBUB_OK || !parser) {
            return -1;
        }

        ns_tree_ctx_t tctx;
        memset(&tctx, 0, sizeof(tctx));
        tctx.doc = ns_alloc_node(&tctx, NS_NODE_DOC);

        hubbub_tree_handler th;
        memset(&th, 0, sizeof(th));
        th.create_comment = th_create_comment;
        th.create_doctype = th_create_doctype;
        th.create_element = th_create_element;
        th.create_text = th_create_text;
        th.ref_node = th_ref_node;
        th.unref_node = th_unref_node;
        th.append_child = th_append_child;
        th.insert_before = th_insert_before;
        th.remove_child = th_remove_child;
        th.clone_node = th_clone_node;
        th.reparent_children = th_reparent_children;
        th.get_parent = th_get_parent;
        th.has_children = th_has_children;
        th.form_associate = th_form_associate;
        th.add_attributes = th_add_attributes;
        th.set_quirks_mode = th_set_quirks_mode;
        th.encoding_change = th_encoding_change;
        th.complete_script = th_complete_script;
        th.ctx = &tctx;

        hubbub_parser_optparams opt;
        opt.tree_handler = &th;
        hubbub_parser_setopt(parser, HUBBUB_PARSER_TREE_HANDLER, &opt);
        opt.document_node = tctx.doc;
        hubbub_parser_setopt(parser, HUBBUB_PARSER_DOCUMENT_NODE, &opt);

        hubbub_parser_parse_chunk(parser, (const uint8_t *)p->html_in, p->html_len);
        hubbub_parser_completed(parser);
        hubbub_parser_destroy(parser);

        p->node_count = tctx.node_count;

        css_select_ctx *sel_ctx = NULL;
        css_stylesheet *sheets[MAX_NS_SHEETS];
        int num_sheets = 0;
        if (css_select_ctx_create(&sel_ctx) == CSS_OK && sel_ctx) {
            collect_css_sheets(tctx.doc, sel_ctx, sheets, &num_sheets);
        }
        css_unit_ctx uctx = {
            .viewport_width = INTTOFIX(800),
            .viewport_height = INTTOFIX(600),
            .font_size_default = INTTOFIX(16),
            .font_size_minimum = INTTOFIX(10),
            .device_dpi = INTTOFIX(96),
            .root_style = NULL,
            .pw = NULL,
            .measure = NULL
        };

        css_media media;
        memset(&media, 0, sizeof(media));
        media.type = CSS_MEDIA_SCREEN;
        media.width = INTTOFIX(800);
        media.height = INTTOFIX(600);
        media.color = INTTOFIX(24);

        serialize_dom(tctx.doc, p, 0, (num_sheets > 0) ? sel_ctx : NULL, &uctx, &media);

        ns_node_t *cur = tctx.alloc_head;
        while (cur) {
            ns_node_t *nxt = cur->alloc_next;
            if (cur->libcss_data) {
                css_libcss_node_data_handler(&g_css_handler, CSS_NODE_DELETED, NULL, cur, NULL, cur->libcss_data);
            }
            if (cur->lwc_tag) lwc_string_unref(cur->lwc_tag);
            if (cur->lwc_id) lwc_string_unref(cur->lwc_id);
            for (uint32_t ci = 0; ci < cur->n_lwc_classes; ci++) {
                if (cur->lwc_classes[ci]) lwc_string_unref(cur->lwc_classes[ci]);
            }
            if (cur->text) free(cur->text);
            if (cur->href) free(cur->href);
            if (cur->alt) free(cur->alt);
            if (cur->id_attr) free(cur->id_attr);
            if (cur->class_attr) free(cur->class_attr);
            if (cur->style_attr) free(cur->style_attr);
            if (cur->type_attr) free(cur->type_attr);
            if (cur->val_attr) free(cur->val_attr);
            if (cur->src_attr) free(cur->src_attr);
            if (cur->width_attr) free(cur->width_attr);
            if (cur->height_attr) free(cur->height_attr);
            free(cur);
            cur = nxt;
        }
        if (sel_ctx) css_select_ctx_destroy(sel_ctx);
        for (int si = 0; si < num_sheets; si++) {
            if (sheets[si]) css_stylesheet_destroy(sheets[si]);
        }
        return 0;
    }

    return -1;
}
