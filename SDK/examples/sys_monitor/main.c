/**
 * SDK/examples/sys_monitor/main.c
 * Esempio SDK: Monitor di sistema freestanding basato su NSRT.DLL v2.0
 */
#include <newos/sdk.h>

static const nsrt_api_t* g_rt = NULL;

static void refresh_stats(nsrt_app_t* app) {
    if (!g_rt || !app) return;
    ns_sys_info_t info;
    char buf[96];

    if (g_rt->get_sys_info(&info)) {
        g_rt->snprintf(buf, sizeof(buf), "Utente: %s (%s)", info.username, info.computer_name);
        g_rt->set_text(app, "lbl_user", buf);

        g_rt->snprintf(buf, sizeof(buf), "Schermo: %ux%u | Ticks: %u",
                       info.screen_w, info.screen_h, (unsigned int)info.uptime_ticks);
        g_rt->set_text(app, "lbl_res", buf);

        g_rt->snprintf(buf, sizeof(buf), "Spazio libero C:\\: %u KB / %u KB",
                       info.free_ram_kb, info.total_ram_kb);
        g_rt->set_text(app, "lbl_disk", buf);
    }

    char ip[48];
    g_rt->net_get_ip(ip, sizeof(ip));
    g_rt->snprintf(buf, sizeof(buf), "Rete: %s (%s)",
                   g_rt->net_is_connected() ? "Connessa" : "Offline", ip);
    g_rt->set_text(app, "lbl_net", buf);

    char drives[16];
    int n = g_rt->drive_list(drives, 16);
    g_rt->snprintf(buf, sizeof(buf), "Unita montate (%d): ", n);
    size_t pos = g_rt->strlen(buf);
    for (int i = 0; i < n && pos + 4 < sizeof(buf); i++) {
        buf[pos++] = drives[i];
        buf[pos++] = ':';
        buf[pos++] = ' ';
    }
    buf[pos] = '\0';
    g_rt->set_text(app, "lbl_drives", buf);
}

static void on_ui_event(nsrt_app_t* app, const char* control_id, void* user_ctx) {
    (void)user_ctx;
    const nsrt_api_t* rt = g_rt;
    if (!rt) return;

    NS_ON_CLICK("btn_refresh") { refresh_stats(app); }
    NS_ON_CLICK("btn_close")   { rt->close_app(app); }
}

void app_main(const kernel_api_t* kapi) {
    g_rt = nsrt_load_library(kapi);
    if (!g_rt) return;

    nsrt_app_t* app = g_rt->create_xuiml_app(
        "NewSoft System Monitor (SDK)",
        420, 300,
        NS_PATH_SDK "examples\\sys_monitor\\monitor.xuiml",
        on_ui_event,
        NULL
    );
    refresh_stats(app);
}
