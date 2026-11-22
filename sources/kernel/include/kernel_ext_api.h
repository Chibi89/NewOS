#ifndef KERNEL_EXT_API_H
#define KERNEL_EXT_API_H

#include <types.h>
#include <drivers/power.h>

typedef struct {
    // WM & Dialogs
    void (*wm_focus_window)(int32_t id);
    void (*wm_set_background_type)(int bg_type);
    bool (*comdlg_open_file_dialog)(const char *init_dir, void *cb, void *userdata);
    bool (*comdlg_save_file_dialog)(const char *init_dir, const char *def_name, void *cb, void *userdata);
    void (*app_comdlg_copy_move_launch)(int is_move, const char *src_path, const char *dst_dir);
    void (*app_comdlg_ftp_stack_launch)(void);

    // App & Control Panel launchers
    void (*app_sound_props_launch)(void);
    void (*app_display_props_launch)(void);
    void (*app_power_props_launch)(void);
    void (*app_mouse_props_launch)(void);
    void (*app_devmgmt_launch)(void);
    void (*app_about_launch)(void);
    void (*app_user_accounts_launch)(void);
    void (*app_net_props_launch)(void);
    void (*app_timedate_props_launch)(void);
    void (*app_keyboard_props_launch)(void);
    void (*app_browser_launch)(void);
    void (*app_browser_open_url)(const char *url);
    void (*app_explorer_open_this_pc)(void);
    void (*app_explorer_open_drive_c)(void);
    void (*app_explorer_open_drive_u)(void);
    void (*app_explorer_open_drive_d)(void);
    void (*app_explorer_open_recycle_bin)(void);
    void (*app_recycle_quota_prompt_launch)(const char *target_path);
    void (*app_recycle_settings_launch)(void);
    void (*app_oobe_launch)(void);

    // Audio
    bool (*ac97_is_available)(void);
    void (*ac97_set_volume)(uint8_t vol);
    void (*ac97_stop)(void);
    void (*sound_stop)(void);
    void (*sound_set_volume)(uint8_t vol);

    // CPU, Sched, IPC, Paging, Power
    void *(*smp_get_info)(void);
    void *(*cpu_get_info)(void);
    void *(*sched_get_task)(int idx);
    const char *(*sched_state_to_str)(int state);
    int (*sched_get_task_count)(void);
    bool (*task_kill)(uint32_t pid);
    uint32_t (*ipc_pending_msg_count)(uint32_t pid);
    void (*paging_get_stats)(void *stats);
    void (*power_enter_screen_standby)(void);
    power_status_t (*power_get_status)(void);

    // Registry diagnostics & tree
    int (*registry_get_log_count)(void);
    const char *(*registry_get_log_line)(int idx);
    int (*registry_get_root_count)(void);
    void *(*registry_get_root)(int idx);
    void *(*registry_get_key)(const char *path);
    const char *(*registry_get_risk_string)(int level);
    int (*registry_get_risk_level)(const char *path, const char *val_name);

    // VFS, ExFAT, DriverMgr, USB, WiFi, VGA, Net
    bool (*vfs_get_drive_info)(char drive_letter, void *out_info);
    int (*vfs_list_drives)(char *out_letters, int max_drives);
    bool (*vfs_wildcard_match)(const char *pattern, const char *str);
    bool (*vfs_mount_drive)(char letter, const char *fs_name, const char *label, uint32_t mb, uint64_t lba, bool rw, void *ops);
    int64_t (*vfs_seek)(void *file, int64_t offset, int whence);
    bool (*vfs_is_protected)(const char *path);
    bool (*exfat_check_boot_integrity)(void *out_info);
    uint32_t (*exfat_get_used_mb)(void);
    uint32_t (*exfat_get_allocated_clusters)(void);
    uint32_t (*exfat_get_free_clusters)(void);
    int (*driver_mgr_get_count)(void);
    void *(*driver_mgr_get_driver)(int idx);
    const char *(*driver_mgr_status_to_str)(int status);
    bool (*xhci_is_active)(void);
    void *(*xhci_get_controller)(void);
    bool (*uhci_is_active)(void);
    void *(*uhci_get_controller)(void);
    bool (*ohci_is_active)(void);
    void *(*ohci_get_controller)(void);
    int (*usb_get_device_count)(void);
    void *(*usb_get_device)(int idx);
    const char *(*usb_controller_type_to_str)(int t);
    const char *(*usb_speed_to_str)(int s);
    const char *(*usb_class_to_str)(uint8_t c);
    int (*wifi_scan)(void *out_nets, int max_nets);
    bool (*wifi_connect)(const char *ssid, const char *pass);
    bool (*wifi_disconnect)(void);
    bool (*wifi_get_info)(void *out_info);
    void (*wifi_set_hardware_cap)(uint32_t rate, const char *chip);
    const char *(*wifi_bus_to_str)(int b);
    const char *(*wifi_band_to_str)(int b);
    bool (*vga_is_bga_supported)(void);
    int (*vga_get_mode_count)(void);
    void *(*vga_get_mode_info)(int idx);
    bool (*vga_set_mode_index)(int idx);
    bool (*vga_set_resolution)(uint32_t w, uint32_t h);
    int (*dhcp_get_state)(void);
    const char *(*dhcp_state_to_str)(int s);
    int (*tcp_get_connections_count)(void);
    void *(*tcp_get_connection)(int idx);
    const char *(*tcp_state_to_str)(int s);

    // Extra per Control Panel Applets (CPLEXT2.DLL)
    void *(*kernel_get_boot_info)(void);
    void *(*ata_get_primary_master)(void);
    int (*ata_get_device_count)(void);
    const char *(*ac97_get_device_name)(void);
    bool (*hwaccel_is_ready)(void);
    uint64_t (*gfx_get_lfb_phys)(void);
    bool (*hwaccel_init)(uint64_t lfb_phys, uint32_t w, uint32_t h);
    void *(*hwaccel_get_info)(void);
    bool (*hwaccel_set_scale)(int mode, uint32_t pct);
    int (*wm_get_background_type)(void);
    void (*wm_set_theme)(int theme);
    int (*vga_get_current_mode_index)(void);
    bool (*vga_init)(void);
    int (*theme_mgr_init)(const char *path);
    int (*theme_mgr_find_by_id)(int id);
    void *(*theme_mgr_get)(int idx);
    int (*theme_mgr_get_count)(void);
    bool (*theme_mgr_apply)(int idx);
    uint8_t (*sound_get_volume)(void);
    const char *(*sound_get_driver_name)(void);
    bool (*sound_is_ac97)(void);
    int (*cursor_mgr_get_active_scheme_index)(void);
    int (*cursor_mgr_get_scheme_count)(void);
    void *(*cursor_mgr_get_scheme_info)(int idx);
    void (*cursor_mgr_draw)(int32_t x, int32_t y, int type);
    bool (*cursor_mgr_set_active_scheme)(int idx);
    void (*gfx_draw_mini_monitor)(int32_t x, int32_t y, int sel);
    uint32_t (*power_get_idle_seconds)(void);
    void (*power_set_profile)(int prof);
    void (*power_set_screen_timeout)(uint32_t sec);
    void (*app_notepad_open_file)(const char *t, const char *c);
    void (*driver_mgr_init)(void);
    void (*app_calc_launch)(void);
    void (*app_cmd_launch)(void);
    void (*app_notepad_launch)(void);
    void (*app_player_launch)(void);
    void (*app_player_open_file)(const char *filepath);
    void (*app_regedit_launch)(void);
    void (*app_rules_viewer_launch)(void);
    void (*app_rules_viewer_open_file)(const char *filepath);
    void (*app_taskmgr_launch)(void);
    const void *(*ata_get_device)(int index);
    const char *(*atapi_get_disc_type_name)(const void *dev);
    bool (*comdlg_exec_file_op)(int op, const char *src_path, const char *dst_path);
    void (*context_menu_build_for_empty)(void *menu, const char *current_dir, bool can_paste);
    void (*context_menu_build_for_item)(void *menu, const char *name, const char *full_path, bool is_folder, bool can_paste);
    void (*context_menu_draw)(const void *menu);
    bool (*context_menu_execute_primary)(const void *menu);
    int (*context_menu_handle_click)(void *menu, int32_t screen_x, int32_t screen_y);
    void (*context_menu_show)(void *menu, int32_t screen_x, int32_t screen_y, int32_t bound_w, int32_t bound_h);
    void (*gfx_set_custom_wallpaper)(const uint32_t *pixels, int32_t width, int32_t height);
    bool (*recycle_empty_bin)(void);
    bool (*recycle_get_stats)(uint32_t *out_used_bytes, uint32_t *out_limit_bytes, uint32_t *out_count);
    bool (*recycle_restore_file)(const char *basename);
    bool (*settings_handle_key)(const void *api, char key, void *state);
    int (*settings_handle_mouse)(const void *api, int32_t cx, int32_t cy, int32_t cw, int32_t ch, int32_t mx, int32_t my, bool clicked, void *state);
    void (*settings_loader_init)(void);
    void (*settings_render)(const void *api, int32_t cx, int32_t cy, int32_t cw, int32_t ch, int32_t mx, int32_t my, bool is_glass, uint32_t bg_color, void *state, const void *cur_theme);
    bool (*theme_mgr_render_script)(const char *script, int32_t x, int32_t y, int32_t w, int32_t h, int32_t radius, uint32_t flags);
    int (*uistyle_get_active)(void);
    bool (*uistyle_set_active)(int style_id);
    const void *(*usb_msc_get_device)(void);
    bool (*usb_msc_is_present)(void);
    uint32_t (*pci_read32)(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
    uint16_t (*pci_read16)(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
    uint8_t (*pci_read8)(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
    void (*pci_write32)(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val);
    void (*pci_write16)(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t val);
    bool (*pci_find_device)(uint16_t vendor_id, uint16_t device_id, void *dev);
    bool (*pci_find_class)(uint8_t class_id, uint8_t subclass_id, void *dev);
    bool (*pci_find_class_prog_if)(uint8_t class_id, uint8_t subclass_id, uint8_t prog_if, void *dev);
    void (*pci_enable_bus_master)(void *dev);
    bool (*ata_read_sectors)(uint64_t lba, uint32_t count, void *buffer);
    bool (*ata_write_sectors)(uint64_t lba, uint32_t count, const void *buffer);
    bool (*ata_read_device_sectors)(const void *dev, uint64_t lba, uint32_t count, void *buffer);
    bool (*ata_write_device_sectors)(const void *dev, uint64_t lba, uint32_t count, const void *buffer);
    bool (*atapi_read_cd_sector)(const void *dev, uint32_t cd_lba, void *buf2048);
    bool (*atapi_read_cd_sectors)(const void *dev, uint32_t cd_lba, uint32_t count, void *buffer);
    bool (*usb_msc_read_sector)(uint32_t lba, void *buffer);
    bool (*usb_msc_write_sector)(uint32_t lba, const void *buffer);
    bool (*vfs_register_driver)(const void *driver);
    bool (*xhci_init)(void);
    void *(*usb_find_wireless_device)(void);
    void (*usb_subsystem_init)(void);
    void *(*kmalloc_aligned)(size_t size, size_t alignment);
    int (*pci_get_device_count)(void);
    const void *(*pci_get_device)(int index);
    uint64_t (*pci_get_bar_addr)(const void *dev, int bar_idx);
    volatile void *(*paging_map_mmio)(uint64_t phys_addr, uint64_t size);
    void *(*dma_alloc)(size_t size, size_t align, uint64_t *out_phys);
    const void *(*acpi_get_info)(void);
    const void *(*gpu3d_get_info)(void);
    bool (*nvme_is_present)(void);
    const char *(*nvme_get_model)(void);
    uint64_t (*nvme_get_capacity_sectors)(void);
    bool (*launcher_open)(const char *target, const char *args);
    bool (*uistyle_open_explorer)(const char *path);
    void (*app_installer_launch)(void);
    void (*app_seccpl_launch)(void);
    const void *(*comctl_get_api)(void);
    bool (*keyboard_is_ctrl_pressed)(void);
    bool (*fontmgr_set_ttf)(const char *path);
    void (*app_fonts_props_launch)(void);
    void (*app_pkgsvc_launch)(void);
    void (*app_ports_props_launch)(void);
    void (*wm_set_start_menu_mode)(int mode);
    int  (*wm_get_start_menu_mode)(void);
} kernel_ext_api_t;

const kernel_ext_api_t *screen_mgr_get_ext_api(void);

#endif
