#include <iostream>

#include "helpers.h"

// wayland headers
#include <wayland-client.h>

int main(int argc, char* argv[]) {
	std::cout << "Creating wl_display..." << std::endl;

	wl_display* display = wl_display_connect(NULL);
	if (!display) {
		fprintf(stderr, "Failed to connect to Wayland display.\n");
		return 1;
	}
	fprintf(stderr, "Connection established!\n");

	#if 0
	long ms_elapsed = 0;
	long five_seconds_ms = 5000;
	while(ms_elapsed < five_seconds_ms) {
		printf("%ld ms elapsed\n", ms_elapsed);
		if (wl_display_dispatch(display) < 0) break;

		ms_elapsed += 1000;
		sleep_ms(1000);
	}
	#endif

	wl_display_disconnect(display);

	return 0;
}
