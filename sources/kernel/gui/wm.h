#ifndef GUI_WM_H
#define GUI_WM_H

#include <types.h>
#include <drivers/mouse.h>

#define MAX_WINDOWS 8

typedef enum {
    APP_CMD = 0,
    APP_NOTEPAD,
    APP_CALC,
    APP_ABOUT,
    APP_DISPLAY,
    APP_SOUND,
    APP_EXPLORER,
    APP_POWER,
    APP_SHUTDOWN,
    APP_INSTALLER,
    APP_DEVMGMT,
    APP_REGEDIT,
    APP_OOBE,
    APP_RULES,
    APP_AUTOPLAY,
    APP_BROWSER
} app_type_t;

typedef struct window window_t;

typedef void (*win_draw_func_t)(window_t* win);
typedef void (*win_mouse_func_t)(window_t* win, int32_t local_x, int32_t local_y, bool clicked);
typedef void (*win_key_func_t)(window_t* win, char c);

struct window {
    int32_t id;
    char title[64];
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    bool is_active;
    bool is_minimized;
    bool is_dragging;
    int32_t drag_off_x;
    int32_t drag_off_y;
    app_type_t app_type;
    void* app_data;

    win_draw_func_t  on_draw;
    win_mouse_func_t on_mouse;
    win_mouse_func_t on_right_click;
    win_key_func_t   on_key;
};

void wm_init(void);
window_t* wm_create_window(const char* title, int32_t x, int32_t y, int32_t w, int32_t h, app_type_t type);
void wm_destroy_window(int32_t id);
void wm_focus_window(int32_t id);
void wm_toggle_minimize(int32_t id);

void wm_set_background_type(int bg_type);
int  wm_get_background_type(void);
void wm_set_theme(int theme);
int  wm_get_theme(void);
void wm_add_desktop_file(const char* name, bool is_folder);

void wm_update(mouse_state_t* mouse, char key);
void wm_render(void);

// Funzioni per avviare le app integrate
void app_cmd_launch(void);
void app_notepad_launch(void);
void app_notepad_open_file(const char* title, const char* content);
void app_calc_launch(void);
void app_about_launch(void);
void app_display_props_launch(void);
void app_sound_props_launch(void);
void app_explorer_open_this_pc(void);
void app_explorer_open_drive_c(void);
void app_explorer_open_folder(const char* folder_name);
void app_explorer_open_recycle_bin(void);
void app_control_panel_open(void);
void app_power_props_launch(void);
void app_shutdown_dialog_launch(void);
void app_installer_launch(void);
void app_regedit_launch(void);
void app_rules_viewer_launch(void);
void app_rules_viewer_open_file(const char* filepath);
void app_browser_launch(void);
void app_browser_open_url(const char* url);

void wm_on_resolution_changed(uint32_t old_w, uint32_t old_h, uint32_t new_w, uint32_t new_h);

#endif // GUI_WM_H

