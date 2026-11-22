#ifndef SYSTEM_LIBRARY_DLL_EXT_MACROS_H
#define SYSTEM_LIBRARY_DLL_EXT_MACROS_H

#include <kernel_ext_api.h>

#define g_ext_api ((const kernel_ext_api_t *)g_dll_api->ext_api)

#define kmalloc(s)               g_dll_api->kmalloc(s)
#define kfree(p)                 g_dll_api->kfree(p)
#define wm_create_window(t,x,y,w,h,a) ((window_t *)g_dll_api->wm_create_window((t),(x),(y),(w),(h),(int)(a)))
#define wm_destroy_window(id)    g_dll_api->wm_destroy_window(id)
#define wm_get_theme()           g_dll_api->wm_get_theme()
#define wm_get_current_theme_props() g_dll_api->wm_get_current_theme_props()
#define wm_draw_themed_button    g_dll_api->wm_draw_themed_button
#define wm_draw_themed_tab       g_dll_api->wm_draw_themed_tab
#define gfx_get_width()          g_dll_api->gfx_get_width()
#define gfx_get_height()         g_dll_api->gfx_get_height()
#define gfx_fill_rect            g_dll_api->gfx_fill_rect
#define gfx_draw_rect            g_dll_api->gfx_draw_rect
#define gfx_draw_windows_logo    g_dll_api->gfx_draw_windows_logo
#define gfx_fill_rounded_rect    g_dll_api->gfx_fill_rounded_rect
#define gfx_draw_rounded_rect    g_dll_api->gfx_draw_rounded_rect
#define gfx_draw_bevel_rect      g_dll_api->gfx_draw_bevel_rect
#define gfx_draw_gradient_h      g_dll_api->gfx_draw_gradient_h
#define gfx_draw_gradient_v      g_dll_api->gfx_draw_gradient_v
#define gfx_draw_string          g_dll_api->gfx_draw_string
#define gfx_draw_char            g_dll_api->gfx_draw_char
#define gfx_draw_pixel           g_dll_api->gfx_draw_pixel
#define gfx_get_pixel            g_dll_api->gfx_get_pixel
#define gfx_draw_icon_16         g_dll_api->gfx_draw_icon_16
#define gfx_draw_icon_32         g_dll_api->gfx_draw_icon_32
#define gfx_fill_rounded_alpha   g_dll_api->gfx_fill_rounded_alpha
#define vfs_read_entire_file     g_dll_api->vfs_read_entire_file
#define vfs_write_entire_file_inplace g_dll_api->vfs_write_entire_file_inplace
#define vfs_readdir(d,e,m)       g_dll_api->vfs_readdir((d),(e),(m))
#define vfs_exists               g_dll_api->vfs_exists
#define vfs_create               g_dll_api->vfs_create
#define vfs_unlink               g_dll_api->vfs_unlink
#define vfs_rename               g_dll_api->vfs_rename
#define vfs_mkdir                g_dll_api->vfs_mkdir
#define vfs_open(p)              ((vfs_file_t *)g_dll_api->vfs_open(p))
#define vfs_read(f,b,c)          g_dll_api->vfs_read((f),(b),(c))
#define vfs_write(f,b,c)         g_dll_api->vfs_write((f),(b),(c))
#define vfs_close(f)             g_dll_api->vfs_close(f)
#define vfs_file_size            g_dll_api->vfs_file_size
#define vfs_flush()              g_dll_api->vfs_flush()
#define vfs_get_drive_bus_info   g_dll_api->vfs_get_drive_bus_info
#define screen_mgr_verify_module g_dll_api->screen_mgr_verify_module
#define screen_mgr_get_api()     g_dll_api
#define user_get_current()       g_dll_api->user_get_current()
#define rtc_get_time(t)          g_dll_api->rtc_get_time(t)
#define timer_get_ticks()        g_dll_api->timer_get_ticks()
#define timer_sleep(ms)          g_dll_api->timer_sleep(ms)
#define power_shutdown()         g_dll_api->power_shutdown()
#define power_reboot()           g_dll_api->power_reboot()
#define sound_play(id)           g_dll_api->sound_play(id)
#define sound_play_wav_file(p)   g_dll_api->sound_play_wav_file(p)
#define net_nic_get_active()     g_dll_api->net_nic_get_active()
#define net_nic_poll()           g_dll_api->net_nic_poll()
#define net_format_mac           g_dll_api->net_format_mac
#define net_format_ip            g_dll_api->net_format_ip
#define net_parse_ip             g_dll_api->net_parse_ip
#define dhcp_discover()          g_dll_api->dhcp_discover()
#define icmp_ping_ip(ip,c,s,cb)  g_dll_api->icmp_ping_ip((ip),(c),(s),(cb))
#define clipboard_set_text       g_dll_api->clipboard_set_text
#define clipboard_get_text       g_dll_api->clipboard_get_text
#define clipboard_push_file      g_dll_api->clipboard_push_file
#define clipboard_get_file_count g_dll_api->clipboard_get_file_count
#define clipboard_get_file       g_dll_api->clipboard_get_file
#define clipboard_clear_files    g_dll_api->clipboard_clear_files
#define registry_is_read_only()  g_dll_api->registry_is_read_only()
#define registry_set_read_only   g_dll_api->registry_set_read_only
#define registry_set_value_sz    g_dll_api->registry_set_value_sz
#define registry_set_value_dword g_dll_api->registry_set_value_dword
#define registry_get_value_sz    g_dll_api->registry_get_value_sz
#define registry_get_value_dword g_dll_api->registry_get_value_dword
#define registry_save_to_disk()  g_dll_api->registry_save_to_disk()
#define app_explorer_open_folder g_dll_api->app_explorer_open_folder
#define app_control_panel_open() g_dll_api->app_control_panel_open()

