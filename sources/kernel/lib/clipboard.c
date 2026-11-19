/**
 * kernel/lib/clipboard.c
 * Implementazione Appunti di Sistema Globali (System Clipboard) per NewOS V1 (64-bit)
 * (C) Newspoint Software - NewSoft - NewOS V1
 */

#include <clipboard.h>
#include <string.h>

static char s_clip_text[CLIPBOARD_MAX_TEXT] = {0};
static char s_clip_files[CLIPBOARD_MAX_FILES][CLIPBOARD_MAX_PATH];
static bool s_clip_file_is_cut[CLIPBOARD_MAX_FILES];
static int  s_clip_file_count = 0;

void clipboard_set_text(const char* text) {
    if (!text) {
        s_clip_text[0] = '\0';
        return;
    }
    strncpy(s_clip_text, text, CLIPBOARD_MAX_TEXT - 1);
    s_clip_text[CLIPBOARD_MAX_TEXT - 1] = '\0';
}

const char* clipboard_get_text(void) {
    return s_clip_text;
}

bool clipboard_has_text(void) {
    return (s_clip_text[0] != '\0');
}

bool clipboard_push_file(const char* filepath, bool is_cut) {
    if (!filepath || !filepath[0] || s_clip_file_count >= CLIPBOARD_MAX_FILES) {
        return false;
    }
    strncpy(s_clip_files[s_clip_file_count], filepath, CLIPBOARD_MAX_PATH - 1);
    s_clip_files[s_clip_file_count][CLIPBOARD_MAX_PATH - 1] = '\0';
    s_clip_file_is_cut[s_clip_file_count] = is_cut;
    s_clip_file_count++;
    return true;
}

int clipboard_get_file_count(void) {
    return s_clip_file_count;
}

const char* clipboard_get_file(int index, bool* out_is_cut) {
    if (index < 0 || index >= s_clip_file_count) return NULL;
    if (out_is_cut) *out_is_cut = s_clip_file_is_cut[index];
    return s_clip_files[index];
}

void clipboard_clear_files(void) {
    s_clip_file_count = 0;
}

