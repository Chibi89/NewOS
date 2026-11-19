#include "../WebBrowser/cssengine.h"
#include <screen_modules.h>

__attribute__((section(".header"), used)) module_header_t g_cssengine_header = {
    .magic = MODULE_MAGIC_NEWOS,
    .header_size = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type = MODULE_TYPE_SYSTEM_LIBRARY,
    .code_size = 0,
    .entry_offset = 64,
    .checksum = 0,
    .security_flags =
        SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name = "CSSENGINE",
    .signature = MODULE_AUTH_TAG};

static int css_dispatch(const kernel_api_t *api, css_params_t *p);

int module_main(const kernel_api_t *api, css_params_t *p) {
  return css_dispatch(api, p);
}

// ==============================================================================
// UTILITA' STRINGHE FREESTANDING
// ==============================================================================
static void c_memset(void *dst, int v, size_t n) {
  uint8_t *d = (uint8_t *)dst;
  for (size_t i = 0; i < n; i++)
    d[i] = (uint8_t)v;
}

static size_t c_strlen(const char *s) {
  size_t n = 0;
  if (!s)
    return 0;
  while (s[n])
    n++;
  return n;
}

static void c_strcpy(char *dst, const char *src, size_t max_len) {
  if (!dst || max_len == 0)
    return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  size_t i = 0;
  while (src[i] && i + 1 < max_len) {
    dst[i] = src[i];
    i++;
  }
  dst[i] = '\0';
}

