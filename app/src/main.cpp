#include "helpers.h"

#include <iostream>

// wayland headers
#include <wayland-client.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#include "xdg/xdg-shell-client-protocol.h"

#define log_func() printf("%s:%d\t%s()\n", __FILE_NAME__, __LINE__, __func__)

// global state variable
struct client_state {
	// globals
	wl_display* display;
	wl_registry* registry;
	wl_shm* shm;
	wl_compositor* compositor;
	xdg_wm_base* xdg_wm_base;

	// objects
	wl_surface* surface;
	xdg_surface* xdg_surface;
	xdg_toplevel* xdg_toplevel;

	// program control
	bool done = false;
};

//
// Declare Listener callback functions and listeners
//

// wl_registry
//    global -> when a global object is registered
//    global_remove ->
static void __wl_registry_handle_global(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version);
static void __wl_registry_handle_global_remove(void* data, wl_registry* registry, uint32_t name);
static const wl_registry_listener _registry_listener = {
	.global        = __wl_registry_handle_global,
	.global_remove = __wl_registry_handle_global_remove
};

// wl_buffer
//    release -> when compositor is no longer using this buffer
static void __wl_buffer_handle_release(void* data, wl_buffer* buffer);
static const wl_buffer_listener _buffer_listener = {
	.release = __wl_buffer_handle_release,
};

// wl_shm
//    format -> broadcasts what formats are supported
static void __wl_shm_handle_format(void* data, wl_shm* shm, uint32_t format);
static const wl_shm_listener _shm_listener = {
	.format = __wl_shm_handle_format,
};

// xdg_wm_base
//    ping -> checks if the client is alive
static void __xdg_wm_base_handle_ping(void* data, xdg_wm_base* base, uint32_t serial);
static const xdg_wm_base_listener _xdg_wm_base_listener = {
	.ping = __xdg_wm_base_handle_ping,
};

// xdg_surface
//    configure -> suggest a surface change
static void __xdg_surface_handle_configure(void* data, xdg_surface* surface, uint32_t serial);
static const xdg_surface_listener _xdg_surface_listener = {
	.configure = __xdg_surface_handle_configure,
};

// xdg_toplevel
//    configure -> suggest a surface change
//    close -> surface wants to be closed
static void __xdg_toplevel_handle_configure(void* data, xdg_toplevel* toplevel, int32_t width, int32_t height, wl_array* states);
static void __xdg_toplevel_handle_close(void* data, xdg_toplevel* toplevel);
static const xdg_toplevel_listener _xdg_toplevel_listener = {
	.configure = __xdg_toplevel_handle_configure,
	.close     = __xdg_toplevel_handle_close,
	.configure_bounds = nullptr,
	.wm_capabilities = nullptr,
};




static wl_buffer* draw_frame(client_state* state) {
	log_func();

	// create shm_pool
	const int width = 900, height = 600;
	const int stride = width * 4;
	const int size = height * stride;

	int fd = allocate_shm_file(size);
	if (fd == -1) {
		return NULL;
	}

	uint8_t* pool_data = map_pool_data(fd, size);
	if (pool_data == MAP_FAILED) {
		close(fd);
		return NULL;
	}

	wl_shm_pool* pool = wl_shm_create_pool(state->shm, fd, size);
	wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_XRGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);

	// blank the buffer with all white pixels
	uint32_t* pixels = (uint32_t*)pool_data;
	memset(pixels, 0, size);

	munmap(pool_data, size);
	wl_buffer_add_listener(buffer, &_buffer_listener, state);
	return buffer;
}

