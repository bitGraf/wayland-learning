cmake_minimum_required(VERSION 3.31)

list(APPEND CMAKE_MESSAGE_INDENT "  ")

project(wayland_proj VERSION 1.0.0 LANGUAGES C CXX)

message("Config generated with: ${CMAKE_GENERATOR}")
message("Project built with C++ compiler: ${CMAKE_CXX_COMPILER}")
message("Project built with C compiler: ${CMAKE_C_COMPILER}")

# find dependencies
list(APPEND CMAKE_PREFIX_PATH "~/dev/vulkan-libs/")
#message(STATUS ${CMAKE_MODULE_PATH})
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# set(CMAKE_CXX_EXTENSIONS ON)

set(OUTPUT_DIR "${CMAKE_BINARY_DIR}/../bin")
cmake_path(NORMAL_PATH OUTPUT_DIR)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${OUTPUT_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${OUTPUT_DIR})
message("Build files going into: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

add_subdirectory(app)
