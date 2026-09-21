#ifndef __HELPERS_H__
#define __HELPERS_H__

#include <stdint.h>
#include <stddef.h>

void sleep_ms(long milliseconds);
void sleep_ns(long nanoseconds);

uint64_t get_time_ms();

#endif // __HELPERS_H__
