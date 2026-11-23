/**
 * SDK/examples/hello_xuiml/main.c
 * Esempio minimo di applicazione NewOS V1 con NewSoft Runtime (NSRT.DLL) + XUIML.DLL
 */
#include <newos/sdk.h>

static const nsrt_api_t* g_rt = NULL;
static int g_clicks = 0;

static void on_ui_event(nsrt_app_t* app, const char* control_id, void* user_ctx) {
    (void)user_ctx;
    const nsrt_api_t* rt = g_rt;
    if (!rt) return;

    NS_ON_CLICK("btn_hello") {
        g_clicks++;
        char buf[64];
        rt->snprintf(buf, sizeof(buf), "Click registrati: %d", g_clicks);
        rt->set_text(app, "lbl_count", buf);

        const char* msg = "Hello from NewSoft SDK v2.0!\n";
        if (rt->file_write(NS_PATH_USER "hello_sdk.txt", msg, (uint32_t)rt->strlen(msg), true)) {
            rt->set_text(app, "lbl_saved", "Salvato in C:\\User\\hello_sdk.txt");
            rt->play_sound(NS_SOUND_NOTIFY);
        }
        NS_LOG(rt, "HELLO", "Evento btn_hello eseguito");
    }

    NS_ON_CLICK("btn_close") {
        rt->close_app(app);
    }
}

void app_main(const kernel_api_t* kapi) {
    g_rt = nsrt_load_library(kapi);
    if (!g_rt) return;

    g_rt->create_xuiml_app(
        "Hello NewSoft SDK",
        380, 240,
        NS_PATH_SDK "examples\\hello_xuiml\\app.xuiml",
        on_ui_event,
        NULL
    );
}
