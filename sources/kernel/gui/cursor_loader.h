/**
 * kernel/gui/cursor_loader.h
 * Header del loader client out-of-kernel per CURSORMGR.DLL
 * (C) Newspoint Software - NewSoft Dynamic System Subsystem
 */

#ifndef GUI_CURSOR_LOADER_H
#define GUI_CURSOR_LOADER_H

#include <gui/cursor.h>

bool cursor_loader_init(void);
bool cursor_mgr_draw(int32_t x, int32_t y, cursor_system_state_t state);
int  cursor_mgr_get_scheme_count(void);
const cursor_scheme_info_t* cursor_mgr_get_scheme_info(int index);
bool cursor_mgr_set_active_scheme(int index);
int  cursor_mgr_get_active_scheme_index(void);
int  cursor_mgr_get_single_count(void);
const cursor_single_info_t* cursor_mgr_get_single_info(int index);
const char* cursor_mgr_get_state_name(cursor_system_state_t state);

#endif // GUI_CURSOR_LOADER_H
