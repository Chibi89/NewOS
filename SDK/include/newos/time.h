/**
 * newos/time.h — Namespace Tempo, RTC e Stopwatch
 */
#ifndef NEWOS_SDK_TIME_H
#define NEWOS_SDK_TIME_H

#include "types.h"
#include <nsrt.h>

typedef struct {
    uint64_t start_ticks;
    uint64_t end_ticks;
    bool     running;
} ns_stopwatch_t;

static inline void ns_stopwatch_start(ns_stopwatch_t* sw, const nsrt_api_t* rt) {
    if (!sw || !rt || !rt->get_ticks) return;
    sw->start_ticks = rt->get_ticks();
    sw->end_ticks = sw->start_ticks;
    sw->running = true;
}

static inline uint64_t ns_stopwatch_elapsed(ns_stopwatch_t* sw, const nsrt_api_t* rt) {
    if (!sw) return 0;
    if (sw->running && rt && rt->get_ticks) {
        sw->end_ticks = rt->get_ticks();
    }
    return sw->end_ticks - sw->start_ticks;
}

static inline void ns_stopwatch_stop(ns_stopwatch_t* sw, const nsrt_api_t* rt) {
    if (!sw) return;
    if (rt && rt->get_ticks) sw->end_ticks = rt->get_ticks();
    sw->running = false;
}

#endif /* NEWOS_SDK_TIME_H */