int main(int argc, char* argv[]) {
	log_func();

	client_state state = {};

	state.display = wl_display_connect(NULL);
	if (!state.display) {
		fprintf(stderr, "Failed to connect to Wayland display.\n");
		return 1;
	}

	state.registry = wl_display_get_registry(state.display);
	if (!state.registry) {
		fprintf(stderr, "Filed to get registry from display.\n");
		return 1;
	}

	wl_registry_add_listener(state.registry, &_registry_listener, &state);
	wl_display_roundtrip(state.display);

	// setup shm listener
	wl_shm_add_listener(state.shm, &_shm_listener, &state);

	// setup xdg listener
	xdg_wm_base_add_listener(state.xdg_wm_base, &_xdg_wm_base_listener, &state);
	
	// flush event queue
	fprintf(stdout, "%d events occured.\n", wl_display_roundtrip(state.display));
	printf("client_state struct {\n");
	printf("   display =      %p\n", (void*)state.display);
	printf("   registry =     %p\n", (void*)state.registry);
	printf("   shm =          %p\n", (void*)state.shm);
	printf("   compositor =   %p\n", (void*)state.compositor);
	printf("   xdg_wm_base =  %p\n", (void*)state.xdg_wm_base);
	printf("   surface =      %p\n", (void*)state.surface);
	printf("   xdg_surface =  %p\n", (void*)state.xdg_surface);
	printf("   xdg_toplevel = %p\n", (void*)state.xdg_toplevel);
	printf("}\n");


	// create wl_surface
	state.surface = wl_compositor_create_surface(state.compositor);
	if (!state.surface) {
		fprintf(stderr, "Failed to create wl_surface.\n");
		return 1;
	}

	// create xdg_surface
	state.xdg_surface = xdg_wm_base_get_xdg_surface(state.xdg_wm_base, state.surface);
	state.xdg_toplevel = xdg_surface_get_toplevel(state.xdg_surface);

	xdg_surface_add_listener(state.xdg_surface, &_xdg_surface_listener, &state);
	xdg_toplevel_add_listener(state.xdg_toplevel, &_xdg_toplevel_listener, &state);
	xdg_toplevel_set_title(state.xdg_toplevel, "Example client");
	wl_surface_commit(state.surface);

	printf("Start main event loop...\n\n");
	#if 1
	long ms_elapsed = 0;
	long five_seconds_ms = 5000;
	int num_events = wl_display_dispatch(state.display);
	printf("%ld ms elapsed, %d events occured.\n", ms_elapsed, num_events);
	while(ms_elapsed < five_seconds_ms) {
		ms_elapsed += 1000;
		sleep_ms(1000);
		
		printf("%ld ms elapsed, %d events occured.\n", ms_elapsed, num_events);
		num_events = wl_display_dispatch(state.display);
		if (num_events < 0) break;
		if (state.done) break;
	}
	#endif
	printf("%ld ms elapsed\n", ms_elapsed);

	wl_display_disconnect(state.display);

	return 0;
}



//
// Define Listener callback functions
//

// wl_registry
//    global -> when a global object is registered
//    global_remove ->
static void __wl_registry_handle_global(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
	log_func();

	client_state* state = (client_state*)data;

	// check for binding to wl_compositor
	if (strcmp(interface, wl_compositor_interface.name) == 0) { // check for the wl_compositor being registered
		state->compositor = (wl_compositor*)wl_registry_bind(registry, name, &wl_compositor_interface, 1);
	}

	// check for binding to wl_shm
	if (strcmp(interface, wl_shm_interface.name) == 0) { // check for the wl_shm being registered
		state->shm = (wl_shm*)wl_registry_bind(registry, name, &wl_shm_interface, 1);
	}

	// check for binding to xdg_wm_base
	if (strcmp(interface, xdg_wm_base_interface.name) == 0) { // check for the xdg_wm_base_interface being registered
		state->xdg_wm_base = (xdg_wm_base*)wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
	}
}
static void __wl_registry_handle_global_remove(void* data, wl_registry* registry, uint32_t name) {
	log_func();

	// do nothing.
}

// wl_buffer
//    release -> when compositor is no longer using this buffer
static void __wl_buffer_handle_release(void* data, wl_buffer* buffer) {
	log_func();

	// sent by the compositor when its no longer using this buffer
	wl_buffer_destroy(buffer);
}

// wl_shm
//    format -> broadcasts what formats are supported
static void __wl_shm_handle_format(void* data, wl_shm* shm, uint32_t format) {
	log_func();

	printf("  Support SHM format: %ud\n", format);
}

// xdg_wm_base
//    ping -> checks if the client is alive
static void __xdg_wm_base_handle_ping(void* data, xdg_wm_base* base, uint32_t serial) {
	log_func();

	xdg_wm_base_pong(base, serial);
}

// xdg_surface
//    configure -> suggest a surface change
static void __xdg_surface_handle_configure(void* data, xdg_surface* surface, uint32_t serial) {
	log_func();

	client_state* state = (client_state*)data;
	xdg_surface_ack_configure(surface, serial);

	wl_buffer* buffer = draw_frame(state);
	wl_surface_attach(state->surface, buffer, 0, 0);
	wl_surface_commit(state->surface);

	wl_display_flush(state->display);
}

// xdg_toplevel
//    configure -> suggest a surface change
//    close -> surface wants to be closed
static void __xdg_toplevel_handle_configure(void* data, xdg_toplevel* toplevel, int32_t width, int32_t height, wl_array* states) {
	log_func();

	// do nothing...
}
static void __xdg_toplevel_handle_close(void* data, xdg_toplevel* toplevel) {
	log_func();

	client_state* state = (client_state*)data;

	state->done = true;
}
