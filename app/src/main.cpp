#include "helpers.h"

// std libs
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// vulkan headers
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_wayland.h>

// wayland headers
#include <wayland-client.h>
#include "xdg/xdg-shell.h"

#define log_func() printf("%s:%d\t%s()\n", __FILE_NAME__, __LINE__, __func__)

#define CHECK_WL_RESULT(_expr) \
if (!(_expr)) { \
    printf("Error executing %s.\n", #_expr);\
}

// global state variable
struct client_state {
	// wayland
	wl_display* display;
	wl_registry* registry;
	wl_compositor* compositor;
	wl_surface* surface;
	
	// xdg
	xdg_wm_base* shell;
	xdg_surface* shell_surface;
	xdg_toplevel* shell_toplevel;

	// program control
	bool done = false;
	bool resize = false;
	bool ready_to_resize = false;
	int32_t new_width = 0;
	int32_t new_height = 0;
};

//
// Declare Listener callback functions and listeners
//

// wl_registry
//    global -> when a global object is registered
//    global_remove ->
static void __wl_registry_handle_global(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version);
static const wl_registry_listener _registry_listener = {
	.global        = __wl_registry_handle_global,
	.global_remove = NULL,
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


int main(int argc, char* argv[]) {
	client_state state = {};

	CHECK_WL_RESULT(state.display = wl_display_connect(NULL));

	CHECK_WL_RESULT(state.registry = wl_display_get_registry(state.display));
	wl_registry_add_listener(state.registry, &_registry_listener, &state);
	wl_display_roundtrip(state.display);

	CHECK_WL_RESULT(state.surface = wl_compositor_create_surface(state.compositor));

	CHECK_WL_RESULT(state.shell_surface = xdg_wm_base_get_xdg_surface(state.shell, state.surface));
	xdg_surface_add_listener(state.shell_surface, &_xdg_surface_listener, &state);

	CHECK_WL_RESULT(state.shell_toplevel = xdg_surface_get_toplevel(state.shell_surface));
	xdg_toplevel_add_listener(state.shell_toplevel, &_xdg_toplevel_listener, &state);
	
	xdg_toplevel_set_title(state.shell_toplevel, "Wayland + Vulkan :)");
	xdg_toplevel_set_app_id(state.shell_toplevel, "wayland_vulkan_smile");

	wl_surface_commit(state.surface);
	wl_display_roundtrip(state.display);
	wl_surface_commit(state.surface);
	
	printf("client_state struct {\n");
	printf("   display =      %p\n", (void*)state.display);
	printf("   registry =     %p\n", (void*)state.registry);
	printf("   compositor =   %p\n", (void*)state.compositor);
	printf("   surface =      %p\n", (void*)state.surface);
	printf("   xdg_wm_base =  %p\n", (void*)state.shell);
	printf("   xdg_surface =  %p\n", (void*)state.shell_surface);
	printf("   xdg_toplevel = %p\n", (void*)state.shell_toplevel);
	printf("}\n");

	printf("Start main event loop...\n\n");

	int count = 0;
	while (!state.done) {
		sleep_ms(100);

		wl_display_roundtrip(state.display);

		count++;
		if (count > 20) break;
	}

	// Cleanup
	printf("Cleaning up resources.\n");
	xdg_toplevel_destroy(state.shell_toplevel);
	xdg_surface_destroy(state.shell_surface);
	wl_surface_destroy(state.surface);
	xdg_wm_base_destroy(state.shell);
	wl_compositor_destroy(state.compositor);
	wl_registry_destroy(state.registry);
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
	client_state* state = (client_state*)data;

	// check for binding to wl_compositor
	if (strcmp(interface, wl_compositor_interface.name) == 0) { // check for the wl_compositor being registered
		state->compositor = (wl_compositor*)wl_registry_bind(registry, name, &wl_compositor_interface, 1);
	}

	// check for binding to xdg_wm_base
	if (strcmp(interface, xdg_wm_base_interface.name) == 0) { // check for the xdg_wm_base_interface being registered
		state->shell = (xdg_wm_base*)wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(state->shell, &_xdg_wm_base_listener, state);
	}
}

// xdg_wm_base
//    ping -> checks if the client is alive
static void __xdg_wm_base_handle_ping(void* data, xdg_wm_base* base, uint32_t serial) {
	xdg_wm_base_pong(base, serial);
}

// xdg_surface
//    configure -> suggest a surface change
static void __xdg_surface_handle_configure(void* data, xdg_surface* surface, uint32_t serial) {
	client_state* state = (client_state*)data;
	xdg_surface_ack_configure(surface, serial);

	if (state->resize) {
		state->ready_to_resize = true;
	}
}

// xdg_toplevel
//    configure -> suggest a surface change
//    close -> surface wants to be closed
static void __xdg_toplevel_handle_configure(void* data, xdg_toplevel* toplevel, int32_t width, int32_t height, wl_array* states) {
	client_state* state = (client_state*)data;

	if (width != 0 && height != 0) {
		state->resize = true;
		state->new_width = width;
		state->new_height = height;
	}
}
static void __xdg_toplevel_handle_close(void* data, xdg_toplevel* toplevel) {
	client_state* state = (client_state*)data;

	state->done = true;
}
