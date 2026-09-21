#include "helpers.h"

// std libs
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vector>

// vulkan headers
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_wayland.h>

// VulkanMemoryAllocator
#define VMA_IMPLEMENTATION // define this in ONE source file...
#include <vk_mem_alloc.h>

// GLM headers
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <tiny_obj_loader.h>

// KTX
#include <ktx.h>
#include <ktxvulkan.h>

// wayland headers
#include <wayland-client.h>
#include "xdg/xdg-shell.h"

#define log_func() printf("%s:%d\t%s()\n", __FILE_NAME__, __LINE__, __func__)

#define CHECK_WL_RESULT(_expr) \
if (!(_expr)) { \
    printf("Error executing %s.\n", #_expr);\
}

#define CHECK_VK_RESULT(_expr) \
result = _expr; \
if (result != VK_SUCCESS) { \
	printf("Error executing %s: %i\n", #_expr, result); \
}

#define GET_EXTENSION_FUNCTION(_instance, _id) ((PFN_##_id)(vkGetInstanceProcAddr(_instance, #_id)))

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

	// Vulkan
	VkInstance instance;
	VkDebugUtilsMessengerEXT debugMessenger;
	VkPhysicalDevice phys_device;
	VkDevice device;
	VkQueue queue;
	VkSwapchainKHR swapchain;
	VkSurfaceKHR vulkan_surface;
	VmaAllocator allocator;

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


// Vulkan debug utils error handler
static VkBool32 onVulkanError(
	VkDebugUtilsMessageSeverityFlagBitsEXT severity,
	VkDebugUtilsMessageTypeFlagsEXT type,
	const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
	void* userData);


int main(int argc, char* argv[]) {
	client_state state = {};

	printf("========================Wayland Setup=================================\n");
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
	printf("======================================================================\n\n\n");

	// setup Vulkan 
	printf("========================Vulkan Setup==================================\n");
	{
		VkResult result;

		VkApplicationInfo appInfo {
			.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
			.pApplicationName = "How to Vulkan",
			.apiVersion = VK_API_VERSION_1_3
		};

		const char* const instanceExtensionNames[] = {
			"VK_EXT_debug_utils",
			"VK_KHR_surface",
			"VK_KHR_wayland_surface"
		};
		VkInstanceCreateInfo instanceCreateInfo {
			.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
			.pApplicationInfo = &appInfo,
			.enabledExtensionCount = sizeof(instanceExtensionNames) / sizeof(const char*),
			.ppEnabledExtensionNames = instanceExtensionNames
		};
		CHECK_VK_RESULT(vkCreateInstance(&instanceCreateInfo, nullptr, &state.instance));

		// setup validation layers
		VkDebugUtilsMessengerCreateInfoEXT debugLayersCreateInfo {
			.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
			.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
			.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
			.pfnUserCallback = onVulkanError,
		};
		CHECK_VK_RESULT(GET_EXTENSION_FUNCTION(state.instance, vkCreateDebugUtilsMessengerEXT)(state.instance, &debugLayersCreateInfo, nullptr, &state.debugMessenger));

		uint32_t deviceCount = 0;
		CHECK_VK_RESULT(vkEnumeratePhysicalDevices(state.instance, &deviceCount, nullptr));
		std::vector<VkPhysicalDevice> devices(deviceCount);
		CHECK_VK_RESULT(vkEnumeratePhysicalDevices(state.instance, &deviceCount, devices.data()));

		uint32_t num_devices = (uint32_t)devices.size();
		printf("%d devices found:\n", num_devices);
		for (uint32_t n = 0; n < num_devices; n++) {
			VkPhysicalDeviceProperties2 deviceProperties { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
			vkGetPhysicalDeviceProperties2(devices[n], &deviceProperties);
			printf("  Selected device: %s\n", deviceProperties.properties.deviceName);
		}
		// manually choose the first physical device
		state.phys_device = devices[0];

		// find graphics capable queue
		uint32_t queueFamilyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(state.phys_device, &queueFamilyCount, nullptr);
		std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(state.phys_device, &queueFamilyCount, queueFamilies.data());
		uint32_t queueFamily = 0;
		for (size_t n = 0; n < queueFamilies.size(); n++) {
			if (queueFamilies[n].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
				queueFamily = (uint32_t)n;
				break;
			}
		}

		const float qfpriorities = 1.0f;
		VkDeviceQueueCreateInfo queueInfo {
			.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.queueFamilyIndex = queueFamily,
			.queueCount = 1,
			.pQueuePriorities = &qfpriorities,
		};

		VkPhysicalDeviceVulkan12Features enabledVk12Features{
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
			.descriptorIndexing = true,
			.shaderSampledImageArrayNonUniformIndexing = true,
			.descriptorBindingVariableDescriptorCount = true,
			.runtimeDescriptorArray = true,
			.bufferDeviceAddress = true
		};
		VkPhysicalDeviceVulkan13Features enabledVk13Features {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
			.pNext = &enabledVk12Features,
			.synchronization2 = true,
			.dynamicRendering = true
		};
		VkPhysicalDeviceFeatures enabledVk10Features {
			.samplerAnisotropy = VK_TRUE
		};

		const std::vector<const char*> deviceExtensions { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
		VkDeviceCreateInfo deviceCreateInfo {
			.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
			.pNext = &enabledVk13Features,
			.queueCreateInfoCount = 1,
			.pQueueCreateInfos = &queueInfo,
			.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size()),
			.ppEnabledExtensionNames = deviceExtensions.data(),
			.pEnabledFeatures = &enabledVk10Features
		};
		CHECK_VK_RESULT(vkCreateDevice(state.phys_device, &deviceCreateInfo, nullptr, &state.device));

		vkGetDeviceQueue(state.device, queueFamily, 0, &state.queue);

		// setup VMA
		VmaVulkanFunctions vkFunctions {
			.vkGetInstanceProcAddr = vkGetInstanceProcAddr,
			.vkGetDeviceProcAddr = vkGetDeviceProcAddr,
			.vkCreateImage = vkCreateImage
		};
		VmaAllocatorCreateInfo allocatorCreateInfo {
			.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
			.physicalDevice = state.phys_device,
			.device = state.device,
			.pVulkanFunctions = &vkFunctions,
			.instance = state.instance
		};
		CHECK_VK_RESULT(vmaCreateAllocator(&allocatorCreateInfo, &state.allocator));
	}
	// create wayland/vulkan surface
	{
		VkResult result;

		VkWaylandSurfaceCreateInfoKHR createInfo {
			.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
			.display = state.display,
			.surface = state.surface
		};
		CHECK_VK_RESULT(vkCreateWaylandSurfaceKHR(state.instance, &createInfo, NULL, &state.vulkan_surface));

		VkSurfaceCapabilitiesKHR surfaceCaps;
		CHECK_VK_RESULT(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(state.phys_device, state.vulkan_surface, &surfaceCaps));

		const VkFormat imageFormat = VK_FORMAT_B8G8R8A8_SRGB;
		VkSwapchainCreateInfoKHR swapchainCreateInfo {
			.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
			.surface = state.vulkan_surface,
			.minImageCount = surfaceCaps.minImageCount,
			.imageFormat = imageFormat,
			.imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR,
			.imageExtent { .width = 900, .height = 600 },
			.imageArrayLayers = 1,
			.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
			.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
			.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
			.presentMode = VK_PRESENT_MODE_FIFO_KHR
		};
		CHECK_VK_RESULT(vkCreateSwapchainKHR(state.device, &swapchainCreateInfo, nullptr, &state.swapchain));
	}

	printf("======================================================================\n\n\n");

	// start main loop
	printf("====================Start main event loop...==========================\n");
	printf("  running...\n");
	int count = 0;
	while (!state.done) {
		sleep_ms(100);

		wl_display_roundtrip(state.display);

		count++;
		if (count > 5) break;
	}
	printf("  done.\n");
	printf("======================================================================\n\n\n");

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




// Vulkan debug utils error handler
static VkBool32 onVulkanError(
	VkDebugUtilsMessageSeverityFlagBitsEXT severity,
	VkDebugUtilsMessageTypeFlagsEXT type,
	const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
	void* userData) {

	printf("Vulkan ");

	switch(type) {
		case VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT: {
			printf("general ");
		} break;
		case VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT: {
			printf("validation ");
		} break;
		case VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT: {
			printf("performance ");
		} break;
	}

	switch(severity) {
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT: {
			printf("(verbose): ");
		} break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT: {
			printf("(info): ");
		} break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: {
			printf("(warning): ");
		} break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT: {
			printf("(error): ");
		} break;

		default: {} break;
	}

	printf("%s\n", callbackData->pMessage);

	return 0;
}