static char c_tolower(char c) {
  return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

static int c_strcasecmp(const char *a, const char *b) {
  while (*a && *b) {
    char ca = c_tolower(*a), cb = c_tolower(*b);
    if (ca != cb)
      return (int)(unsigned char)ca - (int)(unsigned char)cb;
    a++;
    b++;
  }
  return (int)(unsigned char)c_tolower(*a) - (int)(unsigned char)c_tolower(*b);
}

static int c_strncasecmp(const char *a, const char *b, size_t n) {
  for (size_t i = 0; i < n; i++) {
    char ca = c_tolower(a[i]), cb = c_tolower(b[i]);
    if (ca != cb)
      return (int)(unsigned char)ca - (int)(unsigned char)cb;
    if (ca == '\0')
      return 0;
  }
  return 0;
}

static const char *c_strcasestr(const char *hay, const char *needle) {
  if (!hay || !needle || !*needle)
    return hay;
  for (const char *p = hay; *p; p++) {
    const char *h = p, *n = needle;
    while (*h && *n && c_tolower(*h) == c_tolower(*n)) {
      h++;
      n++;
    }
    if (!*n)
      return p;
  }
  return NULL;
}

static bool c_isspace(char c) {
  return (c == ' ' || c == '\t' || c == '\r' || c == '\n');
}

static bool c_isdigit(char c) { return (c >= '0' && c <= '9'); }

static int c_atoi(const char *s) {
  while (*s && c_isspace(*s))
    s++;
  int sign = 1;
  if (*s == '-') {
    sign = -1;
    s++;
  } else if (*s == '+')
    s++;
  int val = 0;
  while (*s >= '0' && *s <= '9') {
    val = val * 10 + (*s - '0');
    s++;
  }
  return val * sign;
}

// Parse numero decimale in millesimi (es. "0.5" -> 500, "1.25" -> 1250)
static int c_parse_milli(const char *s) {
  while (*s && c_isspace(*s))
    s++;
  int sign = 1;
  if (*s == '-') {
    sign = -1;
    s++;
  } else if (*s == '+')
    s++;
  int whole = 0;
  while (*s >= '0' && *s <= '9') {
    whole = whole * 10 + (*s - '0');
    s++;
  }
  int frac = 0;
  if (*s == '.') {
    s++;
    int mult = 100;
    while (*s >= '0' && *s <= '9' && mult >= 1) {
      frac += (*s - '0') * mult;
      mult /= 10;
      s++;
    }
  }
  return sign * (whole * 1000 + frac);
}

// ==============================================================================
// VARIABILI CSS (:root --var) E FOGLIO DI STILE
// ==============================================================================
#define MAX_CSS_RULES 256
#define MAX_CSS_VARS 64

typedef struct {
  char name[40];
  char value[80];
} css_var_t;

typedef struct {
  char selector[64];
  css_style_t style;
} css_rule_t;

typedef struct {
  css_rule_t rules[MAX_CSS_RULES];
  int num_rules;
  css_var_t vars[MAX_CSS_VARS];
  int num_vars;
} css_sheet_t;

static void set_css_var(css_sheet_t *sheet, const char *name, const char *val) {
  if (!sheet || !name || !val)
    return;
  while (*name && c_isspace(*name))
    name++;
  while (*val && c_isspace(*val))
    val++;
  for (int i = 0; i < sheet->num_vars; i++) {
    if (c_strcasecmp(sheet->vars[i].name, name) == 0) {
      c_strcpy(sheet->vars[i].value, val, sizeof(sheet->vars[i].value));
      return;
    }
  }
  if (sheet->num_vars < MAX_CSS_VARS) {
    c_strcpy(sheet->vars[sheet->num_vars].name, name,
             sizeof(sheet->vars[0].name));
    c_strcpy(sheet->vars[sheet->num_vars].value, val,
             sizeof(sheet->vars[0].value));
    sheet->num_vars++;
  }
}

static void resolve_css_vars(const css_sheet_t *sheet, const char *val,
                             char *out, size_t max_out) {
  if (!val || max_out == 0) {
    if (out && max_out)
      out[0] = '\0';
    return;
  }
  const char *vp = c_strcasestr(val, "var(");
  if (!vp) {
    c_strcpy(out, val, max_out);
    return;
  }
  size_t oi = 0;
  const char *p = val;
  while (*p && oi + 1 < max_out) {
    if (c_strncasecmp(p, "var(", 4) == 0) {
      p += 4;
      while (*p && c_isspace(*p))
        p++;
      char vname[40], vfb[64];
      size_t ni = 0, fi = 0;
      vfb[0] = '\0';
      while (*p && *p != ',' && *p != ')' && !c_isspace(*p)) {
        if (ni + 1 < sizeof(vname))
          vname[ni++] = *p;
        p++;
      }
      vname[ni] = '\0';
      while (*p && c_isspace(*p))
        p++;
      if (*p == ',') {
        p++;
        while (*p && c_isspace(*p))
          p++;
        int paren = 0;
        while (*p && (paren > 0 || *p != ')')) {
          if (*p == '(')
            paren++;
          else if (*p == ')')
            paren--;
          if (fi + 1 < sizeof(vfb))
            vfb[fi++] = *p;
          p++;
        }
        while (fi > 0 && c_isspace(vfb[fi - 1]))
          fi--;
        vfb[fi] = '\0';
      }
      if (*p == ')')
        p++;
      const char *resolved = NULL;
      if (sheet) {
        for (int i = 0; i < sheet->num_vars; i++) {
          if (c_strcasecmp(sheet->vars[i].name, vname) == 0) {
            resolved = sheet->vars[i].value;
            break;
          }
        }
      }
      const char *rep = (resolved && resolved[0]) ? resolved : vfb;
      for (size_t k = 0; rep[k] && oi + 1 < max_out; k++)
        out[oi++] = rep[k];
    } else {
      out[oi++] = *p++;
    }
  }
  out[oi] = '\0';
}

// ==============================================================================
// PARSER COLORI W3C (Hex 3/4/6/8, rgb/rgba, hsl/hsla, gradient, Named Colors)
// ==============================================================================
static int hex_val(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

static uint8_t hsl_hue_to_rgb(int p, int q, int t) {
  if (t < 0)
    t += 1000;
  if (t > 1000)
    t -= 1000;
  int v = p;
  if (t < 167)
    v = p + ((q - p) * 6 * t) / 1000;
  else if (t < 500)
    v = q;
  else if (t < 667)
    v = p + ((q - p) * (667 - t) * 6) / 1000;
  int res = (v * 255 + 500) / 1000;
  if (res < 0)
    res = 0;
  if (res > 255)
    res = 255;
  return (uint8_t)res;
}

static bool parse_css_color(const char *val, uint32_t *out,
                            bool *is_transparent) {
  if (is_transparent)
    *is_transparent = false;
  if (!val)
    return false;
  while (*val && c_isspace(*val))
    val++;

  if (c_strncasecmp(val, "transparent", 11) == 0) {
    if (is_transparent)
      *is_transparent = true;
    *out = 0;
    return false;
  }

  // Estrae il primo colore stop se presente in un gradiente CSS
  const char *grad = c_strcasestr(val, "gradient(");
  if (grad) {
    const char *gp = grad + 9;
    while (*gp && *gp != '#' && c_strncasecmp(gp, "rgb", 3) != 0 &&
           c_strncasecmp(gp, "hsl", 3) != 0) {
      if (*gp == ',') {
        gp++;
        while (*gp && c_isspace(*gp))
          gp++;
        break;
      }
      gp++;
    }
    if (*gp)
      return parse_css_color(gp, out, is_transparent);
  }

  if (*val == '#') {
    val++;
    int d[8], len = 0;
    while (len < 8 && hex_val(val[len]) >= 0) {
      d[len] = hex_val(val[len]);
      len++;
    }
    if (len >= 6) {
      if (len == 8 && ((d[6] << 4) | d[7]) == 0) {
        if (is_transparent)
          *is_transparent = true;
        return false;
      }
      *out = ((uint32_t)((d[0] << 4) | d[1]) << 16) |
             ((uint32_t)((d[2] << 4) | d[3]) << 8) |
             (uint32_t)((d[4] << 4) | d[5]);
      return true;
    }
    if (len >= 3) {
      if (len == 4 && d[3] == 0) {
        if (is_transparent)
          *is_transparent = true;
        return false;
      }
      *out = ((uint32_t)((d[0] << 4) | d[0]) << 16) |
             ((uint32_t)((d[1] << 4) | d[1]) << 8) |
             (uint32_t)((d[2] << 4) | d[2]);
      return true;
    }
    return false;
  }

  if (c_strncasecmp(val, "rgb(", 4) == 0 ||
      c_strncasecmp(val, "rgba(", 5) == 0) {
    bool has_a = (val[3] == 'a' || val[3] == 'A');
    const char *p = val + (has_a ? 5 : 4);
    int r = c_atoi(p);
    while (*p && *p != ',' && *p != ' ')
      p++;
    while (*p == ',' || *p == ' ')
      p++;
    int g = c_atoi(p);
    while (*p && *p != ',' && *p != ' ')
      p++;
    while (*p == ',' || *p == ' ')
      p++;
    int b = c_atoi(p);
    if (has_a) {
      while (*p && *p != ',' && *p != '/')
        p++;
      if (*p == ',' || *p == '/') {
        p++;
        int am = c_parse_milli(p);
        if (am <= 50) {
          if (is_transparent)
            *is_transparent = true;
          return false;
        }
      }
    }
    if (r < 0)
      r = 0;
    if (r > 255)
      r = 255;
    if (g < 0)
      g = 0;
    if (g > 255)
      g = 255;
    if (b < 0)
      b = 0;
    if (b > 255)
      b = 255;
    *out = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    return true;
  }

  if (c_strncasecmp(val, "hsl(", 4) == 0 ||
      c_strncasecmp(val, "hsla(", 5) == 0) {
    bool has_a = (val[3] == 'a' || val[3] == 'A');
    const char *p = val + (has_a ? 5 : 4);
    int h = c_atoi(p) % 360;
    if (h < 0)
      h += 360;
    while (*p && *p != ',')
      p++;
    if (*p == ',')
      p++;
    int s = c_atoi(p);
    if (s < 0)
      s = 0;
    if (s > 100)
      s = 100;
    while (*p && *p != ',')
      p++;
    if (*p == ',')
      p++;
    int l = c_atoi(p);
    if (l < 0)
      l = 0;
    if (l > 100)
      l = 100;
    if (has_a) {
      while (*p && *p != ',' && *p != '/')
        p++;
      if (*p == ',' || *p == '/') {
        p++;
        if (c_parse_milli(p) <= 50) {
          if (is_transparent)
            *is_transparent = true;
          return false;
        }
      }
    }
    int sm = s * 10, lm = l * 10;
    uint8_t r, g, b;
    if (sm == 0) {
      r = g = b = (uint8_t)((lm * 255 + 500) / 1000);
    } else {
      int q = (lm < 500) ? ((lm * (1000 + sm)) / 1000)
                         : (lm + sm - (lm * sm) / 1000);
      int pp = 2 * lm - q;
      int hm = (h * 1000) / 360;
      r = hsl_hue_to_rgb(pp, q, hm + 333);
      g = hsl_hue_to_rgb(pp, q, hm);
      b = hsl_hue_to_rgb(pp, q, hm - 333);
    }
    *out = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    return true;
  }

  static const struct {
    const char *name;
    uint32_t rgb;
  } named[] = {{"aliceblue", 0xF0F8FF},      {"antiquewhite", 0xFAEBD7},
               {"aqua", 0x00FFFF},           {"aquamarine", 0x7FFFD4},
               {"azure", 0xF0FFFF},          {"beige", 0xF5F5DC},
               {"bisque", 0xFFE4C4},         {"black", 0x000000},
               {"blue", 0x0000EE},           {"blueviolet", 0x8A2BE2},
               {"brown", 0xA52A2A},          {"burlywood", 0xDEB887},
               {"cadetblue", 0x5F9EA0},      {"chartreuse", 0x7FFF00},
               {"chocolate", 0xD2691E},      {"coral", 0xFF7F50},
               {"cornflowerblue", 0x6495ED}, {"crimson", 0xDC143C},
               {"cyan", 0x00FFFF},           {"darkblue", 0x00008B},
               {"darkcyan", 0x008B8B},       {"darkgray", 0xA9A9A9},
               {"darkgreen", 0x006400},      {"darkgrey", 0xA9A9A9},
               {"darkorange", 0xFF8C00},     {"darkred", 0x8B0000},
               {"darkviolet", 0x9400D3},     {"deeppink", 0xFF1493},
               {"deepskyblue", 0x00BFFF},    {"dimgray", 0x696969},
               {"dimgrey", 0x696969},        {"dodgerblue", 0x1E90FF},
               {"firebrick", 0xB22222},      {"forestgreen", 0x228B22},
               {"fuchsia", 0xFF00FF},        {"gainsboro", 0xDCDCDC},
               {"ghostwhite", 0xF8F8FF},     {"gold", 0xFFD700},
               {"goldenrod", 0xDAA520},      {"gray", 0x808080},
               {"green", 0x008000},          {"greenyellow", 0xADFF2F},
               {"grey", 0x808080},           {"honeydew", 0xF0FFF0},
               {"hotpink", 0xFF69B4},        {"indianred", 0xCD5C5C},
               {"indigo", 0x4B0082},         {"ivory", 0xFFFFF0},
               {"khaki", 0xF0E68C},          {"lavender", 0xE6E6FA},
               {"lightblue", 0xADD8E6},      {"lightcoral", 0xF08080},
               {"lightcyan", 0xE0FFFF},      {"lightgray", 0xD3D3D3},
               {"lightgreen", 0x90EE90},     {"lightgrey", 0xD3D3D3},
               {"lightpink", 0xFFB6C1},      {"lightsalmon", 0xFFA07A},
               {"lightskyblue", 0x87CEFA},   {"lightslategray", 0x778899},
               {"lime", 0x00FF00},           {"limegreen", 0x32CD32},
               {"linen", 0xFAF0E6},          {"magenta", 0xFF00FF},
               {"maroon", 0x800000},         {"mediumblue", 0x0000CD},
               {"midnightblue", 0x191970},   {"mintcream", 0xF5FFFA},
               {"mistyrose", 0xFFE4E1},      {"navy", 0x000080},
               {"oldlace", 0xFDF5E6},        {"olive", 0x808000},
               {"olivedrab", 0x6B8E23},      {"orange", 0xFFA500},
               {"orangered", 0xFF4500},      {"orchid", 0xDA70D6},
               {"palegreen", 0x98FB98},      {"peru", 0xCD853F},
               {"pink", 0xFFC0CB},           {"plum", 0xDDA0DD},
               {"powderblue", 0xB0E0E6},     {"purple", 0x800080},
               {"rebeccapurple", 0x663399},  {"red", 0xCC0000},
               {"rosybrown", 0xBC8F8F},      {"royalblue", 0x4169E1},
               {"saddlebrown", 0x8B4513},    {"salmon", 0xFA8072},
               {"seagreen", 0x2E8B57},       {"sienna", 0xA0522D},
               {"silver", 0xC0C0C0},         {"skyblue", 0x87CEEB},
               {"slateblue", 0x6A5ACD},      {"slategray", 0x708090},
               {"snow", 0xFFFAFA},           {"springgreen", 0x00FF7F},
               {"steelblue", 0x4682B4},      {"tan", 0xD2B48C},
               {"teal", 0x008080},           {"thistle", 0xD8BFD8},
               {"tomato", 0xFF6347},         {"turquoise", 0x40E0D0},
               {"violet", 0xEE82EE},         {"wheat", 0xF5DEB3},
               {"white", 0xFFFFFF},          {"whitesmoke", 0xF5F5F5},
               {"yellow", 0xFFD700},         {"yellowgreen", 0x9ACD32}};
  for (size_t i = 0; i < sizeof(named) / sizeof(named[0]); i++) {
    size_t nl = c_strlen(named[i].name);
    if (c_strncasecmp(val, named[i].name, nl) == 0) {
      char after = val[nl];
      if (after == '\0' || c_isspace(after) || after == ';' || after == '!' ||
          after == ',' || after == ')') {
        *out = named[i].rgb;
        return true;
      }
    }
  }
  return false;
}

// ==============================================================================
// PARSER LUNGHEZZE & SHORTHAND W3C (px, pt, em, rem, %, thin/medium/thick)
// ==============================================================================
static int parse_css_length(const char *s, bool *is_percent) {
  if (is_percent)
    *is_percent = false;
  while (*s && c_isspace(*s))
    s++;
  if (c_strncasecmp(s, "calc(", 5) == 0) {
    const char *p = s + 5;
    int lhs = parse_css_length(p, NULL);
    while (*p && *p != '+' && *p != '-' && *p != '*' && *p != '/' && *p != ')')
      p++;
    if (*p == '+' || *p == '-' || *p == '*' || *p == '/') {
      char op = *p++;
      int rhs = parse_css_length(p, NULL);
      if (op == '+') return lhs + rhs;
      if (op == '-') return lhs - rhs;
      if (op == '*') return lhs * rhs;
      if (op == '/' && rhs != 0) return lhs / rhs;
    }
    return lhs;
  }
  if (c_strncasecmp(s, "auto", 4) == 0 || c_strncasecmp(s, "none", 4) == 0)
    return 0;
  if (c_strncasecmp(s, "thin", 4) == 0)
    return 1;
  if (c_strncasecmp(s, "medium", 6) == 0)
    return 3;
  if (c_strncasecmp(s, "thick", 5) == 0)
    return 5;
  if (!c_isdigit(*s) && *s != '-' && *s != '+' && *s != '.')
    return 0;

  int milli = c_parse_milli(s);
  const char *u = s;
  if (*u == '-' || *u == '+')
    u++;
  while (*u && (c_isdigit(*u) || *u == '.'))
    u++;
  if (c_strncasecmp(u, "rem", 3) == 0 || c_strncasecmp(u, "em", 2) == 0) {
    return (milli * 16 + 500) / 1000;
  }
  if (c_strncasecmp(u, "pt", 2) == 0) {
    return (milli * 4 + 1500) / 3000;
  }
  if (*u == '%') {
    if (is_percent)
      *is_percent = true;
    return milli / 1000;
  }
  return milli / 1000;
}

static void parse_box_shorthand(const char *val, int16_t *top, int16_t *right,
                                int16_t *bottom, int16_t *left) {
  int vals[4] = {0, 0, 0, 0};
  int n = 0;
  const char *p = val;
  while (*p && n < 4) {
    while (*p && c_isspace(*p))
      p++;
    if (!*p)
      break;
    vals[n++] = parse_css_length(p, NULL);
    while (*p && !c_isspace(*p))
      p++;
  }
  if (n == 1) {
    *top = *right = *bottom = *left = (int16_t)vals[0];
  } else if (n == 2) {
    *top = *bottom = (int16_t)vals[0];
    *right = *left = (int16_t)vals[1];
  } else if (n == 3) {
    *top = (int16_t)vals[0];
    *right = *left = (int16_t)vals[1];
    *bottom = (int16_t)vals[2];
  } else if (n == 4) {
    *top = (int16_t)vals[0];
    *right = (int16_t)vals[1];
    *bottom = (int16_t)vals[2];
    *left = (int16_t)vals[3];
  }
}

static void strip_important(char *val) {
  char *imp = (char *)c_strcasestr(val, "!important");
  if (imp)
    *imp = '\0';
  size_t len = c_strlen(val);
  while (len > 0 && c_isspace(val[len - 1]))
    val[--len] = '\0';
}

static void apply_decl(const char *prop, const char *raw_val, css_style_t *st,
                       const css_sheet_t *sheet) {
  while (*prop && c_isspace(*prop))
    prop++;
  char val_buf[128];
  resolve_css_vars(sheet, raw_val, val_buf, sizeof(val_buf));
  strip_important(val_buf);
  const char *val = val_buf;
  while (*val && c_isspace(*val))
    val++;
  if (!*val)
    return;

  // 1. Color & Backgrounds (W3C Color / Backgrounds Level 3/4)
  if (c_strcasecmp(prop, "color") == 0) {
    uint32_t c = 0;
    bool tr = false;
    if (parse_css_color(val, &c, &tr)) {
      st->color = c;
      st->has_color = true;
    }
  } else if (c_strcasecmp(prop, "background-color") == 0 ||
             c_strcasecmp(prop, "background") == 0 ||
             c_strcasecmp(prop, "background-image") == 0) {
    uint32_t c = 0;
    bool tr = false;
    if (parse_css_color(val, &c, &tr)) {
      st->bg_color = c;
      st->has_bg = true;
      st->bg_transparent = false;
    } else if (tr || c_strncasecmp(val, "none", 4) == 0) {
      st->has_bg = false;
      st->bg_transparent = true;
    }
  } else if (c_strcasecmp(prop, "opacity") == 0) {
    int om = c_parse_milli(val);
    if (om < 0)
      om = 0;
    if (om > 1000)
      om = 1000;
    st->opacity = (uint8_t)((om * 255) / 1000);
    if (st->opacity == 0)
      st->display = CSS_DISPLAY_NONE;
  }
  // 2. Display, Visibility, Position, Float, Overflow (W3C Display /
  // Positioning / Overflow)
  else if (c_strcasecmp(prop, "display") == 0) {
    if (c_strncasecmp(val, "none", 4) == 0)
      st->display = CSS_DISPLAY_NONE;
    else if (c_strncasecmp(val, "inline-flex", 11) == 0)
      st->display = CSS_DISPLAY_INLINE_FLEX;
    else if (c_strncasecmp(val, "inline-grid", 11) == 0)
      st->display = CSS_DISPLAY_INLINE_GRID;
    else if (c_strncasecmp(val, "inline-block", 12) == 0)
      st->display = CSS_DISPLAY_INLINE_BLOCK;
    else if (c_strncasecmp(val, "flex", 4) == 0)
      st->display = CSS_DISPLAY_FLEX;
    else if (c_strncasecmp(val, "grid", 4) == 0)
      st->display = CSS_DISPLAY_GRID;
    else if (c_strncasecmp(val, "table", 5) == 0)
      st->display = CSS_DISPLAY_TABLE;
    else if (c_strncasecmp(val, "list-item", 9) == 0)
      st->display = CSS_DISPLAY_LIST_ITEM;
    else if (c_strncasecmp(val, "block", 5) == 0)
      st->display = CSS_DISPLAY_BLOCK;
    else if (c_strncasecmp(val, "inline", 6) == 0)
      st->display = CSS_DISPLAY_INLINE;
  } else if (c_strcasecmp(prop, "visibility") == 0) {
    if (c_strncasecmp(val, "hidden", 6) == 0 ||
        c_strncasecmp(val, "collapse", 8) == 0)
      st->display = CSS_DISPLAY_NONE;
  } else if (c_strcasecmp(prop, "position") == 0) {
    if (c_strncasecmp(val, "relative", 8) == 0)
      st->position = CSS_POS_RELATIVE;
    else if (c_strncasecmp(val, "absolute", 8) == 0)
      st->position = CSS_POS_ABSOLUTE;
    else if (c_strncasecmp(val, "fixed", 5) == 0)
      st->position = CSS_POS_FIXED;
    else if (c_strncasecmp(val, "sticky", 6) == 0)
      st->position = CSS_POS_STICKY;
    else
      st->position = CSS_POS_STATIC;
  } else if (c_strcasecmp(prop, "float") == 0) {
    if (c_strncasecmp(val, "left", 4) == 0)
      st->float_mode = CSS_FLOAT_LEFT;
    else if (c_strncasecmp(val, "right", 5) == 0)
      st->float_mode = CSS_FLOAT_RIGHT;
    else
      st->float_mode = CSS_FLOAT_NONE;
  } else if (c_strcasecmp(prop, "overflow") == 0 ||
             c_strcasecmp(prop, "overflow-x") == 0 ||
             c_strcasecmp(prop, "overflow-y") == 0) {
    if (c_strncasecmp(val, "hidden", 6) == 0)
      st->overflow = CSS_OVERFLOW_HIDDEN;
    else if (c_strncasecmp(val, "scroll", 6) == 0)
      st->overflow = CSS_OVERFLOW_SCROLL;
    else if (c_strncasecmp(val, "auto", 4) == 0)
      st->overflow = CSS_OVERFLOW_AUTO;
    else
      st->overflow = CSS_OVERFLOW_VISIBLE;
  } else if (c_strcasecmp(prop, "z-index") == 0) {
    st->z_index = (int16_t)c_atoi(val);
  }
  // 3. Borders, Outline & Shadow (W3C Backgrounds & Borders / UI)
  else if (c_strcasecmp(prop, "border") == 0 ||
           c_strcasecmp(prop, "border-top") == 0 ||
           c_strcasecmp(prop, "border-bottom") == 0 ||
           c_strcasecmp(prop, "border-left") == 0 ||
           c_strcasecmp(prop, "border-right") == 0) {
    if (c_strncasecmp(val, "none", 4) == 0 || c_strcasecmp(val, "0") == 0) {
      st->has_border = false;
      st->border_width = 0;
    } else {
      const char *p = val;
      while (*p) {
        while (*p && c_isspace(*p))
          p++;
        if (!*p)
          break;
        if (c_isdigit(*p)) {
          st->border_width = (int16_t)parse_css_length(p, NULL);
          if (st->border_width <= 0)
            st->border_width = 1;
        } else {
          uint32_t bc = 0;
          bool tr = false;
          if (parse_css_color(p, &bc, &tr)) {
            st->border_color = bc;
            st->has_border = true;
          }
        }
        while (*p && !c_isspace(*p))
          p++;
      }
      if (st->border_width > 0 && !st->has_border) {
        st->border_color = 0xCCCCCC;
        st->has_border = true;
      }
    }
  } else if (c_strcasecmp(prop, "border-width") == 0) {
    int bw = parse_css_length(val, NULL);
    st->border_width = (int16_t)bw;
    if (bw > 0)
      st->has_border = true;
  } else if (c_strcasecmp(prop, "border-color") == 0) {
    uint32_t bc = 0;
    bool tr = false;
    if (parse_css_color(val, &bc, &tr)) {
      st->border_color = bc;
      st->has_border = true;
    }
  } else if (c_strcasecmp(prop, "border-radius") == 0 ||
             c_strcasecmp(prop, "border-top-left-radius") == 0 ||
             c_strcasecmp(prop, "border-top-right-radius") == 0) {
    bool is_pct = false;
    int r = parse_css_length(val, &is_pct);
    if (is_pct && r >= 45)
      st->border_radius = 50;
    else if (r >= 0 && r <= 64)
      st->border_radius = (int16_t)r;
  } else if (c_strcasecmp(prop, "outline") == 0 ||
             c_strcasecmp(prop, "outline-color") == 0) {
    if (c_strncasecmp(val, "none", 4) == 0 || c_strcasecmp(val, "0") == 0) {
      st->has_outline = false;
    } else {
      uint32_t oc = 0;
      bool tr = false;
      if (parse_css_color(val, &oc, &tr)) {
        st->outline_color = oc;
        st->has_outline = true;
      }
    }
  } else if (c_strcasecmp(prop, "box-shadow") == 0) {
    st->has_shadow = (c_strncasecmp(val, "none", 4) != 0);
  }
  // 4. Fonts & Typography (W3C CSS Fonts / Text / Text Decoration)
  else if (c_strcasecmp(prop, "font-weight") == 0) {
    if (c_strncasecmp(val, "bold", 4) == 0 ||
        c_strncasecmp(val, "bolder", 6) == 0 || c_atoi(val) >= 600) {
      st->bold = true;
    } else if (c_strncasecmp(val, "normal", 6) == 0 ||
               (c_isdigit(val[0]) && c_atoi(val) < 600)) {
      st->bold = false;
    }
  } else if (c_strcasecmp(prop, "font-style") == 0) {
    st->italic = (c_strncasecmp(val, "italic", 6) == 0 ||
                  c_strncasecmp(val, "oblique", 7) == 0);
  } else if (c_strcasecmp(prop, "font-size") == 0) {
    if (c_strncasecmp(val, "xx-large", 8) == 0)
      st->font_size = 32;
    else if (c_strncasecmp(val, "x-large", 7) == 0)
      st->font_size = 24;
    else if (c_strncasecmp(val, "large", 5) == 0)
      st->font_size = 18;
    else if (c_strncasecmp(val, "medium", 6) == 0)
      st->font_size = 15;
    else if (c_strncasecmp(val, "small", 5) == 0)
      st->font_size = 13;
    else if (c_strncasecmp(val, "x-small", 7) == 0)
      st->font_size = 11;
    else {
      int px = parse_css_length(val, NULL);
      if (px > 0 && px <= 72)
        st->font_size = (int16_t)px;
    }
  } else if (c_strcasecmp(prop, "line-height") == 0) {
    int px = parse_css_length(val, NULL);
    if (px > 0 && px <= 96)
      st->line_height = (int16_t)px;
  } else if (c_strcasecmp(prop, "font") == 0) {
    if (c_strcasestr(val, "bold"))
      st->bold = true;
    if (c_strcasestr(val, "italic"))
      st->italic = true;
    const char *p = val;
    while (*p) {
      if (c_isdigit(*p)) {
        int px = parse_css_length(p, NULL);
        if (px >= 8 && px <= 72) {
          st->font_size = (int16_t)px;
          break;
        }
      }
      p++;
    }
  } else if (c_strcasecmp(prop, "text-align") == 0) {
    if (c_strncasecmp(val, "center", 6) == 0)
      st->text_align = CSS_ALIGN_CENTER;
    else if (c_strncasecmp(val, "right", 5) == 0 ||
             c_strncasecmp(val, "end", 3) == 0)
      st->text_align = CSS_ALIGN_RIGHT;
    else if (c_strncasecmp(val, "justify", 7) == 0)
      st->text_align = CSS_ALIGN_JUSTIFY;
    else
      st->text_align = CSS_ALIGN_LEFT;
  } else if (c_strcasecmp(prop, "text-decoration") == 0 ||
             c_strcasecmp(prop, "text-decoration-line") == 0) {
    if (c_strcasestr(val, "none")) {
      st->text_deco = CSS_TEXT_DECO_NONE;
      st->no_underline = true;
    } else if (c_strcasestr(val, "underline")) {
      st->text_deco = CSS_TEXT_DECO_UNDERLINE;
      st->no_underline = false;
    } else if (c_strcasestr(val, "line-through")) {
      st->text_deco = CSS_TEXT_DECO_LINE_THRU;
    } else if (c_strcasestr(val, "overline")) {
      st->text_deco = CSS_TEXT_DECO_OVERLINE;
    }
  } else if (c_strcasecmp(prop, "text-transform") == 0) {
    if (c_strncasecmp(val, "uppercase", 9) == 0)
      st->text_transform = CSS_TEXT_TRANSFORM_UPPERCASE;
    else if (c_strncasecmp(val, "lowercase", 9) == 0)
      st->text_transform = CSS_TEXT_TRANSFORM_LOWERCASE;
    else if (c_strncasecmp(val, "capitalize", 10) == 0)
      st->text_transform = CSS_TEXT_TRANSFORM_CAPITALIZE;
    else
      st->text_transform = CSS_TEXT_TRANSFORM_NONE;
  } else if (c_strcasecmp(prop, "white-space") == 0) {
    st->nowrap = (c_strncasecmp(val, "nowrap", 6) == 0);
  }
  // 5. Box Model: Margin, Padding, Sizing, Gap (W3C Box Model / Flexbox / Grid)
  else if (c_strcasecmp(prop, "margin") == 0) {
    parse_box_shorthand(val, &st->margin_top, &st->margin_right,
                        &st->margin_bottom, &st->margin_left);
  } else if (c_strcasecmp(prop, "margin-top") == 0) {
    st->margin_top = (int16_t)parse_css_length(val, NULL);
  } else if (c_strcasecmp(prop, "margin-right") == 0) {
    st->margin_right = (int16_t)parse_css_length(val, NULL);
  } else if (c_strcasecmp(prop, "margin-bottom") == 0) {
    st->margin_bottom = (int16_t)parse_css_length(val, NULL);
  } else if (c_strcasecmp(prop, "margin-left") == 0) {
    st->margin_left = (int16_t)parse_css_length(val, NULL);
  } else if (c_strcasecmp(prop, "padding") == 0) {
    parse_box_shorthand(val, &st->padding_top, &st->padding_right,
                        &st->padding_bottom, &st->padding_left);
  } else if (c_strcasecmp(prop, "padding-top") == 0) {
    st->padding_top = (int16_t)parse_css_length(val, NULL);
  } else if (c_strcasecmp(prop, "padding-right") == 0) {
    st->padding_right = (int16_t)parse_css_length(val, NULL);
  } else if (c_strcasecmp(prop, "padding-bottom") == 0) {
    st->padding_bottom = (int16_t)parse_css_length(val, NULL);
  } else if (c_strcasecmp(prop, "padding-left") == 0) {
    st->padding_left = (int16_t)parse_css_length(val, NULL);
  } else if (c_strcasecmp(prop, "width") == 0) {
    bool pct = false;
    int w = parse_css_length(val, &pct);
    if (!pct && w > 0)
      st->width = (int16_t)w;
  } else if (c_strcasecmp(prop, "height") == 0) {
    bool pct = false;
    int h = parse_css_length(val, &pct);
    if (!pct && h > 0)
      st->height = (int16_t)h;
  } else if (c_strcasecmp(prop, "min-width") == 0) {
    bool pct = false;
    int w = parse_css_length(val, &pct);
    if (!pct && w > 0)
      st->min_width = (int16_t)w;
  } else if (c_strcasecmp(prop, "max-width") == 0) {
    bool pct = false;
    int w = parse_css_length(val, &pct);
    if (!pct && w > 0)
      st->max_width = (int16_t)w;
  } else if (c_strcasecmp(prop, "gap") == 0 ||
             c_strcasecmp(prop, "grid-gap") == 0 ||
             c_strcasecmp(prop, "column-gap") == 0 ||
             c_strcasecmp(prop, "row-gap") == 0) {
    int g = parse_css_length(val, NULL);
    if (g >= 0 && g <= 64)
      st->gap = (int16_t)g;
  } else if (c_strcasecmp(prop, "box-sizing") == 0) {
    st->box_sizing_border = (c_strncasecmp(val, "border-box", 10) == 0);
  }
  // 6. Flexbox / Grid & Extra Box/Text/UI (W3C Flexbox, Grid, Text, UI)
  else if (c_strcasecmp(prop, "flex-direction") == 0) {
    st->flex_direction = (c_strncasecmp(val, "column", 6) == 0) ? CSS_FLEX_DIR_COL : CSS_FLEX_DIR_ROW;
  } else if (c_strcasecmp(prop, "flex-wrap") == 0) {
    st->flex_wrap = (c_strncasecmp(val, "wrap", 4) == 0);
  } else if (c_strcasecmp(prop, "justify-content") == 0) {
    if (c_strncasecmp(val, "center", 6) == 0) st->justify_content = CSS_JUSTIFY_CENTER;
    else if (c_strncasecmp(val, "flex-end", 8) == 0 || c_strncasecmp(val, "end", 3) == 0) st->justify_content = CSS_JUSTIFY_END;
    else if (c_strncasecmp(val, "space-between", 13) == 0) st->justify_content = CSS_JUSTIFY_BETWEEN;
    else st->justify_content = CSS_JUSTIFY_START;
  } else if (c_strcasecmp(prop, "grid-template-columns") == 0) {
    const char *rp = c_strcasestr(val, "repeat(");
    if (rp) {
      int gc = c_atoi(rp + 7);
      if (gc >= 1 && gc <= 6) st->grid_cols = (uint8_t)gc;
    }
  } else if (c_strcasecmp(prop, "border-style") == 0) {
    if (c_strncasecmp(val, "none", 4) == 0 || c_strncasecmp(val, "hidden", 6) == 0) {
      st->has_border = false;
      st->border_width = 0;
    }
  } else if (c_strcasecmp(prop, "border-bottom-left-radius") == 0 ||
             c_strcasecmp(prop, "border-bottom-right-radius") == 0) {
    bool is_pct = false;
    int r = parse_css_length(val, &is_pct);
    if (is_pct && r >= 45) st->border_radius = 50;
    else if (r > 0 && r <= 64 && st->border_radius == 0) st->border_radius = (int16_t)r;
  } else if (c_strcasecmp(prop, "letter-spacing") == 0) {
    int ls = parse_css_length(val, NULL);
    if (ls >= -4 && ls <= 16) st->letter_spacing = (int16_t)ls;
  } else if (c_strcasecmp(prop, "text-indent") == 0) {
    int ti = parse_css_length(val, NULL);
    if (ti >= 0 && ti <= 64) st->text_indent = (int16_t)ti;
  } else if (c_strcasecmp(prop, "list-style") == 0 ||
           c_strcasecmp(prop, "list-style-type") == 0) {
    if (c_strncasecmp(val, "none", 4) == 0)
      st->list_style_none = true;
  } else if (c_strcasecmp(prop, "cursor") == 0) {
    st->cursor_pointer = (c_strncasecmp(val, "pointer", 7) == 0);
  }
}

static void parse_decl_block(const char *blk, size_t len, css_style_t *st,
                             css_sheet_t *sheet, bool collect_vars) {
  size_t i = 0;
  while (i < len) {
    while (i < len && (c_isspace(blk[i]) || blk[i] == ';'))
      i++;
    if (i >= len)
      break;

    // Salta commenti /* ... */
    if (i + 1 < len && blk[i] == '/' && blk[i + 1] == '*') {
      i += 2;
      while (i + 1 < len && !(blk[i] == '*' && blk[i + 1] == '/'))
        i++;
      i += 2;
      continue;
    }

    char prop[48], val[128];
    size_t pi = 0, vi = 0;
    while (i < len && blk[i] != ':' && blk[i] != ';' && blk[i] != '}') {
      if (!c_isspace(blk[i]) && pi + 1 < sizeof(prop))
        prop[pi++] = blk[i];
      i++;
    }
    prop[pi] = '\0';
    if (i < len && blk[i] == ':') {
      i++;
      char in_q = 0;
      while (i < len && (in_q || (blk[i] != ';' && blk[i] != '}'))) {
        if (blk[i] == '"' || blk[i] == '\'') {
          if (in_q == blk[i])
            in_q = 0;
          else if (!in_q)
            in_q = blk[i];
        }
        if (vi + 1 < sizeof(val))
          val[vi++] = blk[i];
        i++;
      }
      val[vi] = '\0';
      if (pi > 0 && vi > 0) {
        if (prop[0] == '-' && prop[1] == '-') {
          if (collect_vars && sheet)
            set_css_var(sheet, prop, val);
        } else {
          apply_decl(prop, val, st, sheet);
        }
      }
    }
    if (i < len && blk[i] == ';')
      i++;
  }
}

static void parse_css_chunk(css_sheet_t *sheet, const char *css, size_t len) {
  size_t i = 0;
  while (i < len && sheet->num_rules < MAX_CSS_RULES) {
    while (i < len && c_isspace(css[i]))
      i++;
    if (i >= len)
      break;

    if (i + 1 < len && css[i] == '/' && css[i + 1] == '*') {
      i += 2;
      while (i + 1 < len && !(css[i] == '*' && css[i + 1] == '/'))
        i++;
      i += 2;
      continue;
    }

    // Gestione @media (screen / min-width / all) vs altri @-rules (@keyframes,
    // @import)
    if (css[i] == '@') {
      bool is_media = (c_strncasecmp(css + i, "@media", 6) == 0);
      size_t hdr_start = i;
      while (i < len && css[i] != '{' && css[i] != ';')
        i++;
      if (i >= len)
        break;
      if (css[i] == ';') {
        i++;
        continue;
      }
      size_t hdr_end = i;
      i++; // '{'
      size_t body_start = i;
      int depth = 1;
      while (i < len && depth > 0) {
        if (css[i] == '{')
          depth++;
        else if (css[i] == '}')
          depth--;
        i++;
      }
      size_t body_end = (i > body_start && css[i - 1] == '}') ? (i - 1) : i;
      if (is_media) {
        char mhdr[96];
        size_t hl = hdr_end - hdr_start;
        if (hl >= sizeof(mhdr))
          hl = sizeof(mhdr) - 1;
        for (size_t k = 0; k < hl; k++)
          mhdr[k] = css[hdr_start + k];
        mhdr[hl] = '\0';
        // Ignora @media print, dark-mode o mobile stretto (max-width: 4xx)
        if (!c_strcasestr(mhdr, "print") &&
            !c_strcasestr(mhdr, "prefers-color-scheme: dark") &&
            !c_strcasestr(mhdr, "prefers-color-scheme:dark") &&
            !c_strcasestr(mhdr, "max-width: 4")) {
          parse_css_chunk(sheet, css + body_start, body_end - body_start);
        }
      }
      continue;
    }

    size_t sel_start = i;
    while (i < len && css[i] != '{')
      i++;
    if (i >= len)
      break;
    size_t sel_end = i;
    i++; // '{'

    size_t blk_start = i;
    int depth = 1;
    while (i < len && depth > 0) {
      if (css[i] == '{')
        depth++;
      else if (css[i] == '}')
        depth--;
      i++;
    }
    size_t blk_end = (i > blk_start && css[i - 1] == '}') ? (i - 1) : i;

    css_style_t parsed;
    c_memset(&parsed, 0, sizeof(parsed));
    parsed.display = CSS_DISPLAY_INLINE;
    parsed.opacity = 255;
    parse_decl_block(css + blk_start, blk_end - blk_start, &parsed, sheet,
                     true);

    // Supporta selettori separati da virgola: "h1, .title, #main"
    size_t s = sel_start;
    while (s < sel_end && sheet->num_rules < MAX_CSS_RULES) {
      while (s < sel_end && (c_isspace(css[s]) || css[s] == ','))
        s++;
      if (s >= sel_end)
        break;
      size_t e = s;
      while (e < sel_end && css[e] != ',')
        e++;
      size_t te = e;
      while (te > s && c_isspace(css[te - 1]))
        te--;

      if (te > s) {
        css_rule_t *r = &sheet->rules[sheet->num_rules++];
        size_t clen = te - s;
        if (clen >= sizeof(r->selector))
          clen = sizeof(r->selector) - 1;
        for (size_t k = 0; k < clen; k++)
          r->selector[k] = css[s + k];
        r->selector[clen] = '\0';
        r->style = parsed;
      }
      s = e + 1;
    }
  }
}

static bool token_contains(const char *list, const char *needle) {
  if (!list || !needle || !*needle)
    return false;
  size_t nl = c_strlen(needle);
  const char *p = list;
  while (*p) {
    while (*p && c_isspace(*p))
      p++;
    if (!*p)
      break;
    const char *start = p;
    while (*p && !c_isspace(*p))
      p++;
    if ((size_t)(p - start) == nl && c_strncasecmp(start, needle, nl) == 0)
      return true;
  }
  return false;
}

static bool selector_matches(const char *sel, const char *tag,
                             const char *id_attr, const char *class_attr,
                             int *out_spec) {
  if (!sel || !*sel)
    return false;
  // Salta pseudo-classi interattive (:hover, :active, :focus) nel rendering
  // statico
  if (c_strcasestr(sel, ":hover") || c_strcasestr(sel, ":active") ||
      c_strcasestr(sel, ":focus")) {
    return false;
  }
  if (c_strcasecmp(sel, "*") == 0 || c_strcasecmp(sel, ":root") == 0) {
    *out_spec = 0;
    return true;
  }
  // Estrae l'ultimo selettore semplice in caso di combinatori (es. ".card a" o "ul > li")
  const char *last_part = sel;
  for (const char *sp = sel; *sp; sp++) {
    if (*sp == ' ' || *sp == '>' || *sp == '+' || *sp == '~') {
      if (sp[1] && sp[1] != ' ' && sp[1] != '>') last_part = sp + 1;
    }
  }
  // Rimuove eventuale selettore attributo [attr=...] dal segmento finale
  char clean[64];
  c_strcpy(clean, last_part, sizeof(clean));
  for (int k = 0; clean[k]; k++) {
    if (clean[k] == ':' || clean[k] == '[') {
      clean[k] = '\0';
      break;
    }
  }

  if (clean[0] == '.') {
    if (class_attr && token_contains(class_attr, clean + 1)) {
      *out_spec = 10;
      return true;
    }
    return false;
  }
  if (clean[0] == '#') {
    if (id_attr && c_strcasecmp(id_attr, clean + 1) == 0) {
      *out_spec = 100;
      return true;
    }
    return false;
  }
  // tag.class o tag#id
  const char *dot = NULL, *hash = NULL;
  for (int k = 0; clean[k]; k++) {
    if (clean[k] == '.') {
      dot = &clean[k];
      break;
    }
    if (clean[k] == '#') {
      hash = &clean[k];
      break;
    }
  }
  if (dot && tag && tag[0]) {
    size_t tlen = (size_t)(dot - clean);
    if (c_strlen(tag) == tlen && c_strncasecmp(clean, tag, tlen) == 0 &&
        class_attr && token_contains(class_attr, dot + 1)) {
      *out_spec = 11;
      return true;
    }
    return false;
  }
  if (hash && tag && tag[0]) {
    size_t tlen = (size_t)(hash - clean);
    if (c_strlen(tag) == tlen && c_strncasecmp(clean, tag, tlen) == 0 &&
        id_attr && c_strcasecmp(id_attr, hash + 1) == 0) {
      *out_spec = 101;
      return true;
    }
    return false;
  }
  if (tag && tag[0] && c_strcasecmp(clean, tag) == 0) {
    *out_spec = 1;
    return true;
  }
  return false;
}

static void merge_style(css_style_t *dst, const css_style_t *src) {
  if (src->has_color) {
    dst->color = src->color;
    dst->has_color = true;
  }
  if (src->has_bg) {
    dst->bg_color = src->bg_color;
    dst->has_bg = true;
    dst->bg_transparent = false;
  } else if (src->bg_transparent) {
    dst->has_bg = false;
    dst->bg_transparent = true;
  }
  if (src->has_border) {
    dst->border_color = src->border_color;
    dst->has_border = true;
  }
  if (src->border_width > 0)
    dst->border_width = src->border_width;
  if (src->border_radius > 0)
    dst->border_radius = src->border_radius;
  if (src->has_outline) {
    dst->outline_color = src->outline_color;
    dst->has_outline = true;
  }
  if (src->has_shadow)
    dst->has_shadow = true;
  if (src->display != CSS_DISPLAY_INLINE)
    dst->display = src->display;
  if (src->text_align != CSS_ALIGN_LEFT)
    dst->text_align = src->text_align;
  if (src->position != CSS_POS_STATIC)
    dst->position = src->position;
  if (src->float_mode != CSS_FLOAT_NONE)
    dst->float_mode = src->float_mode;
  if (src->overflow != CSS_OVERFLOW_VISIBLE)
    dst->overflow = src->overflow;
  if (src->text_deco != CSS_TEXT_DECO_DEFAULT) {
    dst->text_deco = src->text_deco;
    dst->no_underline = (src->text_deco == CSS_TEXT_DECO_NONE);
  }
  if (src->text_transform != CSS_TEXT_TRANSFORM_NONE)
    dst->text_transform = src->text_transform;
  if (src->flex_direction != CSS_FLEX_DIR_ROW)
    dst->flex_direction = src->flex_direction;
  if (src->justify_content != CSS_JUSTIFY_START)
    dst->justify_content = src->justify_content;
  if (src->grid_cols > 0)
    dst->grid_cols = src->grid_cols;
  if (src->flex_wrap)
    dst->flex_wrap = true;
  if (src->letter_spacing != 0)
    dst->letter_spacing = src->letter_spacing;
  if (src->text_indent != 0)
    dst->text_indent = src->text_indent;
  if (src->opacity < 255)
    dst->opacity = src->opacity;
  if (src->bold)
    dst->bold = true;
  if (src->italic)
    dst->italic = true;
  if (src->nowrap)
    dst->nowrap = true;
  if (src->box_sizing_border)
    dst->box_sizing_border = true;
  if (src->list_style_none)
    dst->list_style_none = true;
  if (src->cursor_pointer)
    dst->cursor_pointer = true;
  if (src->font_size > 0)
    dst->font_size = src->font_size;
  if (src->line_height > 0)
    dst->line_height = src->line_height;
  if (src->width > 0)
    dst->width = src->width;
  if (src->height > 0)
    dst->height = src->height;
  if (src->min_width > 0)
    dst->min_width = src->min_width;
  if (src->max_width > 0)
    dst->max_width = src->max_width;
  if (src->margin_top != 0)
    dst->margin_top = src->margin_top;
  if (src->margin_right != 0)
    dst->margin_right = src->margin_right;
  if (src->margin_bottom != 0)
    dst->margin_bottom = src->margin_bottom;
  if (src->margin_left != 0)
    dst->margin_left = src->margin_left;
  if (src->padding_top != 0)
    dst->padding_top = src->padding_top;
  if (src->padding_right != 0)
    dst->padding_right = src->padding_right;
  if (src->padding_bottom != 0)
    dst->padding_bottom = src->padding_bottom;
  if (src->padding_left != 0)
    dst->padding_left = src->padding_left;
  if (src->gap > 0)
    dst->gap = src->gap;
  if (src->z_index != 0)
    dst->z_index = src->z_index;
}

static int css_dispatch(const kernel_api_t *api, css_params_t *p) {
  if (!api || !p)
    return -1;
  if (p->cmd == CSS_CMD_INIT)
    return 0;

  if (p->cmd == CSS_CMD_PARSE_HTML && p->html_or_css) {
    css_sheet_t *sheet = (css_sheet_t *)api->kmalloc(sizeof(css_sheet_t));
    if (!sheet)
      return -1;
    c_memset(sheet, 0, sizeof(css_sheet_t));

    const char *cur = p->html_or_css;
    while (*cur && sheet->num_rules < MAX_CSS_RULES) {
      while (*cur && !(*cur == '<' && (cur[1] == 's' || cur[1] == 'S') &&
                       c_strncasecmp(cur, "<style", 6) == 0)) {
        cur++;
      }
      if (!*cur)
        break;
      const char *st = cur;
      while (*st && *st != '>')
        st++;
      if (*st == '>')
        st++;
      const char *en = st;
      while (*en && !(*en == '<' && en[1] == '/' &&
                      c_strncasecmp(en, "</style>", 8) == 0)) {
        en++;
      }
      if (!*en)
        break;
      parse_css_chunk(sheet, st, (size_t)(en - st));
      cur = en + 8;
    }
    p->sheet_handle = sheet;
    return 0;
  }

  if (p->cmd == CSS_CMD_RESOLVE_TAG) {
    c_memset(&p->out_style, 0, sizeof(p->out_style));
    p->out_style.display = CSS_DISPLAY_INLINE;
    p->out_style.opacity = 255;

    css_sheet_t *sheet = (css_sheet_t *)p->sheet_handle;
    if (sheet) {
      static const int spec_order[] = {0, 1, 10, 11, 100, 101};
      for (size_t s = 0; s < sizeof(spec_order) / sizeof(spec_order[0]); s++) {
        int target_spec = spec_order[s];
        for (int i = 0; i < sheet->num_rules; i++) {
          int sp = -1;
          if (selector_matches(sheet->rules[i].selector, p->tag_name,
                               p->id_attr, p->class_attr, &sp) &&
              sp == target_spec) {
            merge_style(&p->out_style, &sheet->rules[i].style);
          }
        }
      }
    }

    // Inline style="..." (specificita' massima, risolve var(--...) dal foglio)
    if (p->inline_style && p->inline_style[0]) {
      parse_decl_block(p->inline_style, c_strlen(p->inline_style),
                       &p->out_style, sheet, false);
    }
    return 0;
  }

  return -1;
}
