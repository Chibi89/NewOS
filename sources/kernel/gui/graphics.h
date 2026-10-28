#ifndef GUI_GRAPHICS_H
#define GUI_GRAPHICS_H

#include <types.h>
#include <gui/image.h>

// ─── Palette colori autentica Windows 2000 / XP Classic ──────────────────
#define COLOR_DESKTOP              0x008080  // Teal classico Windows 2000
#define COLOR_BTNFACE              0xD4D0C8  // Grigio 3D pulsanti e finestre
#define COLOR_BTNHIGHLIGHT         0xFFFFFF  // Bianco rilievo luce
#define COLOR_BTNSHADOW            0x808080  // Grigio medio ombra
#define COLOR_BTNDKSHADOW          0x404040  // Grigio scuro ombra profonda
#define COLOR_ACTIVECAPTION_LEFT   0x0A246A  // Blu profondo barra del titolo
#define COLOR_ACTIVECAPTION_RIGHT  0xA6CAF0  // Gradiente ciano chiaro
#define COLOR_INACTIVECAPTION_LEFT 0x808080  // Barra inattiva grigio
#define COLOR_INACTIVECAPTION_RIGHT 0xC0C0C0 // Gradiente inattivo grigio chiaro
#define COLOR_CAPTIONTEXT          0xFFFFFF  // Testo bianco barra titolo
#define COLOR_INACTIVECAPTIONTEXT  0xD4D0C8  // Testo barra inattiva
#define COLOR_WINDOW               0xFFFFFF  // Sfondo client area finestre
#define COLOR_WINDOWTEXT           0x000000  // Testo nero standard
#define COLOR_MENU_SIDEBAR         0x0A246A  // Barra laterale blu Start Menu

// ─── Identificatori sfondo ────────────────────────────────────────────────
#define BG_TEAL_2000               0  // Teal Classico Windows 2000
#define BG_BLUE_NAVY               1  // Blu Notte Windows NT
#define BG_BLISS_XP                2  // Gradiente Bliss XP
#define BG_SLATE_GRAY              3  // Grigio Ardesia Industriale
#define BG_BRICKS                  4  // Pattern Retrò Mattoni
#define BG_MATRIX                  5  // Pattern Circuito Matrix
#define BG_PHOTO_HERO              6  // Windows Hero (img0.jpg da resources/wallpaper)
#define BG_PHOTO_FLOW              7  // Theme Flow (img20.jpg da resources/wallpaper)
#define BG_CUSTOM                  8  // Sfondo Personalizzato (Caricato da File BMP o JPG)
#define NUM_BACKGROUNDS            9

// ─── Sistema di temi visivi ───────────────────────────────────────────────
#define THEME_CLASSIC              0  // Windows 2000 Classic
#define THEME_LIQUID_GLASS         1  // Apple Liquid Glass
#define THEME_MICA_LIGHT           2  // Windows 11 Mica Light
#define THEME_MICA_DARK            3  // Windows 11 Mica Dark

// ─── Palette Liquid Glass ────────────────────────────────────────────────
// Vetro bianco neutro (corpo finestra)
#define LG_GLASS_TINT              0xFFFFFF
#define LG_GLASS_ALPHA             52        // ~20% opacità
// Vetro blu (finestra attiva)
#define LG_GLASS_BLUE              0xC8DEFF
#define LG_GLASS_BLUE_ALPHA        60
// Speculare superiore
#define LG_SPECULAR_COLOR          0xFFFFFF
#define LG_SPECULAR_ALPHA          160
// Bordo sottile
#define LG_BORDER_COLOR            0xFFFFFF
#define LG_BORDER_ALPHA            55
// Ombra
#define LG_SHADOW_COLOR            0x000000
#define LG_SHADOW_ALPHA            70
// Taskbar scura
#define LG_TASKBAR_COLOR           0x08080F
#define LG_TASKBAR_ALPHA           210
// Testo su vetro
#define LG_TEXT_PRIMARY            0xFFFFFF
#define LG_TEXT_SECONDARY          0xCCE0FF
// Hover glass (selezione menu)
#define LG_HOVER_COLOR             0xFFFFFF
#define LG_HOVER_ALPHA             90
// Raggio angoli arrotondati
#define LG_CORNER_RADIUS           12

