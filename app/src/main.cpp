#include "helpers.h"

#define VK_NO_PROTOTYPES
#define VOLK_IMPLEMENTATION
#include <vulkan/vulkan.h>
#include <volk.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

// std libs
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vector>
#include <iostream>

// VulkanMemoryAllocator
#define VMA_IMPLEMENTATION // define this in ONE source file...
#include <vk_mem_alloc.h>

// GLM headers
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>

#include <tiny_obj_loader.h>

// KTX
#include <ktx.h>
#include <ktxvulkan.h>

#define log_func() printf("%s:%d\t%s()\n", __FILE_NAME__, __LINE__, __func__)

#define CHECK_VK_RESULT(_expr) \
result = _expr; \
if (result != VK_SUCCESS) { \
	printf("Error executing %s on line %d: %i\n", #_expr, __LINE__, result); \
}
#define CHECK_SDL_RESULT(_expr) \
if (!(_expr)) { \
	printf("Error executing %s on line %d: \n\t'%s'\n", #_expr, __LINE__, SDL_GetError()); \
}

#define GET_EXTENSION_FUNCTION(_instance, _id) ((PFN_##_id)(vkGetInstanceProcAddr(_instance, #_id)))

struct ShaderSpirvSource {
	const uint32_t* pCode;
	size_t codeSize;
};
bool load_spirv_from_file(const char* filename, ShaderSpirvSource& shaderSrc);
void free_spirv_code(ShaderSpirvSource& shaderSrc);


struct ShaderData {
    glm::mat4 projection;
    glm::mat4 view;
    glm::mat4 model[3];
    glm::vec4 lightPos{ 0.0f, -10.0f, 10.0f, 0.0f };
    uint32_t selected{1};
};

struct ShaderDataBuffer {
	VmaAllocation allocation{ VK_NULL_HANDLE };
	VmaAllocationInfo allocationInfo{};
	VkBuffer buffer{ VK_NULL_HANDLE };
	VkDeviceAddress deviceAddress{};
};

// global state variable
constexpr uint32_t maxFramesInFlight {3};
struct client_state {
	// SDL
	SDL_Window* window;

	// Vulkan
	VkInstance instance;
	VkDebugUtilsMessengerEXT debugMessenger;
	VkPhysicalDevice phys_device;
	VkDevice device;
	VkQueue queue;
	VkSwapchainKHR swapchain;
	VkSurfaceKHR vulkan_surface;
	VmaAllocator allocator;
	std::vector<VkImage> swapchainImages;
	std::vector<VkImageView> swapchainImageViews;
    VkImage depthImage;
    VkImageView depthImageView;
    VmaAllocation depthImageAllocation;
	VkBuffer vBuffer;
	VmaAllocation vBufferAllocation;
	VkShaderModule shaderModule;
	VkCommandPool commandPool;
	struct Texture {
		VmaAllocation allocation{ VK_NULL_HANDLE };
		VkImage image{ VK_NULL_HANDLE };
		VkImageView view{ VK_NULL_HANDLE };
		VkSampler sampler{ VK_NULL_HANDLE };
	};
	std::array<Texture, 3> textures{};
	VkDescriptorSetLayout descriptorSetLayoutTex;
	VkDescriptorPool descriptorPool;
	VkDescriptorSet descriptorSetTex;
	VkPipelineLayout pipelineLayout;
	VkPipeline pipeline;
	VkDeviceSize vBufSize;
	VkDeviceSize iBufSize;
	VkDeviceSize indexCount;   

	// Vulkan per-frame resources
	uint32_t frameIndex {0};
	uint32_t imageIndex {0};
	std::array<ShaderDataBuffer, maxFramesInFlight> shaderDataBuffers;
	std::array<VkCommandBuffer, maxFramesInFlight> commandBuffers;
	std::array<VkFence, maxFramesInFlight> fences;
	std::array<VkSemaphore, maxFramesInFlight> imageAcquiredSemaphores;
	std::vector<VkSemaphore> renderCompleteSemaphores;

	// program control
	bool done = false;
	bool resize = false;
	bool ready_to_resize = false;
	int32_t new_width = 0;
	int32_t new_height = 0;
	bool updateSwapchain = false;

	glm::ivec2 windowSize;
};

// mesh loading
struct Vertex {
	glm::vec3 pos;
	glm::vec3 normal;
	glm::vec2 uv;
};

// Vulkan debug utils error handler
static VkBool32 onVulkanError(
	VkDebugUtilsMessageSeverityFlagBitsEXT severity,
	VkDebugUtilsMessageTypeFlagsEXT type,
	const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
	void* userData);


int main(int argc, char* argv[]) {
	client_state state = {};

	printf("==========================SDL Setup===================================\n");
	
	CHECK_SDL_RESULT(SDL_Init(SDL_INIT_VIDEO));
	CHECK_SDL_RESULT(SDL_Vulkan_LoadLibrary(NULL));
	volkInitialize();
	uint32_t sdlExtCount = 0;
	char const* const* sdlExtensions { SDL_Vulkan_GetInstanceExtensions(&sdlExtCount) };
	// add VK_EXT_DEBUG_UTILS_EXTENSION to SDL extensions
	std::vector<const char*> extensions(sdlExtCount + 1);
	for (uint32_t n = 0; n < sdlExtCount; n++) {
		extensions[n] = sdlExtensions[n];
	}
	extensions[sdlExtCount] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;

	printf("SDL Vulkan Extensions:\n");
	for (uint32_t n = 0; n < sdlExtCount; n++) {
		printf(" - %s\n", sdlExtensions[n]);
	}

	printf("======================================================================\n\n\n");

	// setup Vulkan 
	printf("========================Vulkan Setup==================================\n");

	VkResult result;

	// Create vulkan instance
	VkApplicationInfo appInfo {
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName = "How to Vulkan",
		.apiVersion = VK_API_VERSION_1_3
	};

	printf("Additional Vulkan Extensions:\n");
	for(uint32_t n = sdlExtCount; n < extensions.size(); n++) {
		printf(" - %s\n", extensions[n]);
	}
	char const* validationLayers[] = {
	    "VK_LAYER_KHRONOS_validation"
	};
	const uint32_t numValidationLayers = sizeof(validationLayers)/sizeof(char*);
	printf("Loading %u validation layers:\n", numValidationLayers);
	for (uint32_t n = 0; n < numValidationLayers; n++) {
		printf(" - %s\n", validationLayers[n]);
	}
	VkInstanceCreateInfo instanceCreateInfo {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &appInfo,
    	.enabledLayerCount = numValidationLayers,
    	.ppEnabledLayerNames = validationLayers,
		.enabledExtensionCount = (uint32_t)extensions.size(),
		.ppEnabledExtensionNames = extensions.data(),
	};
	CHECK_VK_RESULT(vkCreateInstance(&instanceCreateInfo, nullptr, &state.instance));
	volkLoadInstance(state.instance);

	// setup validation layers
	VkDebugUtilsMessengerCreateInfoEXT debugLayersCreateInfo {
		.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
		.messageSeverity = /*VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT | */VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
		.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
		.pfnUserCallback = onVulkanError,
	};
	CHECK_VK_RESULT(vkCreateDebugUtilsMessengerEXT(state.instance, &debugLayersCreateInfo, nullptr, &state.debugMessenger));

	// create vkDevice
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
	if(!SDL_Vulkan_GetPresentationSupport(state.instance, state.phys_device, queueFamily)) {
		fprintf(stderr, "SDL_Vulkan_GetPresentationSupport() returned false.\n");
		exit(-1);
	}

	// create logical device
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

	// SDL window and surface
	state.window = SDL_CreateWindow("SDL + Vulkan", 900u, 600u, SDL_WINDOW_VULKAN);
	assert(state.window);
	SDL_Vulkan_CreateSurface(state.window, state.instance, nullptr, &state.vulkan_surface);
	SDL_GetWindowSize(state.window, &state.windowSize.x, &state.windowSize.y);

	VkSurfaceCapabilitiesKHR surfaceCaps;
	CHECK_VK_RESULT(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(state.phys_device, state.vulkan_surface, &surfaceCaps));
	VkExtent2D swapchainExtent { surfaceCaps.currentExtent };
	if (surfaceCaps.currentExtent.width == 0xFFFFFFFF) {	// special case if the window is made by Wayland
		swapchainExtent = { .width = static_cast<uint32_t>(state.windowSize.x), .height = static_cast<uint32_t>(state.windowSize.y) };
	}
	printf("Window size: (%d, %d)\n", state.windowSize.x, state.windowSize.y);

	// Swap chain
	const VkFormat imageFormat = VK_FORMAT_B8G8R8A8_SRGB;
	VkSwapchainCreateInfoKHR swapchainCreateInfo {
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = state.vulkan_surface,
		.minImageCount = surfaceCaps.minImageCount,
		.imageFormat = imageFormat,
		.imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR,
		.imageExtent { .width = swapchainExtent.width, .height = swapchainExtent.height },
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode = VK_PRESENT_MODE_FIFO_KHR
	};
	CHECK_VK_RESULT(vkCreateSwapchainKHR(state.device, &swapchainCreateInfo, nullptr, &state.swapchain));

	uint32_t imageCount = 0;
	CHECK_VK_RESULT(vkGetSwapchainImagesKHR(state.device, state.swapchain, &imageCount, nullptr));
	state.swapchainImages.resize(imageCount);
	uint32_t imageViewCount = 0;
	CHECK_VK_RESULT(vkGetSwapchainImagesKHR(state.device, state.swapchain, &imageCount, state.swapchainImages.data()));
	state.swapchainImageViews.resize(imageCount);
	for (uint32_t i = 0; i < imageCount; i++) {
			VkImageViewCreateInfo viewCI {
				.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
				.image = state.swapchainImages[i], 
				.viewType = VK_IMAGE_VIEW_TYPE_2D,
				.format = imageFormat,
				.subresourceRange { 
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.levelCount = 1,
					.layerCount = 1 }
			};
			CHECK_VK_RESULT(vkCreateImageView(state.device, &viewCI, nullptr, &state.swapchainImageViews[i]));
		}

		// depth attachment
	std::vector<VkFormat> depthFormatList{ VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT };
	VkFormat depthFormat = VK_FORMAT_UNDEFINED;
	for (VkFormat& format : depthFormatList) {
	    VkFormatProperties2 formatProperties{ .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2 };
	    vkGetPhysicalDeviceFormatProperties2(state.phys_device, format, &formatProperties);
	    if (formatProperties.formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
	        depthFormat = format;
	        break;
	    }
	}
	VkImageCreateInfo depthImageCreateInfo {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = depthFormat,
		.extent { .width = (uint32_t)state.windowSize.x, .height = (uint32_t)state.windowSize.y, .depth = 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};
	VmaAllocationCreateInfo allocCreateInfo {
		.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
		.usage = VMA_MEMORY_USAGE_AUTO
	};
	CHECK_VK_RESULT(vmaCreateImage(state.allocator, &depthImageCreateInfo, &allocCreateInfo, &state.depthImage, &state.depthImageAllocation, nullptr));

	VkImageViewCreateInfo depthViewCreateInfo { 
	    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
	    .image = state.depthImage,
	    .viewType = VK_IMAGE_VIEW_TYPE_2D,
	    .format = depthFormat,
	    .subresourceRange{ .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, .levelCount = 1, .layerCount = 1 }
	};
	CHECK_VK_RESULT(vkCreateImageView(state.device, &depthViewCreateInfo, nullptr, &state.depthImageView));

	// load mesh
	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	if (!tinyobj::LoadObj(&attrib, &shapes, &materials, nullptr, nullptr, "assets/suzanne.obj")) {
		printf("Failed to load .obj file!\n");
		exit(-1);
	}

	state.indexCount = shapes[0].mesh.indices.size();
	std::vector<Vertex> vertices{};
	std::vector<uint16_t> indices{};
	// Load vertex and index data
	for (auto& index : shapes[0].mesh.indices) {
	    Vertex v{
	        .pos = { 
				 attrib.vertices[static_cast<size_t>(index.vertex_index * 3)], 
				-attrib.vertices[static_cast<size_t>(index.vertex_index * 3 + 1)], 
				 attrib.vertices[static_cast<size_t>(index.vertex_index * 3 + 2)] },
	        .normal = { 
				 attrib.normals[static_cast<size_t>(index.normal_index * 3)], 
				-attrib.normals[static_cast<size_t>(index.normal_index * 3 + 1)], 
				 attrib.normals[static_cast<size_t>(index.normal_index * 3 + 2)] },
	        .uv = { 
					  attrib.texcoords[static_cast<size_t>(index.texcoord_index * 2)], 
				1.0 - attrib.texcoords[static_cast<size_t>(index.texcoord_index * 2 + 1)] }
	    };
	    vertices.push_back(v);
	    indices.push_back(static_cast<uint16_t>(indices.size()));
	}

	#if 0
	printf("=======\n");
	for (uint32_t i = 0; i < vertices.size(); i++) {
		const Vertex& v = vertices[i];
		printf("vertex[%5u]: pos  = < %4.1f, %4.1f, %4.1f >\n", i, v.pos.x, v.pos.y, v.pos.z);
		printf("             : norm = < %4.1f, %4.1f, %4.1f >\n", v.normal.x, v.normal.y, v.normal.z);
		printf("             : uv   = < %4.1f, %4.1f >\n", v.uv.x, v.uv.y);
	}
	for (uint32_t i = 0; i < indices.size(); i++) {
		printf("index[%5u]: < %u >\n", i, indices[i]);
	}
	printf("=======\n");
	#endif

	state.vBufSize = sizeof(Vertex) * vertices.size();
	state.iBufSize = sizeof(uint16_t) * indices.size();
	VkBufferCreateInfo bufferCI{
	    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
	    .size = state.vBufSize + state.iBufSize,
	    .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT
	};

	VmaAllocationCreateInfo vBufferAllocCI{
	    .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
	    .usage = VMA_MEMORY_USAGE_AUTO
	};
	VmaAllocationInfo vBufferAllocInfo{};
	CHECK_VK_RESULT(vmaCreateBuffer(state.allocator, &bufferCI, &vBufferAllocCI, &state.vBuffer, &state.vBufferAllocation, &vBufferAllocInfo));

	memcpy(vBufferAllocInfo.pMappedData, vertices.data(), state.vBufSize);
	memcpy(((char*)vBufferAllocInfo.pMappedData) + state.vBufSize, indices.data(), state.iBufSize);

		// shader data buffers
	for (uint32_t i = 0; i < maxFramesInFlight; i++) {
	    VkBufferCreateInfo uBufferCI{
	        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
	        .size = sizeof(ShaderData),
	        .usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
	    };
	    VmaAllocationCreateInfo uBufferAllocCI{
	        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
	        .usage = VMA_MEMORY_USAGE_AUTO
    	};
    	CHECK_VK_RESULT(vmaCreateBuffer(state.allocator, &uBufferCI, &uBufferAllocCI, &state.shaderDataBuffers[i].buffer, &state.shaderDataBuffers[i].allocation, &state.shaderDataBuffers[i].allocationInfo));

	    VkBufferDeviceAddressInfo uBufferBdaInfo{
	        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
	        .buffer = state.shaderDataBuffers[i].buffer
	    };
	    state.shaderDataBuffers[i].deviceAddress = vkGetBufferDeviceAddress(state.device, &uBufferBdaInfo);
	}

		// sync objects
	VkSemaphoreCreateInfo semaphoreCI{
	    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
	};
	VkFenceCreateInfo fenceCI{
	    .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
	    .flags = VK_FENCE_CREATE_SIGNALED_BIT
	};
	for (uint32_t i = 0; i < maxFramesInFlight; i++) {
	    CHECK_VK_RESULT(vkCreateFence(state.device, &fenceCI, nullptr, &state.fences[i]));
	    CHECK_VK_RESULT(vkCreateSemaphore(state.device, &semaphoreCI, nullptr, &state.imageAcquiredSemaphores[i]));
	}
	state.renderCompleteSemaphores.resize(state.swapchainImages.size());
	for (auto& semaphore : state.renderCompleteSemaphores) {
	    CHECK_VK_RESULT(vkCreateSemaphore(state.device, &semaphoreCI, nullptr, &semaphore));
	}

		// Command pool
	VkCommandPoolCreateInfo commandPoolCI{
	    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
	    .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
	    .queueFamilyIndex = queueFamily
	};
	CHECK_VK_RESULT(vkCreateCommandPool(state.device, &commandPoolCI, nullptr, &state.commandPool));

	VkCommandBufferAllocateInfo cbAllocCI{
	    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
	    .commandPool = state.commandPool,
	    .commandBufferCount = maxFramesInFlight
	};
	CHECK_VK_RESULT(vkAllocateCommandBuffers(state.device, &cbAllocCI, state.commandBuffers.data()));

	// load textures
	std::vector<VkDescriptorImageInfo> textureDescriptors{};
	for (uint32_t i = 0; i < state.textures.size(); i++) {
	    ktxTexture* ktxTexture{ nullptr };
	    std::string filename = "assets/suzanne" + std::to_string(i) + ".ktx";
	    ktxTexture_CreateFromNamedFile(filename.c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktxTexture);

		VkImageCreateInfo texImgCI{
		    .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		    .imageType = VK_IMAGE_TYPE_2D,
		    .format = ktxTexture_GetVkFormat(ktxTexture),
		    .extent = {.width = ktxTexture->baseWidth, .height = ktxTexture->baseHeight, .depth = 1 },
		    .mipLevels = ktxTexture->numLevels,
		    .arrayLayers = 1,
		    .samples = VK_SAMPLE_COUNT_1_BIT,
		    .tiling = VK_IMAGE_TILING_OPTIMAL,
		    .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
		};
		VmaAllocationCreateInfo texImageAllocCI{ .usage = VMA_MEMORY_USAGE_AUTO };
		CHECK_VK_RESULT(vmaCreateImage(state.allocator, &texImgCI, &texImageAllocCI, &state.textures[i].image, &state.textures[i].allocation, nullptr));
		
		VkImageViewCreateInfo texViewCI{
		    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		    .image = state.textures[i].image,
		    .viewType = VK_IMAGE_VIEW_TYPE_2D,
		    .format = texImgCI.format,
		    .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = ktxTexture->numLevels, .layerCount = 1 }
		};
		CHECK_VK_RESULT(vkCreateImageView(state.device, &texViewCI, nullptr, &state.textures[i].view));

			// upload textures
		VkBuffer imgSrcBuffer{};
		VmaAllocation imgSrcAllocation{};
		VkBufferCreateInfo imgSrcBufferCI{
		    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		    .size = (uint32_t)ktxTexture->dataSize,
		    .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT
		};
		VmaAllocationCreateInfo imgSrcAllocCI{
		    .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
		    .usage = VMA_MEMORY_USAGE_AUTO
		};
		VmaAllocationInfo imgSrcAllocInfo;
		CHECK_VK_RESULT(vmaCreateBuffer(state.allocator, &imgSrcBufferCI, &imgSrcAllocCI, &imgSrcBuffer, &imgSrcAllocation, &imgSrcAllocInfo));
		memcpy(imgSrcAllocInfo.pMappedData, ktxTexture->pData, ktxTexture->dataSize);

		VkFenceCreateInfo fenceOneTimeCI {
			.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO
		};
		VkFence fenceOneTime{};
		CHECK_VK_RESULT(vkCreateFence(state.device, &fenceOneTimeCI, nullptr, &fenceOneTime));
		VkCommandBuffer cbOneTime{};
		VkCommandBufferAllocateInfo cbOneTimeAI{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = state.commandPool,
			.commandBufferCount = 1
		};
		CHECK_VK_RESULT(vkAllocateCommandBuffers(state.device, &cbOneTimeAI, &cbOneTime));

		VkCommandBufferBeginInfo cbOneTimeBI {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
		};
		CHECK_VK_RESULT(vkBeginCommandBuffer(cbOneTime, &cbOneTimeBI));
		VkImageMemoryBarrier2 barrierTexImage {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
			.srcStageMask = VK_PIPELINE_STAGE_2_NONE,
			.srcAccessMask = VK_ACCESS_2_NONE,
			.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.image = state.textures[i].image,
			.subresourceRange = {
   				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
   				.levelCount = ktxTexture->numLevels,
   				.layerCount = 1 }
		};
		VkDependencyInfo barrierTexInfo {
			.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
			.imageMemoryBarrierCount = 1,
			.pImageMemoryBarriers = &barrierTexImage
		};
		vkCmdPipelineBarrier2(cbOneTime, &barrierTexInfo);
		std::vector<VkBufferImageCopy> copyRegions{};
		for (uint32_t j = 0; j < ktxTexture->numLevels; j++) {
			ktx_size_t mipOffset{0};
			KTX_error_code ret = ktxTexture_GetImageOffset(ktxTexture, j, 0, 0, &mipOffset);
			copyRegions.push_back({
				.bufferOffset = mipOffset,
				.imageSubresource {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = (uint32_t)j, .layerCount = 1},
				.imageExtent {.width = ktxTexture->baseWidth >> j, .height = ktxTexture->baseHeight >> j, .depth = 1}
			});
		}
		vkCmdCopyBufferToImage(cbOneTime, 
							   imgSrcBuffer, 
							   state.textures[i].image, 
							   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
							   static_cast<uint32_t>(copyRegions.size()), 
							   copyRegions.data());
		VkImageMemoryBarrier2 barrierTexRead {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
			.srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT,
			.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
			.image = state.textures[i].image,
			.subresourceRange = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = ktxTexture->numLevels, .layerCount = 1 }
		};
		barrierTexInfo.pImageMemoryBarriers = &barrierTexRead;
		vkCmdPipelineBarrier2(cbOneTime, &barrierTexInfo);
		CHECK_VK_RESULT(vkEndCommandBuffer(cbOneTime));
		VkCommandBufferSubmitInfo cbOneTimeSubmitInfo {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
			.commandBuffer = cbOneTime
		};
		VkSubmitInfo2 oneTimeSI {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
			.commandBufferInfoCount = 1,
			.pCommandBufferInfos = &cbOneTimeSubmitInfo
		};
		CHECK_VK_RESULT(vkQueueSubmit2(state.queue, 1, &oneTimeSI, fenceOneTime));
		CHECK_VK_RESULT(vkWaitForFences(state.device, 1, &fenceOneTime, VK_TRUE, UINT64_MAX));
		vkDestroyFence(state.device, fenceOneTime, nullptr);
		vmaDestroyBuffer(state.allocator, imgSrcBuffer, imgSrcAllocation);

		// create texture sampler
		VkSamplerCreateInfo samplerCI {
			.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
			.magFilter = VK_FILTER_LINEAR,
			.minFilter = VK_FILTER_LINEAR,
			.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
			.anisotropyEnable = VK_TRUE,
			.maxAnisotropy = 8.0f,
			.maxLod = (float)ktxTexture->numLevels,
		};
		CHECK_VK_RESULT(vkCreateSampler(state.device, &samplerCI, nullptr, &state.textures[i].sampler));

		ktxTexture_Destroy(ktxTexture);
		textureDescriptors.push_back({
			.sampler = state.textures[i].sampler,
			.imageView = state.textures[i].view,
			.imageLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL
		});
	}

	VkDescriptorBindingFlags descVariableFlag {VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT };
	VkDescriptorSetLayoutBindingFlagsCreateInfo descBindingFlags {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
		.bindingCount = 1,
		.pBindingFlags = &descVariableFlag
	};
	VkDescriptorSetLayoutBinding descLayoutBindingTex {
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.descriptorCount = static_cast<uint32_t>(state.textures.size()),
		.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
	};
	VkDescriptorSetLayoutCreateInfo descLayoutTexCI {
		.sType  = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.pNext = &descBindingFlags,
		.bindingCount = 1,
		.pBindings = &descLayoutBindingTex
	};
	CHECK_VK_RESULT(vkCreateDescriptorSetLayout(state.device, &descLayoutTexCI, nullptr, &state.descriptorSetLayoutTex));

	VkDescriptorPoolSize poolSize {
		.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.descriptorCount = static_cast<uint32_t>(state.textures.size())
	};
	VkDescriptorPoolCreateInfo descPoolCI {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = 1,
		.poolSizeCount = 1,
		.pPoolSizes = &poolSize
	};
	CHECK_VK_RESULT(vkCreateDescriptorPool(state.device, &descPoolCI, nullptr, &state.descriptorPool));

	uint32_t variableDescCount {static_cast<uint32_t>(state.textures.size()) };
	VkDescriptorSetVariableDescriptorCountAllocateInfo variableDescCountAI {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO_EXT,
		.descriptorSetCount = 1,
		.pDescriptorCounts = &variableDescCount
	};
	VkDescriptorSetAllocateInfo texDescSetAlloc {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.pNext = &variableDescCountAI,
		.descriptorPool = state.descriptorPool,
		.descriptorSetCount = 1,
		.pSetLayouts = &state.descriptorSetLayoutTex
	};
	CHECK_VK_RESULT(vkAllocateDescriptorSets(state.device, &texDescSetAlloc, &state.descriptorSetTex));

	VkWriteDescriptorSet writeDescSet{
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = state.descriptorSetTex,
		.dstBinding = 0,
		.descriptorCount = static_cast<uint32_t>(textureDescriptors.size()),
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.pImageInfo = textureDescriptors.data()
	};
	vkUpdateDescriptorSets(state.device, 1, &writeDescSet, 0, nullptr);

	// load shaders
	ShaderSpirvSource shaderSrc;
	if(!load_spirv_from_file("bin/shader.spv", shaderSrc)) {
		fprintf(stderr, "Failed to load shader source %s!\n", "shader.spv");
		exit(-1);
	}

	VkShaderModuleCreateInfo shaderModuleCI {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = shaderSrc.codeSize,
		.pCode = shaderSrc.pCode
	};
	CHECK_VK_RESULT(vkCreateShaderModule(state.device, &shaderModuleCI, nullptr, &state.shaderModule));
		
	// create graphics pipeline
	VkPushConstantRange pushConstantRange {
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
		.size = sizeof(VkDeviceAddress)
	};
	VkPipelineLayoutCreateInfo pipelineLayoutCI {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &state.descriptorSetLayoutTex,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &pushConstantRange
	};
	CHECK_VK_RESULT(vkCreatePipelineLayout(state.device, &pipelineLayoutCI, nullptr, &state.pipelineLayout));

	VkVertexInputBindingDescription vertexBinding {
		.binding = 0,
		.stride = sizeof(Vertex),
		.inputRate = VK_VERTEX_INPUT_RATE_VERTEX
	};

	std::vector<VkVertexInputAttributeDescription> vertexAttributes {
		{ .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT },
		{ .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, normal) },
		{ .location = 2, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(Vertex, uv) },
	};

	VkPipelineVertexInputStateCreateInfo vertexInputState {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = &vertexBinding,
		.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributes.size()),
		.pVertexAttributeDescriptions = vertexAttributes.data(),
	};
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyState {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
	};
	std::vector<VkPipelineShaderStageCreateInfo> shaderStages {
		{ .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		  .stage = VK_SHADER_STAGE_VERTEX_BIT,
		  .module = state.shaderModule, .pName = "main" },
		{ .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		  .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
		  .module = state.shaderModule, .pName = "main" },
	};

	VkPipelineViewportStateCreateInfo viewportState {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.scissorCount = 1
	};
	std::array<VkDynamicState, 2> dynamicStates { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicState {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = 2,
		.pDynamicStates = dynamicStates.data()
	};
	VkPipelineDepthStencilStateCreateInfo depthStencilState {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = VK_TRUE,
		.depthWriteEnable = VK_TRUE,
		.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL
	};
	VkPipelineRenderingCreateInfo renderingCI {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
		.colorAttachmentCount = 1,
		.pColorAttachmentFormats = &imageFormat,
		.depthAttachmentFormat = depthFormat
	};

	VkPipelineColorBlendAttachmentState blendAttachment {
		.colorWriteMask = 0xF
	};
	VkPipelineColorBlendStateCreateInfo colorBlendState {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &blendAttachment
	};
	VkPipelineRasterizationStateCreateInfo rasterizationState {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.lineWidth = 1.0f
	};
	VkPipelineMultisampleStateCreateInfo multisampleState {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
	};

	VkGraphicsPipelineCreateInfo pipelineCI {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.pNext = &renderingCI,
		.stageCount = 2,
		.pStages = shaderStages.data(),
		.pVertexInputState = &vertexInputState,
		.pInputAssemblyState = &inputAssemblyState,
		.pViewportState = &viewportState,
		.pRasterizationState = &rasterizationState,
		.pMultisampleState = &multisampleState,
		.pDepthStencilState = &depthStencilState,
		.pColorBlendState = &colorBlendState,
		.pDynamicState = &dynamicState,
		.layout = state.pipelineLayout
	};
	CHECK_VK_RESULT(vkCreateGraphicsPipelines(state.device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &state.pipeline));

	printf("======================================================================\n\n\n");

	// start main loop
	printf("====================Start main event loop...==========================\n");

	glm::vec3 objectRotations[3]{};
	glm::vec3 camPos{0.0f, 0.0f, -6.0f};
	ShaderData shaderData {};

	printf("  running...\n");
	uint64_t lastTime = get_time_ms();
	uint64_t startTime = get_time_ms();
	float elapsedTime = 0.0f;
	while (!state.done) {
		VkResult result;

		// Wait on fence
		CHECK_VK_RESULT(vkWaitForFences(state.device, 1, &state.fences[state.frameIndex], true, UINT64_MAX));
		CHECK_VK_RESULT(vkResetFences(state.device, 1, &state.fences[state.frameIndex]));

		// Acquire next image
		result = vkAcquireNextImageKHR(state.device, state.swapchain, UINT64_MAX, state.imageAcquiredSemaphores[state.frameIndex], VK_NULL_HANDLE, &state.imageIndex);
		if (result < VK_SUCCESS) {
			if (result == VK_ERROR_OUT_OF_DATE_KHR) {
				state.updateSwapchain = true;
			} else {
				printf("Error executing %s: %i\n", "vkAcquireNextImageKHR", result);
			}
		}

		// Update shader data
		objectRotations[1].x += 0.5f * elapsedTime;
		objectRotations[1].y += 1.0f * elapsedTime;
		objectRotations[1].z += 1.5f * elapsedTime;
		shaderData.projection = glm::perspective(glm::radians(45.0f), (float)state.windowSize.x / (float)state.windowSize.y, 0.1f, 32.0f);
		shaderData.view = glm::translate(glm::mat4(1.0f), camPos);
		for (int i = 0; i < 3; i++) {
			glm::vec3 instancePos = glm::vec3((float)(i-1) * 3.0f, 0.0f, 0.0f);
			shaderData.model[i] = glm::translate(glm::mat4(1.0f), instancePos) * glm::mat4_cast(glm::quat(objectRotations[i]));
		}
		memcpy(state.shaderDataBuffers[state.frameIndex].allocationInfo.pMappedData, &shaderData, sizeof(ShaderData));

		#if 0
		std::cout << "WindowSize: (" << state.windowSize.x << ", " << state.windowSize.y << ")\n";
		std::cout << "Projection matrix: " << glm::to_string(shaderData.projection) << std::endl;
		std::cout << "view matrix:       " << glm::to_string(shaderData.view) << std::endl;
		std::cout << "model[0]:          " << glm::to_string(shaderData.model[0]) << std::endl;
		std::cout << "  objectRotations[0]:" << glm::to_string(objectRotations[0]) << std::endl;
		std::cout << "model[1]:          " << glm::to_string(shaderData.model[1]) << std::endl;
		std::cout << "  objectRotations[1]:" << glm::to_string(objectRotations[1]) << std::endl;
		std::cout << "model[2]:          " << glm::to_string(shaderData.model[2]) << std::endl;
		std::cout << "  objectRotations[2]:" << glm::to_string(objectRotations[2]) << std::endl;
		#endif

		// Record command buffer
		auto cb = state.commandBuffers[state.frameIndex];
		CHECK_VK_RESULT(vkResetCommandBuffer(cb, 0));

		VkCommandBufferBeginInfo cbBI {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
		};
		CHECK_VK_RESULT(vkBeginCommandBuffer(cb, &cbBI));
		
		std::array<VkImageMemoryBarrier2, 2> outputBarriers {
			VkImageMemoryBarrier2 {
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
				.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
				.srcAccessMask = 0,
				.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
				.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
				.image = state.swapchainImages[state.imageIndex],
				.subresourceRange { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 }
			},
			VkImageMemoryBarrier2 {
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
				.srcStageMask = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
				.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
				.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
				.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
				.image = state.depthImage,
				.subresourceRange { .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, .levelCount = 1, .layerCount = 1 }
			},
		};
		VkDependencyInfo barrierDependencyInfo {
			.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
			.imageMemoryBarrierCount = 2,
			.pImageMemoryBarriers = outputBarriers.data()
		};
		vkCmdPipelineBarrier2(cb, &barrierDependencyInfo);

		VkRenderingAttachmentInfo colorAttachmentInfo {
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView = state.swapchainImageViews[state.imageIndex],
			.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.clearValue = { .color { {0.0f, 0.0f, 0.2f, 1.0f} } }
		};
		VkRenderingAttachmentInfo depthAttachmentInfo {
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView = state.depthImageView, 
			.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.clearValue = { .depthStencil = {1.0f, 0} }
		};

		VkRenderingInfo renderingInfo {
			.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
			.renderArea { .extent {.width = static_cast<uint32_t>(state.windowSize.x), .height = static_cast<uint32_t>(state.windowSize.y) } },
			.layerCount = 1,
			.colorAttachmentCount = 1,
			.pColorAttachments = &colorAttachmentInfo,
			.pDepthAttachment = &depthAttachmentInfo
		};
		vkCmdBeginRendering(cb, &renderingInfo);

		VkViewport vp {
			.width =static_cast<float>(state.windowSize.x),
			.height = static_cast<float>(state.windowSize.y),
			.minDepth = 0.0f,
			.maxDepth = 1.0f
		};
		vkCmdSetViewport(cb, 0, 1, &vp);
		VkRect2D scissor { .extent { .width = static_cast<uint32_t>(state.windowSize.x), .height = static_cast<uint32_t>(state.windowSize.y) } };
		vkCmdSetScissor(cb, 0, 1, &scissor);

		vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, state.pipeline);
		VkDeviceSize vOffset {0};
		vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, state.pipelineLayout, 0, 1, &state.descriptorSetTex, 0, nullptr);
		vkCmdBindVertexBuffers(cb, 0, 1, &state.vBuffer, &vOffset);
		vkCmdBindIndexBuffer(cb, state.vBuffer, state.vBufSize, VK_INDEX_TYPE_UINT16);

		vkCmdPushConstants(cb, state.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(VkDeviceAddress), &state.shaderDataBuffers[state.frameIndex].deviceAddress);

		vkCmdDrawIndexed(cb, static_cast<uint32_t>(state.indexCount), 3, 0, 0, 0);

		vkCmdEndRendering(cb);

		VkImageMemoryBarrier2 barrierPresent {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
			.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			.dstAccessMask = 0,
			.oldLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			.image = state.swapchainImages[state.imageIndex],
			.subresourceRange { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 }
		};
		VkDependencyInfo barrierPresentDependencyInfo {
			.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
			.imageMemoryBarrierCount = 1,
			.pImageMemoryBarriers = &barrierPresent
		};
		vkCmdPipelineBarrier2(cb, &barrierPresentDependencyInfo);

		vkEndCommandBuffer(cb);

		// Submit command buffer
		VkSemaphoreSubmitInfo waitSemaphoreInfo {
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
			.semaphore = state.imageAcquiredSemaphores[state.frameIndex],
			.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
		};
		VkCommandBufferSubmitInfo commandBufferSubmitInfo {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
			.commandBuffer = cb
		};
		VkSemaphoreSubmitInfo signalSemaphoreInfo {
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
			.semaphore = state.renderCompleteSemaphores[state.imageIndex],
			.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
		};
		VkSubmitInfo2 submitInfo {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
			.waitSemaphoreInfoCount = 1,
			.pWaitSemaphoreInfos = &waitSemaphoreInfo,
			.commandBufferInfoCount = 1,
			.pCommandBufferInfos = &commandBufferSubmitInfo,
			.signalSemaphoreInfoCount = 1,
			.pSignalSemaphoreInfos = &signalSemaphoreInfo
		};
		CHECK_VK_RESULT(vkQueueSubmit2(state.queue, 1, &submitInfo, state.fences[state.frameIndex]));

		state.frameIndex = (state.frameIndex + 1) % maxFramesInFlight;

		// Present image
		VkPresentInfoKHR presentInfo {
			.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &state.renderCompleteSemaphores[state.imageIndex],
			.swapchainCount = 1,
			.pSwapchains = &state.swapchain,
			.pImageIndices = &state.imageIndex
		};	
		result = vkQueuePresentKHR(state.queue, &presentInfo); 
		if (result < VK_SUCCESS) {
			if (result == VK_ERROR_OUT_OF_DATE_KHR) {
				state.updateSwapchain = true;
			} else {
				printf("Error executing %s: %i\n", "vkAcquireNextImageKHR", result);
			}
		}

		// Poll events
		uint64_t now = get_time_ms();
		elapsedTime = static_cast<float>(now - lastTime) / 1000.0f;
		lastTime = now;
		float totalTime = static_cast<float>(get_time_ms() - startTime) / 1000.0f;
		float fps = 1.0f / elapsedTime;
		char titleStr[256];
		snprintf(titleStr, 256, "fps: %.1f", fps);
		SDL_SetWindowTitle(state.window, titleStr);
		SDL_Event e;
		while(SDL_PollEvent(&e)) {}

		if (totalTime > 5.0f) {
			state.done = true;
		}
	}
	printf("  done.\n");
	printf("======================================================================\n\n\n");

	// Cleanup
	printf("Cleaning up resources.\n");
	CHECK_VK_RESULT(vkDeviceWaitIdle(state.device));
	for (uint32_t i = 0; i < maxFramesInFlight; i++) {
		vkDestroyFence(state.device, state.fences[i], nullptr);
		vkDestroySemaphore(state.device, state.imageAcquiredSemaphores[i], nullptr);
		vmaDestroyBuffer(state.allocator, state.shaderDataBuffers[i].buffer, state.shaderDataBuffers[i].allocation);
	}
	for (uint32_t i = 0; i < state.renderCompleteSemaphores.size(); i++) {
		vkDestroySemaphore(state.device, state.renderCompleteSemaphores[i], nullptr);
	}
	vmaDestroyImage(state.allocator, state.depthImage, state.depthImageAllocation);
	vkDestroyImageView(state.device, state.depthImageView, nullptr);
	for(uint32_t i = 0; i < state.swapchainImageViews.size(); i++) {
		vkDestroyImageView(state.device, state.swapchainImageViews[i], nullptr);
	}
	vmaDestroyBuffer(state.allocator, state.vBuffer, state.vBufferAllocation);
	for (uint32_t i = 0; i < state.textures.size(); i++) {
		vkDestroyImageView(state.device, state.textures[i].view, nullptr);
		vkDestroySampler(state.device, state.textures[i].sampler, nullptr);
		vmaDestroyImage(state.allocator, state.textures[i].image, state.textures[i].allocation);
	}
	vkDestroyDescriptorSetLayout(state.device, state.descriptorSetLayoutTex, nullptr);
	vkDestroyDescriptorPool(state.device, state.descriptorPool, nullptr);
	vkDestroyPipelineLayout(state.device, state.pipelineLayout, nullptr);
	vkDestroyPipeline(state.device, state.pipeline, nullptr);
	vkDestroySwapchainKHR(state.device, state.swapchain, nullptr);
	vkDestroySurfaceKHR(state.instance, state.vulkan_surface, nullptr);
	vkDestroyCommandPool(state.device, state.commandPool, nullptr);
	vkDestroyShaderModule(state.device, state.shaderModule, nullptr);
	vmaDestroyAllocator(state.allocator);

	vkDestroyDevice(state.device, nullptr);
	vkDestroyDebugUtilsMessengerEXT(state.instance, state.debugMessenger, nullptr);
	vkDestroyInstance(state.instance, nullptr);

	return 0;
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

// helper function to load a spirv blob from file
bool load_spirv_from_file(const char* filename, ShaderSpirvSource& shaderSrc) {
	FILE* fp = fopen(filename, "rb");
	if (!fp) {
		return false;
	}

	fseek(fp, 0, SEEK_END);
	size_t size = static_cast<size_t>(ftell(fp));
	fseek(fp, 0, SEEK_SET);

	char* buffer = (char*)malloc(size);
	if (!buffer) {
		fclose(fp);
		return false;
	}

	size_t read_bytes = fread(buffer, 1, size, fp);
	if (read_bytes != size) {
		fprintf(stderr, "Failed to read entire file: %s\n", filename);
		free(buffer);
		fclose(fp);
		return false;
	}

	fclose(fp);

	shaderSrc.pCode = (const uint32_t*)buffer;
	shaderSrc.codeSize = size;
	return true;
}

void free_spirv_code(ShaderSpirvSource& shaderSrc) {
	if (shaderSrc.pCode) {
		free((void*)shaderSrc.pCode);
		shaderSrc.pCode = nullptr;
	}
	shaderSrc.codeSize = 0;
}
