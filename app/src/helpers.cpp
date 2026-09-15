#include "helpers.h"

#ifndef _POSIX_C_SOURCE
	#warning "_POSIX_C_SOURCE not defined!"
#endif

#include <time.h>
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

