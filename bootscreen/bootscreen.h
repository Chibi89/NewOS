#ifndef BOOTSCREEN_H
#define BOOTSCREEN_H

#include <screen_modules.h>
#include <kernel_ext_api.h>
#include "../SystemLibrary/motion/nsmotion.h"

// Entry point del modulo Bootscreen protetto
int bootscreen_main(const kernel_api_t* api, const bootscreen_params_t* params);

#endif // BOOTSCREEN_H
