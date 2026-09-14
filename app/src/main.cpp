#include "helpers.h"

#include <iostream>

// wayland headers
#include <wayland-client.h>
#include <string.h>

// global state variable
struct our_state {
	wl_compositor* compositor;
	wl_shm* shm;
};

// 5.1 binding to globals
//static void registry_handle_global(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
//	printf("interface: '%s', version: %d, name: %d\n", interface, version, name);
//}
static void registry_handle_global_remove(void* data, wl_registry* registry, uint32_t name) {
	// do nothing
}

// 6.1 wl_compositor
static void registry_handle_global(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
	our_state* state = (our_state*)data;

	// check for binding to wl_compositor
	if (strcmp(interface, wl_compositor_interface.name) == 0) { // check for the wl_compositor being registered
		printf("interface: '%s', version: %d, name: %d\n", interface, version, name);
		state->compositor = (wl_compositor*)wl_registry_bind(registry, name, &wl_compositor_interface, 4);
	}

	// check for binding to wl_shm
	if (strcmp(interface, wl_shm_interface.name) == 0) { // check for the wl_shm being registered
		printf("interface: '%s', version: %d, name: %d\n", interface, version, name);
		state->shm = (wl_shm*)wl_registry_bind(registry, name, &wl_shm_interface, 1);
	}
}

static void shm_handle_format(void* data, wl_shm* shm, uint32_t format) {
	printf(" supported format: %u\n", format);
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

	our_state state = {};
	wl_registry_listener registry_listener;
	registry_listener.global = registry_handle_global;
	registry_listener.global_remove = registry_handle_global_remove;
	wl_registry_add_listener(registry, &registry_listener, &state);
	wl_display_roundtrip(display);

	// setup shm listener
	wl_shm_listener shm_listener;
	shm_listener.format = shm_handle_format;
	wl_shm_add_listener(state.shm, &shm_listener, &state);
	
	// flush event queue
	fprintf(stdout, "%d events occured.\n", wl_display_roundtrip(display));

	// create shm_pool
	printf("Creating a wl_pool.\n");
	const int width = 900, height = 600;
	const int stride = width * 4;
	const int shm_pool_size = height * stride * 2;

	int fd = allocate_shm_file(shm_pool_size);
	uint8_t* pool_data = map_pool_data(fd, shm_pool_size);
	wl_shm_pool* pool = wl_shm_create_pool(state.shm, fd, shm_pool_size);

	// create buffers from pool
	printf("Creating buffers from pool.\n");
	int index = 0;
	int offset = height * stride * index;
	wl_buffer* buffer = wl_shm_pool_create_buffer(pool, offset, width, height, stride, WL_SHM_FORMAT_XRGB8888);

	// blank the buffer with all white pixels
	uint32_t* pixels = (uint32_t*)&pool_data[offset];
	memset(pixels, 0, width * height * 4);

	// create wl_surface
	printf("Creating a wl_surface.\n");
	wl_surface* surface = wl_compositor_create_surface(state.compositor);
	if (!surface) {
		fprintf(stderr, "Failed to create wl_surface.\n");
		return 1;
	}

	fprintf(stdout, "%d events occured.\n", wl_display_roundtrip(display));
	
	// attach buffer to surface and commite changes
	fprintf(stdout, "Draw to surface\n");
	wl_surface_attach(surface, buffer, 0, 0);
	wl_surface_damage(surface, 0, 0, (int32_t)UINT32_MAX, (int32_t)UINT32_MAX);
	wl_surface_commit(surface);
	

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