// ─── Primitives classiche ────────────────────────────────────────────────
void gfx_init(BootInfo* binfo);
void gfx_flip(void);
void gfx_clear(uint32_t color);

void gfx_draw_background(int bg_type);
void gfx_draw_pixel(int32_t x, int32_t y, uint32_t color);
uint32_t gfx_get_pixel(int32_t x, int32_t y);
void gfx_fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
void gfx_draw_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
void gfx_draw_bevel_rect(int32_t x, int32_t y, int32_t w, int32_t h, bool sunken);
void gfx_draw_gradient_h(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c_start, uint32_t c_end);
void gfx_draw_gradient_v(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c_start, uint32_t c_end);

void gfx_draw_char(int32_t x, int32_t y, char c, uint32_t fg, uint32_t bg, bool transparent);
void gfx_draw_string(int32_t x, int32_t y, const char* str, uint32_t fg, uint32_t bg, bool transparent);

void gfx_draw_cursor(int32_t x, int32_t y);
void gfx_draw_resize_cursor(int32_t x, int32_t y, int resize_edge);
void gfx_draw_windows_logo(int32_t x, int32_t y);
void gfx_draw_icon_computer(int32_t x, int32_t y);
void gfx_draw_icon_notepad(int32_t x, int32_t y);
void gfx_draw_icon_calc(int32_t x, int32_t y);
void gfx_draw_icon_cmd(int32_t x, int32_t y);
void gfx_draw_icon_folder(int32_t x, int32_t y);
void gfx_draw_icon_doc(int32_t x, int32_t y);
void gfx_draw_mini_monitor(int32_t x, int32_t y, int bg_type);

uint32_t gfx_get_width(void);
uint32_t gfx_get_height(void);
uint32_t gfx_get_pitch_bytes(void);
bool     gfx_set_resolution(uint32_t width, uint32_t height);
void     gfx_draw_image(int32_t x, int32_t y, const image_t* img);

// ─── Primitives Liquid Glass ─────────────────────────────────────────────

// Alpha blend di un colore solido sopra i pixel del back-buffer
void gfx_fill_alpha(int32_t x, int32_t y, int32_t w, int32_t h,
                    uint32_t color, uint8_t alpha);

// Rettangolo con angoli arrotondati (solid fill)
void gfx_fill_rounded_rect(int32_t x, int32_t y, int32_t w, int32_t h,
                            int32_t radius, uint32_t color);

// Bordo perimetrale con angoli arrotondati che segue la curvatura
void gfx_draw_rounded_rect(int32_t x, int32_t y, int32_t w, int32_t h,
                            int32_t radius, uint32_t color);

// Pannello vetro: campiona blurred_buf + alpha-blend tint + rounded corners
void gfx_fill_rounded_alpha(int32_t x, int32_t y, int32_t w, int32_t h,
                             int32_t radius, uint32_t tint, uint8_t alpha);

// Rim speculare brillante nella fascia superiore del pannello glass
void gfx_draw_specular_rim(int32_t x, int32_t y, int32_t w, int32_t h,
                            int32_t radius);

// Ombra morbida multi-layer sotto un pannello glass
void gfx_draw_drop_shadow(int32_t x, int32_t y, int32_t w, int32_t h,
                           int32_t spread);

// Pre-calcola il buffer sfocato dal wallpaper corrente (chiamare dopo cambio sfondo)
void gfx_precompute_blur(void);

// Recupera pixel dal buffer sfocato (usato da gfx_fill_rounded_alpha)
uint32_t gfx_get_blurred_pixel(int32_t x, int32_t y);

// Imposta un wallpaper personalizzato a partire da pixel decodificati
void gfx_set_custom_wallpaper(const uint32_t* pixels, int32_t width, int32_t height);

#endif // GUI_GRAPHICS_H
