#include <iostream>

#include "helpers.h"

// wayland headers
#include <wayland-client.h>

static void registry_handle_global(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
	printf("interface: '%s', version: %d, name: %d\n", interface, version, name);
}
static void registry_handle_global_remove(void* data, wl_registry* registry, uint32_t name) {
	// do nothing
}

int main(int argc, char* argv[]) {
	std::cout << "Creating wl_display..." << std::endl;

	wl_display* display = wl_display_connect(NULL);
	if (!display) {
		fprintf(stderr, "Failed to connect to Wayland display.\n");
		return 1;
	}
	fprintf(stdout, "Connection established!\n");

	wl_registry* registry = wl_display_get_registry(display);
	if (!registry) {
		fprintf(stderr, "Filed to get registry from display.\n");
		return 1;
	}
	fprintf(stdout, "Registry retrieved!\n");

	wl_registry_listener registry_listener;
	registry_listener.global = registry_handle_global;
	registry_listener.global_remove = registry_handle_global_remove;
	wl_registry_add_listener(registry, &registry_listener, NULL);
	fprintf(stdout, "registry listener attached.\n");
	fprintf(stdout, "%d events occured.\n", wl_display_roundtrip(display));
	fprintf(stdout, "%d events occured.\n", wl_display_roundtrip(display));
	

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
