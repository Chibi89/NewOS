#ifndef LOGOUTSCREEN_H
#define LOGOUTSCREEN_H

#include <screen_modules.h>
#include <kernel_ext_api.h>
#include "../SystemLibrary/motion/nsmotion.h"

// Entry point del modulo Logoutscreen protetto
int logoutscreen_main(const kernel_api_t* api, const logoutscreen_params_t* params);

#endif // LOGOUTSCREEN_H
