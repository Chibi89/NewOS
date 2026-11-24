#include "loginscreen.h"

// Header di protezione del modulo (64 byte esatti all'inizio del file)
__attribute__((section(".header"), used))
module_header_t g_loginscreen_header = {
    .magic          = MODULE_MAGIC_NEWOS,
    .header_size    = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type    = MODULE_TYPE_LOGINSCREEN,
    .code_size      = 0,
    .entry_offset   = 64,
    .checksum       = 0,
    .security_flags = SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name    = "LOGINSCREEN",
    .signature      = MODULE_AUTH_TAG
};

#define MAX_LOGIN_USERS 6

typedef struct {
    char username[32];
    char password[32];
    char role[28];
    bool is_guest;
    uint32_t avatar_col;
} login_user_entry_t;

typedef struct {
    int selected_idx;
    char password[32];
    int pwd_len;
    bool auth_error;
    uint64_t err_time;
    int32_t intro_step;
    int32_t slide_step;
    bool slide_target_open;
    nsm_spring_t shake_sp;
    int32_t hov_user_q8[MAX_LOGIN_USERS];
    int32_t hov_acc_q8;
    int32_t hov_off_q8;
    bool exiting;
    int32_t exit_step;
    int exit_result;
    uint64_t cursor_tick;
} login_ui_state_t;

static void mod_memzero(void* dst, size_t n) {
    uint8_t* p = (uint8_t*)dst;
    for (size_t i = 0; i < n; i++) p[i] = 0;
}

static size_t mod_strlen(const char* s) {
    size_t len = 0;
    while (s && s[len]) len++;
    return len;
}

static int mod_strcmp(const char* s1, const char* s2) {
    if (!s1 || !s2) return s1 ? 1 : (s2 ? -1 : 0);
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static int mod_strncmp(const char* s1, const char* s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i] || s1[i] == '\0') {
            return (unsigned char)s1[i] - (unsigned char)s2[i];
        }
    }
    return 0;
}