// Estensioni via kernel_ext_api_t
#define wm_focus_window          g_ext_api->wm_focus_window
#define wm_set_background_type(t) g_ext_api->wm_set_background_type((int)(t))
#define comdlg_open_file_dialog(d,c,u) g_ext_api->comdlg_open_file_dialog((d),(void*)(c),(void*)(u))
#define comdlg_save_file_dialog(d,n,c,u) g_ext_api->comdlg_save_file_dialog((d),(n),(void*)(c),(void*)(u))
#define app_comdlg_copy_move_launch g_ext_api->app_comdlg_copy_move_launch
#define app_comdlg_ftp_stack_launch g_ext_api->app_comdlg_ftp_stack_launch
#define app_sound_props_launch() g_ext_api->app_sound_props_launch()
#define app_display_props_launch() g_ext_api->app_display_props_launch()
#define app_power_props_launch() g_ext_api->app_power_props_launch()
#define app_mouse_props_launch() g_ext_api->app_mouse_props_launch()
#define app_devmgmt_launch()     g_ext_api->app_devmgmt_launch()
#define app_about_launch()       g_ext_api->app_about_launch()
#define app_user_accounts_launch() g_ext_api->app_user_accounts_launch()
#define app_net_props_launch()   g_ext_api->app_net_props_launch()
#define app_timedate_props_launch() g_ext_api->app_timedate_props_launch()
#define app_keyboard_props_launch() g_ext_api->app_keyboard_props_launch()
#define app_browser_launch()     g_ext_api->app_browser_launch()
#define app_browser_open_url(u)  g_ext_api->app_browser_open_url(u)
#define app_explorer_open_this_pc() g_ext_api->app_explorer_open_this_pc()
#define app_explorer_open_drive_c() g_ext_api->app_explorer_open_drive_c()
#define app_explorer_open_drive_u() g_ext_api->app_explorer_open_drive_u()
#define app_explorer_open_drive_d() g_ext_api->app_explorer_open_drive_d()
#define app_explorer_open_recycle_bin() g_ext_api->app_explorer_open_recycle_bin()
#define app_recycle_quota_prompt_launch(p) g_ext_api->app_recycle_quota_prompt_launch(p)
#define app_recycle_settings_launch() g_ext_api->app_recycle_settings_launch()
#define app_oobe_launch()        g_ext_api->app_oobe_launch()
#define ac97_is_available()      g_ext_api->ac97_is_available()
#define ac97_set_volume(v)       g_ext_api->ac97_set_volume(v)
#define ac97_stop()              g_ext_api->ac97_stop()
#define sound_stop()             g_ext_api->sound_stop()
#define sound_set_volume(v)      g_ext_api->sound_set_volume(v)
#define smp_get_info()           ((const smp_info_t *)g_ext_api->smp_get_info())
#define cpu_get_info()           ((const cpu_info_t *)g_ext_api->cpu_get_info())
#define sched_get_task(i)        ((task_t *)g_ext_api->sched_get_task(i))
#define sched_state_to_str(s)    g_ext_api->sched_state_to_str((int)(s))
#define sched_get_task_count()   g_ext_api->sched_get_task_count()
#define task_kill(p)             g_ext_api->task_kill(p)
#define ipc_pending_msg_count(p) g_ext_api->ipc_pending_msg_count(p)
#define paging_get_stats(s)      g_ext_api->paging_get_stats(s)
#define power_enter_screen_standby() g_ext_api->power_enter_screen_standby()
#define power_get_status()       g_ext_api->power_get_status()
#define registry_get_log_count() g_ext_api->registry_get_log_count()
#define registry_get_log_line(i) g_ext_api->registry_get_log_line(i)
#define registry_get_root_count() g_ext_api->registry_get_root_count()
#define registry_get_root(i)     ((reg_key_t *)g_ext_api->registry_get_root(i))
#define registry_get_key(p)      ((reg_key_t *)g_ext_api->registry_get_key(p))
#define registry_get_risk_string(l) g_ext_api->registry_get_risk_string((int)(l))
#define registry_get_risk_level(p,v) ((reg_risk_t)g_ext_api->registry_get_risk_level((p),(v)))
#define vfs_get_drive_info(d,o)  g_ext_api->vfs_get_drive_info((d),(o))
#define vfs_list_drives(o,m)     g_ext_api->vfs_list_drives((o),(m))
#define vfs_wildcard_match(p,s)  g_ext_api->vfs_wildcard_match((p),(s))
#define vfs_mount_drive(l,f,lb,m,b,r,o) g_ext_api->vfs_mount_drive((l),(f),(lb),(m),(b),(r),(o))
#define vfs_seek(f,o,w)          g_ext_api->vfs_seek((f),(o),(w))
#define vfs_is_protected(p)      g_ext_api->vfs_is_protected(p)
#define exfat_check_boot_integrity(o) g_ext_api->exfat_check_boot_integrity(o)
#define exfat_get_used_mb()      g_ext_api->exfat_get_used_mb()
#define exfat_get_allocated_clusters() g_ext_api->exfat_get_allocated_clusters()
#define exfat_get_free_clusters() g_ext_api->exfat_get_free_clusters()
#define driver_mgr_get_count()   g_ext_api->driver_mgr_get_count()
#define driver_mgr_get_driver(i) ((driver_info_t *)g_ext_api->driver_mgr_get_driver(i))
#define driver_mgr_status_to_str(s) g_ext_api->driver_mgr_status_to_str((int)(s))
#define xhci_is_active()         g_ext_api->xhci_is_active()
#define xhci_get_controller()    ((xhci_controller_t *)g_ext_api->xhci_get_controller())
#define uhci_is_active()         g_ext_api->uhci_is_active()
#define uhci_get_controller()    ((uhci_controller_t *)g_ext_api->uhci_get_controller())
#define ohci_is_active()         g_ext_api->ohci_is_active()
#define ohci_get_controller()    ((ohci_controller_t *)g_ext_api->ohci_get_controller())
#define usb_get_device_count()   g_ext_api->usb_get_device_count()
#define usb_get_device(i)        ((usb_device_t *)g_ext_api->usb_get_device(i))
#define usb_controller_type_to_str(t) g_ext_api->usb_controller_type_to_str((int)(t))
#define usb_speed_to_str(s)      g_ext_api->usb_speed_to_str((int)(s))
#define usb_class_to_str(c)      g_ext_api->usb_class_to_str(c)
#define wifi_scan(o,m)           g_ext_api->wifi_scan((o),(m))
#define wifi_connect(s,p)        g_ext_api->wifi_connect((s),(p))
#define wifi_disconnect()        g_ext_api->wifi_disconnect()
#define wifi_get_info(o)         g_ext_api->wifi_get_info(o)
#define wifi_set_hardware_cap(r,c) g_ext_api->wifi_set_hardware_cap((r),(c))
#define wifi_bus_to_str(b)       g_ext_api->wifi_bus_to_str((int)(b))
#define wifi_band_to_str(b)      g_ext_api->wifi_band_to_str((int)(b))
#define vga_is_bga_supported()   g_ext_api->vga_is_bga_supported()
#define vga_get_mode_count()     g_ext_api->vga_get_mode_count()
#define vga_get_mode_info(i)     ((const vga_mode_info_t *)g_ext_api->vga_get_mode_info(i))
#define vga_set_mode_index(i)    g_ext_api->vga_set_mode_index(i)
#define vga_set_resolution(w,h)  g_ext_api->vga_set_resolution((w),(h))
#define dhcp_get_state()         g_ext_api->dhcp_get_state()
#define dhcp_state_to_str(s)     g_ext_api->dhcp_state_to_str((int)(s))
#define tcp_get_connections_count() g_ext_api->tcp_get_connections_count()
#define tcp_get_connection(i)    ((tcp_conn_t *)g_ext_api->tcp_get_connection(i))
#define tcp_state_to_str(s)      g_ext_api->tcp_state_to_str((int)(s))
#define kernel_get_boot_info()   ((const BootInfo *)g_ext_api->kernel_get_boot_info())
#define ata_get_primary_master() ((const ata_device_t *)g_ext_api->ata_get_primary_master())
#define ata_get_device_count()   g_ext_api->ata_get_device_count()
#define ac97_get_device_name()   g_ext_api->ac97_get_device_name()
#define hwaccel_is_ready()       g_ext_api->hwaccel_is_ready()
#define gfx_get_lfb_phys()       g_ext_api->gfx_get_lfb_phys()
#define hwaccel_init(p,w,h)      g_ext_api->hwaccel_init((p),(w),(h))
#define hwaccel_get_info()       ((const hwaccel_info_t *)g_ext_api->hwaccel_get_info())
#define hwaccel_set_scale(m,p)   g_ext_api->hwaccel_set_scale((int)(m),(p))
#define wm_get_background_type() g_ext_api->wm_get_background_type()
#define wm_set_theme(t)          g_ext_api->wm_set_theme((int)(t))
#define vga_get_current_mode_index() g_ext_api->vga_get_current_mode_index()
#define vga_init()               g_ext_api->vga_init()
#define theme_mgr_init(p)        g_ext_api->theme_mgr_init(p)
#define theme_mgr_find_by_id(id) g_ext_api->theme_mgr_find_by_id(id)
#define theme_mgr_get(i)         ((const theme_info_t *)g_ext_api->theme_mgr_get(i))
#define theme_mgr_get_count()    g_ext_api->theme_mgr_get_count()
#define theme_mgr_apply(i)       g_ext_api->theme_mgr_apply(i)
#define sound_get_volume()       g_ext_api->sound_get_volume()
#define sound_get_driver_name()  g_ext_api->sound_get_driver_name()
#define sound_is_ac97()          g_ext_api->sound_is_ac97()
#define cursor_mgr_get_active_scheme_index() g_ext_api->cursor_mgr_get_active_scheme_index()
#define cursor_mgr_get_scheme_count() g_ext_api->cursor_mgr_get_scheme_count()
#define cursor_mgr_get_scheme_info(i) ((const cursor_scheme_info_t *)g_ext_api->cursor_mgr_get_scheme_info(i))
#define cursor_mgr_draw(x,y,t)   g_ext_api->cursor_mgr_draw((x),(y),(int)(t))
#define cursor_mgr_set_active_scheme(i) g_ext_api->cursor_mgr_set_active_scheme(i)
#define gfx_draw_mini_monitor(x,y,s) g_ext_api->gfx_draw_mini_monitor((x),(y),(s))
#define power_get_idle_seconds() g_ext_api->power_get_idle_seconds()
#define power_set_profile(p)     g_ext_api->power_set_profile((int)(p))
#define power_set_screen_timeout(s) g_ext_api->power_set_screen_timeout(s)
#define app_notepad_open_file(t,c) g_ext_api->app_notepad_open_file((t),(c))
#define driver_mgr_init()        g_ext_api->driver_mgr_init()
#define gfx_fill_alpha(x,y,w,h,c,a) g_dll_api->gfx_fill_alpha((x),(y),(w),(h),(c),(a))
#define gfx_draw_specular_rim(x,y,w,h,r) g_dll_api->gfx_draw_specular_rim((x),(y),(w),(h),(r))
#define gfx_draw_drop_shadow(x,y,w,h,s) g_dll_api->gfx_draw_drop_shadow((x),(y),(w),(h),(s))
#define image_load_from_file(p)  g_dll_api->image_load_from_file(p)
#define image_free(img)          g_dll_api->image_free(img)
#define app_calc_launch()        g_ext_api->app_calc_launch()
#define app_cmd_launch()         g_ext_api->app_cmd_launch()
#define app_notepad_launch()     g_ext_api->app_notepad_launch()
#define app_player_launch()      g_ext_api->app_player_launch()
#define app_player_open_file(f)  g_ext_api->app_player_open_file(f)
#define app_regedit_launch()     g_ext_api->app_regedit_launch()
#define app_rules_viewer_launch() g_ext_api->app_rules_viewer_launch()
#define app_rules_viewer_open_file(f) g_ext_api->app_rules_viewer_open_file(f)
#define app_taskmgr_launch()     g_ext_api->app_taskmgr_launch()
#define ata_get_device(i)        ((const ata_device_t *)g_ext_api->ata_get_device(i))
#define atapi_get_disc_type_name(d) g_ext_api->atapi_get_disc_type_name(d)
#define comdlg_exec_file_op(o,s,d) g_ext_api->comdlg_exec_file_op((int)(o),(s),(d))
#define context_menu_build_for_empty(m,d,p) g_ext_api->context_menu_build_for_empty((m),(d),(p))
#define context_menu_build_for_item(m,n,f,is_f,p) g_ext_api->context_menu_build_for_item((m),(n),(f),(is_f),(p))
#define context_menu_draw(m)     g_ext_api->context_menu_draw(m)
#define context_menu_execute_primary(m) g_ext_api->context_menu_execute_primary(m)
#define context_menu_handle_click(m,x,y) ((context_menu_action_t)g_ext_api->context_menu_handle_click((m),(x),(y)))
#define context_menu_show(m,x,y,w,h) g_ext_api->context_menu_show((m),(x),(y),(w),(h))
#define gfx_set_custom_wallpaper(p,w,h) g_ext_api->gfx_set_custom_wallpaper((p),(w),(h))
#define recycle_empty_bin()      g_ext_api->recycle_empty_bin()
#define recycle_get_stats(u,l,c) g_ext_api->recycle_get_stats((u),(l),(c))
#define recycle_restore_file(b)  g_ext_api->recycle_restore_file(b)
#define settings_handle_key(a,k,s) g_ext_api->settings_handle_key((a),(k),(s))
#define settings_handle_mouse(a,cx,cy,cw,ch,mx,my,cl,s) ((settings_action_t)g_ext_api->settings_handle_mouse((a),(cx),(cy),(cw),(ch),(mx),(my),(cl),(s)))
#define settings_loader_init()   g_ext_api->settings_loader_init()
#define settings_render(a,cx,cy,cw,ch,mx,my,ig,bg,st,th) g_ext_api->settings_render((a),(cx),(cy),(cw),(ch),(mx),(my),(ig),(bg),(st),(th))
#define theme_mgr_render_script(s,x,y,w,h,r,f) g_ext_api->theme_mgr_render_script((s),(x),(y),(w),(h),(r),(f))
#define uistyle_get_active()     g_ext_api->uistyle_get_active()
#define uistyle_set_active(id)   g_ext_api->uistyle_set_active(id)
#define usb_msc_get_device()     ((const usb_msc_device_t *)g_ext_api->usb_msc_get_device())
#define usb_msc_is_present()     g_ext_api->usb_msc_is_present()
#define pci_read32(b,s,f,o)      g_ext_api->pci_read32((b),(s),(f),(o))
#define pci_read16(b,s,f,o)      g_ext_api->pci_read16((b),(s),(f),(o))
#define pci_read8(b,s,f,o)       g_ext_api->pci_read8((b),(s),(f),(o))
#define pci_write32(b,s,f,o,v)   g_ext_api->pci_write32((b),(s),(f),(o),(v))
#define pci_write16(b,s,f,o,v)   g_ext_api->pci_write16((b),(s),(f),(o),(v))
#define pci_find_device(v,d,dev) g_ext_api->pci_find_device((v),(d),(dev))
#define pci_find_class(c,sc,dev) g_ext_api->pci_find_class((c),(sc),(dev))
#define pci_find_class_prog_if(c,sc,p,dev) g_ext_api->pci_find_class_prog_if((c),(sc),(p),(dev))
#define pci_enable_bus_master(d) g_ext_api->pci_enable_bus_master(d)
#define ata_read_sectors(l,c,b)  g_ext_api->ata_read_sectors((l),(c),(b))
#define ata_write_sectors(l,c,b) g_ext_api->ata_write_sectors((l),(c),(b))
#define ata_read_device_sectors(d,l,c,b) g_ext_api->ata_read_device_sectors((d),(l),(c),(b))
#define ata_write_device_sectors(d,l,c,b) g_ext_api->ata_write_device_sectors((d),(l),(c),(b))
#define atapi_read_cd_sector(d,l,b) g_ext_api->atapi_read_cd_sector((d),(l),(b))
#define atapi_read_cd_sectors(d,l,c,b) g_ext_api->atapi_read_cd_sectors((d),(l),(c),(b))
#define usb_msc_read_sector(l,b) g_ext_api->usb_msc_read_sector((l),(b))
#define usb_msc_write_sector(l,b) g_ext_api->usb_msc_write_sector((l),(b))
#define vfs_register_driver(d)   g_ext_api->vfs_register_driver(d)
#define xhci_init()              g_ext_api->xhci_init()
#define usb_find_wireless_device() ((usb_device_t *)g_ext_api->usb_find_wireless_device())
#define usb_subsystem_init()     g_ext_api->usb_subsystem_init()
#define kmalloc_aligned(s,a)     g_ext_api->kmalloc_aligned((s),(a))
#define pci_get_device_count()   g_ext_api->pci_get_device_count()
#define pci_get_device(i)        ((const pci_device_t *)g_ext_api->pci_get_device(i))
#define pci_get_bar_addr(d,i)    g_ext_api->pci_get_bar_addr((d),(i))
#define paging_map_mmio(p,s)     g_ext_api->paging_map_mmio((p),(s))
#define dma_alloc(s,a,op)        g_ext_api->dma_alloc((s),(a),(op))
#define acpi_get_info()          ((const acpi_info_t *)g_ext_api->acpi_get_info())
#define gpu3d_get_info()         ((const gpu3d_info_t *)g_ext_api->gpu3d_get_info())
#define nvme_is_present()        g_ext_api->nvme_is_present()
#define nvme_get_model()         g_ext_api->nvme_get_model()
#define nvme_get_capacity_sectors() g_ext_api->nvme_get_capacity_sectors()
#define launcher_open(t,a)       g_ext_api->launcher_open((t),(a))
#define uistyle_open_explorer(p) g_ext_api->uistyle_open_explorer(p)
#define app_installer_launch()   g_ext_api->app_installer_launch()
#define app_seccpl_launch()      g_ext_api->app_seccpl_launch()

void *memset(void *dst, int c, size_t n) { return dll_memset(dst, c, n); }
void *memcpy(void *dst, const void *src, size_t n) { return dll_memcpy(dst, src, n); }
int memcmp(const void *a, const void *b, size_t n) { return dll_memcmp(a, b, n); }
#define strlen   dll_strlen
#define strcpy   dll_strcpy
#define strncpy  dll_strncpy
#define strcat   dll_strcat
#define strncat  dll_strncat
#define strcmp   dll_strcmp
#define strncmp  dll_strncmp
#define strcasecmp  dll_strcasecmp
#define strncasecmp dll_strncasecmp
#define strchr   dll_strchr
#define strrchr  dll_strrchr
#define strstr   dll_strstr
#define itoa     dll_itoa
#define mini_snprintf dll_snprintf

#endif
