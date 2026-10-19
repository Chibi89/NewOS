#ifndef DRIVERS_TIMER_H
#define DRIVERS_TIMER_H

#include <types.h>

void timer_init(uint32_t frequency);
uint64_t timer_get_ticks(void);
void timer_sleep(uint32_t ms);

#endif // DRIVERS_TIMER_H