static void mod_strcpy(char* dest, const char* src, size_t max_len) {
    if (!dest || !src || max_len == 0) return;
    size_t i = 0;
    while (i + 1 < max_len && src[i]) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

static void mod_strcat(char* dest, const char* src, size_t max_len) {
    if (!dest || !src) return;
    size_t dlen = mod_strlen(dest);
    if (dlen >= max_len) return;
    mod_strcpy(dest + dlen, src, max_len - dlen);
}

static uint32_t lerp_color_fallback(uint32_t c1, uint32_t c2, int32_t t_q8) {
    if (t_q8 <= 0) return c1;
    if (t_q8 >= 256) return c2;
    uint32_t r1 = (c1 >> 16) & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = c1 & 0xFF;
    uint32_t r2 = (c2 >> 16) & 0xFF, g2 = (c2 >> 8) & 0xFF, b2 = c2 & 0xFF;
    uint32_t r = r1 + (((int32_t)r2 - (int32_t)r1) * t_q8) / 256;
    uint32_t g = g1 + (((int32_t)g2 - (int32_t)g1) * t_q8) / 256;
    uint32_t b = b1 + (((int32_t)b2 - (int32_t)b1) * t_q8) / 256;
    return (r << 16) | (g << 8) | b;
}

static void read_user_cfg_password(const kernel_api_t* api, const char* username, char* out_pass, size_t max_len) {
    out_pass[0] = '\0';
    if (!api || !api->vfs_read_entire_file || !username || !username[0]) return;

    char path[128] = "C:\\User\\";
    mod_strcat(path, username, sizeof(path));
    mod_strcat(path, "\\USER.CFG", sizeof(path));

    uint32_t sz = 0;
    char* cfg = (char*)api->vfs_read_entire_file(path, &sz);
    if (!cfg || sz == 0) {
        if (cfg && api->kfree) api->kfree(cfg);
        return;
    }

    char* p = cfg;
    char* end = cfg + sz;
    while (p < end) {
        char* nl = p;
        while (nl < end && *nl != '\n' && *nl != '\r') nl++;
        if (nl - p >= 5 && mod_strncmp(p, "PASS=", 5) == 0) {
            size_t plen = (size_t)(nl - (p + 5));
            if (plen >= max_len) plen = max_len - 1;
            for (size_t i = 0; i < plen; i++) out_pass[i] = p[5 + i];
            out_pass[plen] = '\0';
            break;
        }
        p = nl + 1;
    }
    if (api->kfree) api->kfree(cfg);
}

static int build_user_list(const kernel_api_t* api, const loginscreen_params_t* params,
                           user_profile_t* primary_user, login_user_entry_t* out_users) {
    int count = 0;
    const uint32_t palette[4] = {0x1D5EC4, 0x0E7490, 0x6D28D9, 0x047857};

    const char* primary_name = "Morgan";
    if (primary_user && primary_user->username[0]) {
        primary_name = primary_user->username;
    } else if (params && params->default_user[0]) {
        primary_name = params->default_user;
    }

    mod_strcpy(out_users[count].username, primary_name, 32);
    mod_strcpy(out_users[count].password,
               (primary_user && primary_user->password[0]) ? primary_user->password : "", 32);
    if (out_users[count].password[0] == '\0') {
        read_user_cfg_password(api, primary_name, out_users[count].password, 32);
    }
    mod_strcpy(out_users[count].role, "Amministratore", 28);
    out_users[count].is_guest = false;
    out_users[count].avatar_col = palette[0];
    count++;

    if (api->vfs_readdir) {
        vfs_dirent_t entries[12];
        int n = api->vfs_readdir("C:\\User", entries, 12);
        for (int i = 0; i < n && count < MAX_LOGIN_USERS - 1; i++) {
            if (!entries[i].is_directory || entries[i].name[0] == '.') continue;
            if (mod_strcmp(entries[i].name, "Default") == 0 || mod_strcmp(entries[i].name, "Ospite") == 0) continue;

            bool dup = false;
            for (int j = 0; j < count; j++) {
                if (mod_strcmp(out_users[j].username, entries[i].name) == 0) {
                    dup = true;
                    break;
                }
            }
            if (dup) continue;

            mod_strcpy(out_users[count].username, entries[i].name, 32);
            read_user_cfg_password(api, entries[i].name, out_users[count].password, 32);
            mod_strcpy(out_users[count].role, "Utente Locale", 28);
            out_users[count].is_guest = false;
            out_users[count].avatar_col = palette[count % 4];
            count++;
        }
    }

    mod_strcpy(out_users[count].username, "Ospite", 32);
    out_users[count].password[0] = '\0';
    mod_strcpy(out_users[count].role, "Sessione Temporanea", 28);
    out_users[count].is_guest = true;
    out_users[count].avatar_col = 0x475569;
    count++;

    return count;
}

static void login_try_auth(const kernel_api_t* api, const login_user_entry_t* sel,
                           login_ui_state_t* st, uint64_t now) {
    if (sel->is_guest) {
        if (api->user_set_profile) {
            api->user_set_profile("Ospite", "", "NEWOS-GUEST", 0, 0, false);
        }
        st->exiting = true;
        st->exit_step = 0;
        st->exit_result = LOGIN_GUEST;
        return;
    }

    bool ok = (sel->password[0] == '\0' || mod_strcmp(st->password, sel->password) == 0);
    if (ok) {
        if (api->user_set_profile) {
            api->user_set_profile(sel->username, sel->password, "NEWOS-PC", 1, 0, true);
        }
        if (api->serial_puts) {
            api->serial_puts("[LOGIN.MOD] Autenticazione riuscita per l'utente: ");
            api->serial_puts(sel->username);
            api->serial_puts("\n");
        }
        if (api->sound_play_wav_file) {
            if (!api->sound_play_wav_file("C:\\NewOS\\Resources\\Sounds\\DING.WAV")) {
                api->sound_play_wav_file("/NewOS/Resources/Sounds/DING.WAV");
            }
        }
        st->exiting = true;
        st->exit_step = 0;
        st->exit_result = LOGIN_SUCCESS;
    } else {
        st->auth_error = true;
        st->err_time = now;
        st->password[0] = '\0';
        st->pwd_len = 0;
        st->shake_sp.pos = 0;
        st->shake_sp.vel = 22 * 256;
        st->shake_sp.target = 0;
        if (api->sound_play) api->sound_play(2);
    }
}

static bool login_render_user_list(const kernel_api_t* api, const nsmotion_api_t* motion,
                                   const login_user_entry_t* users, int user_count,
                                   login_ui_state_t* st, const mouse_state_t* mouse, bool clicked,
                                   int32_t left_x, int32_t card_y, int32_t left_w, int32_t card_h) {
    if (api->gfx_draw_windows_logo) api->gfx_draw_windows_logo(left_x + 18, card_y + 16);
    if (api->gfx_draw_string) api->gfx_draw_string(left_x + 42, card_y + 18, "Lista User", 0x0D1B36, 0, true);
    if (api->gfx_fill_alpha) api->gfx_fill_alpha(left_x + 16, card_y + 44, left_w - 32, 1, 0x4A658A, 80);

    int32_t list_y0 = card_y + 58;
    int32_t item_h  = 52;
    int32_t item_gap = 8;
    int32_t item_x  = left_x + 16;
    int32_t item_w  = left_w - 32;

    for (int i = 0; i < user_count; i++) {
        int32_t iy = list_y0 + i * (item_h + item_gap);
        bool is_sel = (st->selected_idx == i);
        bool hov = !st->exiting && (mouse->x >= item_x && mouse->x <= item_x + item_w &&
                                    mouse->y >= iy && mouse->y <= iy + item_h);
        st->hov_user_q8[i] += hov ? 52 : -52;
        if (st->hov_user_q8[i] < 0) st->hov_user_q8[i] = 0;
        if (st->hov_user_q8[i] > 256) st->hov_user_q8[i] = 256;

        uint32_t base_box = is_sel ? 0xD2E4FF : 0xEDF3FC;
        uint32_t hov_box  = is_sel ? 0xC6DDFE : 0xFFFFFF;
        uint32_t box_col  = (motion && motion->lerp_argb)
            ? motion->lerp_argb(base_box, hov_box, st->hov_user_q8[i] << 8)
            : lerp_color_fallback(base_box, hov_box, st->hov_user_q8[i]);

        if (api->gfx_fill_rounded_alpha) {
            if (is_sel) {
                api->gfx_fill_rounded_alpha(item_x - 2, iy - 2, item_w + 4, item_h + 4, 11, 0x246BFD, 210);
            }
            api->gfx_fill_rounded_alpha(item_x, iy, item_w, item_h, 10, box_col, is_sel ? 238 : 195);
            api->gfx_fill_rounded_alpha(item_x + 10, iy + 9, 34, 34, 17, users[i].avatar_col, 230);
        }
        if (api->gfx_draw_string) {
            char init_s[2] = {users[i].username[0] ? users[i].username[0] : 'U', '\0'};
            if (init_s[0] >= 'a' && init_s[0] <= 'z') init_s[0] -= 32;
            api->gfx_draw_string(item_x + 23, iy + 18, init_s, 0xFFFFFF, 0, true);
            api->gfx_draw_string(item_x + 54, iy + 11, users[i].username, 0x0A1A38, 0, true);
            api->gfx_draw_string(item_x + 54, iy + 29, users[i].role, 0x475569, 0, true);
        }

        if (clicked && hov) {
            if (st->selected_idx == i && st->slide_target_open) {
                st->selected_idx = -1;
                st->slide_target_open = false;
            } else {
                st->selected_idx = i;
                st->slide_target_open = true;
            }
            st->password[0] = '\0';
            st->pwd_len = 0;
            st->auth_error = false;
        }
    }

    int32_t btn_off_x = left_x + 16;
    int32_t btn_off_y = card_y + card_h - 44;
    int32_t btn_off_w = left_w - 32;
    int32_t btn_off_h = 28;
    bool hov_off = !st->exiting && (mouse->x >= btn_off_x && mouse->x <= btn_off_x + btn_off_w &&
                                    mouse->y >= btn_off_y && mouse->y <= btn_off_y + btn_off_h);
    st->hov_off_q8 += hov_off ? 48 : -48;
    if (st->hov_off_q8 < 0) st->hov_off_q8 = 0;
    if (st->hov_off_q8 > 256) st->hov_off_q8 = 256;
    uint32_t col_off = (motion && motion->lerp_argb)
        ? motion->lerp_argb(0xE4EAF4, 0xFCE4E4, st->hov_off_q8 << 8)
        : lerp_color_fallback(0xE4EAF4, 0xFCE4E4, st->hov_off_q8);
    if (api->gfx_fill_rounded_rect) api->gfx_fill_rounded_rect(btn_off_x, btn_off_y, btn_off_w, btn_off_h, 8, col_off);
    if (api->gfx_draw_string) {
        api->gfx_draw_string(btn_off_x + (btn_off_w - 72) / 2, btn_off_y + 7,
                             "Spegni PC", hov_off ? 0xB71C1C : 0x2B3C54, 0, true);
    }
    return (clicked && hov_off);
}

static void login_render_access_panel(const kernel_api_t* api, const nsmotion_api_t* motion,
                                      const login_user_entry_t* sel, login_ui_state_t* st,
                                      const mouse_state_t* mouse, bool clicked, uint64_t now,
                                      int32_t rx, int32_t card_y, int32_t cur_right_w, int32_t card_h) {
    if (api->gfx_fill_alpha) api->gfx_fill_alpha(rx, card_y + 18, 1, card_h - 36, 0x4A658A, 95);
    if (cur_right_w < 260) return;

    int32_t pad_x = rx + 24;
    int32_t inner_w = cur_right_w - 48;

    if (api->gfx_draw_string) api->gfx_draw_string(pad_x, card_y + 18, "Controlli Accesso", 0x0D1B36, 0, true);
    if (api->gfx_fill_alpha) api->gfx_fill_alpha(pad_x, card_y + 44, inner_w, 1, 0x4A658A, 80);

    int32_t sel_y = card_y + 60;
    if (api->gfx_fill_rounded_alpha) {
        api->gfx_fill_rounded_alpha(pad_x, sel_y, inner_w, 64, 10, 0xFFFFFF, 165);
        api->gfx_fill_rounded_alpha(pad_x + 12, sel_y + 11, 42, 42, 21, sel->avatar_col, 235);
    }
    if (api->gfx_draw_string) {
        char init_s[2] = {sel->username[0] ? sel->username[0] : 'U', '\0'};
        if (init_s[0] >= 'a' && init_s[0] <= 'z') init_s[0] -= 32;
        api->gfx_draw_string(pad_x + 29, sel_y + 24, init_s, 0xFFFFFF, 0, true);
        api->gfx_draw_string(pad_x + 66, sel_y + 14, sel->username, 0x0A1A38, 0, true);
        api->gfx_draw_string(pad_x + 66, sel_y + 34, sel->role, 0x334766, 0, true);
    }

    int32_t pwd_lbl_y = card_y + 144;
    int32_t pwd_y     = card_y + 166;
    int32_t pwd_h     = 32;

    if (sel->is_guest) {
        if (api->gfx_draw_string) {
            api->gfx_draw_string(pad_x, pwd_lbl_y + 6, "Accesso Ospite", 0x162642, 0, true);
            api->gfx_draw_string(pad_x, pwd_y + 6, "Nessuna password richiesta.", 0x354B6E, 0, true);
        }
    } else {
        if (api->gfx_draw_string) api->gfx_draw_string(pad_x, pwd_lbl_y, "Password:", 0x162642, 0, true);
        if (api->gfx_fill_rounded_alpha) {
            uint32_t box_border = st->auth_error ? 0xD93025 : 0x246BFD;
            api->gfx_fill_rounded_alpha(pad_x - 1, pwd_y - 1, inner_w + 2, pwd_h + 2, 9, box_border, 200);
            api->gfx_fill_rounded_alpha(pad_x, pwd_y, inner_w, pwd_h, 8, 0xFFFFFF, 240);
        }

        char mask_str[36];
        for (int i = 0; i < st->pwd_len && i < 32; i++) mask_str[i] = '*';
        mask_str[st->pwd_len] = '\0';
        if (api->gfx_draw_string) api->gfx_draw_string(pad_x + 12, pwd_y + 9, mask_str, 0x0D1B36, 0, true);

        st->cursor_tick++;
        if ((st->cursor_tick / 18) % 2 == 0 && api->gfx_fill_rect) {
            api->gfx_fill_rect(pad_x + 12 + (st->pwd_len * 8), pwd_y + 7, 2, 18, 0x1D5EC4);
        }

        if (st->auth_error && api->gfx_draw_string) {
            api->gfx_draw_string(pad_x, pwd_y + 42, "[X] Password errata.", 0xC62828, 0, true);
        } else if (api->gfx_draw_string) {
            api->gfx_draw_string(pad_x, pwd_y + 42, "Premi [Invio] o clicca Accedi", 0x354B6E, 0, true);
        }
    }

    int32_t btn_acc_y = card_y + card_h - 56;
    int32_t btn_acc_h = 34;
    bool hov_acc = !st->exiting && (mouse->x >= pad_x && mouse->x <= pad_x + inner_w &&
                                    mouse->y >= btn_acc_y && mouse->y <= btn_acc_y + btn_acc_h);
    st->hov_acc_q8 += hov_acc ? 48 : -48;
    if (st->hov_acc_q8 < 0) st->hov_acc_q8 = 0;
    if (st->hov_acc_q8 > 256) st->hov_acc_q8 = 256;

    uint32_t col_acc = (motion && motion->lerp_argb)
        ? motion->lerp_argb(0x1B64D8, 0x3A86FF, st->hov_acc_q8 << 8)
        : lerp_color_fallback(0x1B64D8, 0x3A86FF, st->hov_acc_q8);
    if (api->gfx_fill_rounded_rect) api->gfx_fill_rounded_rect(pad_x, btn_acc_y, inner_w, btn_acc_h, 9, col_acc);
    if (api->gfx_draw_string) {
        const char* lbl = sel->is_guest ? "Entra come Ospite" : "Accedi";
        int lbl_w = (int)mod_strlen(lbl) * 8;
        api->gfx_draw_string(pad_x + (inner_w - lbl_w) / 2, btn_acc_y + 10, lbl, 0xFFFFFF, 0, true);
    }

    if (clicked && hov_acc) {
        login_try_auth(api, sel, st, now);
    }
}

__attribute__((section(".text.entry"), used))
int loginscreen_main(const kernel_api_t* api, const loginscreen_params_t* params) {
    if (!api) return -1;

    if (api->serial_puts) {
        api->serial_puts("[LOGIN.MOD] Schermata Lista User + Slide Liquid Glass & NSMotion in esecuzione...\n");
    }

    const kernel_ext_api_t* ext = (const kernel_ext_api_t*)api->ext_api;
    const nsmotion_api_t* motion = NULL;
    if (ext && ext->get_system_service) {
        motion = (const nsmotion_api_t*)ext->get_system_service("NSMOTION");
    } else if (ext && ext->nsmotion_get_api) {
        motion = (const nsmotion_api_t*)ext->nsmotion_get_api();
    }

    uint32_t sw = api->gfx_get_width ? api->gfx_get_width() : 1024;
    uint32_t sh = api->gfx_get_height ? api->gfx_get_height() : 768;

    image_t* bg_img = NULL;
    if (api->image_load_from_file) {
        bg_img = api->image_load_from_file("C:\\NewOS\\Resources\\Screens\\LOGIN.BMP");
        if (!bg_img) bg_img = api->image_load_from_file("C:\\NewOS\\Resources\\LOGIN.BMP");
        if (!bg_img) bg_img = api->image_load_from_file("C:\\NewOS\\Resource\\wallpapers\\LOGIN.BMP");
        if (!bg_img) bg_img = api->image_load_from_file("/NewOS/Resources/Screens/LOGIN.BMP");
        if (!bg_img) bg_img = api->image_load_from_file("C:\\NewOS\\Resources\\Wallpapers\\FLOW.BMP");
    }

    user_profile_t* user = api->user_get_current ? api->user_get_current() : NULL;
    login_user_entry_t users[MAX_LOGIN_USERS];
    int user_count = build_user_list(api, params, user, users);

    login_ui_state_t st;
    mod_memzero(&st, sizeof(st));
    st.selected_idx = -1;
    st.exit_result = LOGIN_SUCCESS;

    const int32_t left_w  = 270;
    const int32_t right_w = 340;
    const int32_t card_h  = 360;
    const int32_t base_y  = ((int32_t)sh - card_h) / 2 - 12;
    const int32_t intro_total = 16;
    const int32_t slide_total = 18;
    bool prev_left = false;

    while (1) {
        uint64_t now = api->timer_get_ticks ? api->timer_get_ticks() : 0;

        if (!st.exiting && api->keyboard_has_char && api->keyboard_has_char()) {
            char c = api->keyboard_get_char();
            if (st.selected_idx < 0) {
                if (c == '\n' || c == '\r') {
                    st.selected_idx = 0;
                    st.slide_target_open = true;
                    st.slide_step = 0;
                    st.password[0] = '\0';
                    st.pwd_len = 0;
                    st.auth_error = false;
                }
            } else if (c == 27) {
                st.selected_idx = -1;
                st.slide_target_open = false;
                st.slide_step = slide_total;
                st.password[0] = '\0';
                st.pwd_len = 0;
                st.auth_error = false;
            } else if (c == '\n' || c == '\r') {
                login_try_auth(api, &users[st.selected_idx], &st, now);
            } else if (c == '\b') {
                if (st.pwd_len > 0) st.password[--st.pwd_len] = '\0';
            } else if (c >= 32 && c <= 126 && st.pwd_len < 30) {
                st.password[st.pwd_len++] = c;
                st.password[st.pwd_len] = '\0';
                st.auth_error = false;
            }
        }

        mouse_state_t mouse = {0};
        if (api->mouse_get_state) api->mouse_get_state(&mouse);
        bool clicked = !st.exiting && mouse.left_button && !prev_left;
        prev_left = mouse.left_button;

        if (st.auth_error && (now - st.err_time > 400)) st.auth_error = false;

        int32_t anim_dy = 0;
        int32_t card_alpha = 138;
        if (st.intro_step < intro_total) {
            int32_t t_q16 = (st.intro_step * NSM_ONE) / intro_total;
            int32_t e_q16 = motion && motion->ease_q16 ? motion->ease_q16(NSM_EASE_OUT_CUBIC, t_q16) : t_q16;
            anim_dy = ((NSM_ONE - e_q16) * 40) >> 16;
            card_alpha = 40 + ((e_q16 * 98) >> 16);
            st.intro_step++;
        }

        if (st.slide_target_open && st.slide_step < slide_total) st.slide_step++;
        else if (!st.slide_target_open && st.slide_step > 0) st.slide_step--;

        int32_t slide_t_q16 = (st.slide_step * NSM_ONE) / slide_total;
        int32_t slide_q16 = motion && motion->ease_q16 ? motion->ease_q16(NSM_EASE_OUT_CUBIC, slide_t_q16) : slide_t_q16;

        if (st.exiting) {
            int32_t t_q16 = (st.exit_step * NSM_ONE) / 8;
            int32_t e_q16 = motion && motion->ease_q16 ? motion->ease_q16(NSM_EASE_IN_CUBIC, t_q16) : t_q16;
            anim_dy = -((e_q16 * 28) >> 16);
            card_alpha = 138 - ((e_q16 * 118) >> 16);
            if (card_alpha < 10) card_alpha = 10;
            st.exit_step++;
            if (st.exit_step >= 8) {
                if (bg_img && api->image_free) api->image_free(bg_img);
                return st.exit_result;
            }
        }

        int32_t shake_dx = 0;
        if (motion && motion->spring_step) {
            motion->spring_step(&st.shake_sp, 180, 12);
            shake_dx = st.shake_sp.pos >> 8;
        }

        int32_t cur_right_w = (right_w * slide_q16) >> 16;
        int32_t total_w = left_w + cur_right_w;
        int32_t left_x = (((int32_t)sw - total_w) / 2) + shake_dx;
        int32_t card_y = base_y + anim_dy;

        if (bg_img && api->gfx_draw_image) api->gfx_draw_image(0, 0, bg_img);
        else if (api->gfx_draw_gradient_v) api->gfx_draw_gradient_v(0, 0, sw, sh, 0x0A162E, 0x132E58);
        else if (api->gfx_clear) api->gfx_clear(0x0A162E);

        if (api->gfx_fill_alpha) api->gfx_fill_alpha(0, 0, (int32_t)sw, (int32_t)sh, 0x040A18, 46);

        if (api->gfx_fill_rounded_alpha) {
            api->gfx_fill_rounded_alpha(left_x + 4, card_y + 10, total_w, card_h, 18, 0x000000, 52);
            api->gfx_fill_rounded_alpha(left_x + 2, card_y + 5, total_w, card_h, 16, 0x000000, 72);
        }
        if (ext && ext->nscomp_apply_window_refraction) {
            ext->nscomp_apply_window_refraction(left_x, card_y, total_w, card_h, total_w / 2, 16);
        }
        if (ext && ext->gfx_fill_rounded_glass) {
            ext->gfx_fill_rounded_glass(left_x, card_y, total_w, card_h, 14, 0xDCE8FA, (uint8_t)card_alpha);
        } else if (api->gfx_fill_rounded_alpha) {
            api->gfx_fill_rounded_alpha(left_x, card_y, total_w, card_h, 14, 0xE6EEF8, (uint8_t)card_alpha);
        }
        if (api->gfx_fill_alpha) api->gfx_fill_alpha(left_x + 14, card_y + 1, total_w - 28, 2, 0xFFFFFF, 160);

        if (login_render_user_list(api, motion, users, user_count, &st, &mouse, clicked,
                                   left_x, card_y, left_w, card_h)) {
            st.exiting = true;
            st.exit_step = 0;
            st.exit_result = LOGIN_SHUTDOWN;
        }

        if (cur_right_w > 24 && st.selected_idx >= 0) {
            login_render_access_panel(api, motion, &users[st.selected_idx], &st, &mouse, clicked, now,
                                      left_x + left_w, card_y, cur_right_w, card_h);
        }

        const char* badge = "[ Modulo Protetto Esterno: C:\\NewOS\\SystemProgram\\LOGIN.MOD - Liquid Glass Logon ]";
        int badge_len = (int)mod_strlen(badge);
        if (api->gfx_draw_string) {
            api->gfx_draw_string(((int32_t)sw - (badge_len * 8)) / 2, (int32_t)sh - 28, badge, 0xB6C8E2, 0, true);
        }
        if (api->gfx_draw_cursor) api->gfx_draw_cursor(mouse.x, mouse.y);
        if (api->gfx_flip) api->gfx_flip();
        if (api->timer_sleep) api->timer_sleep(16);
    }
}
