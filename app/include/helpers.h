#ifndef __HELPERS_H__
#define __HELPERS_H__

#include <stdint.h>
#include <stddef.h>

void sleep_ms(long milliseconds);
void sleep_ns(long nanoseconds);

// create a file for shm
int allocate_shm_file(size_t size);
uint8_t* map_pool_data(int fd, int pool_size);

#endif // __HELPERS_H__
