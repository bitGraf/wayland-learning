#include "helpers.h"

#ifndef _POSIX_C_SOURCE
	#warning "_POSIX_C_SOURCE not defined!"
#endif

#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <stdio.h>

void sleep_ms(long milliseconds) {
	timespec ts;
	ts.tv_sec = milliseconds / 1'000;
	ts.tv_nsec = (milliseconds % 1'000) * 1'000'000;

	// printf("ms: %ld    sec:%ld    ns:%ld\n", milliseconds, ts.tv_sec, ts.tv_nsec);	

	nanosleep(&ts, NULL);
}

void sleep_ns(long nanoseconds) {
	timespec ts;
	ts.tv_sec = nanoseconds / 1'000'000'000;
	ts.tv_nsec = (nanoseconds % 1'000'000'000);
	nanosleep(&ts, NULL);
}

static void randname(char* buf) {
	timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	long r = ts.tv_nsec;
	for (int i = 0; i < 6; ++i) {
		buf[i] = 'A'+static_cast<char>((r&15)+(r&16)*2);
		r >>= 5;
	}
}
static int create_shm_file(void) {
	int retries = 100;
	do {
		char name[] = "/wl_shm-XXXXXX";
		randname(name + sizeof(name)-7);
		--retries;
		int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
		if (fd >= 0) {
			shm_unlink(name);
			// printf("shm file opened: %s\n", name);
			return fd;
		}
	} while (retries > 0 && errno == EEXIST);
	return -1;
}

// create a file for shm
int allocate_shm_file(size_t size) {
	int fd = create_shm_file();
	if (fd < 0) {
		return -1;
	}
	int ret;
	do {
		ret = ftruncate(fd, (__off_t)size);
	} while (ret < 0 && errno == EINTR);
	if (ret < 0) {
		close(fd);
		return -1;
	}
	return fd;
}


uint8_t* map_pool_data(int fd, int pool_size) {
	uint8_t* pool_data = (uint8_t*)mmap(NULL, (size_t)pool_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

	return pool_data;
}
