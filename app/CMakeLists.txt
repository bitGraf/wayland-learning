###############################################################################
# Create entry application -> executable app.exe
###############################################################################
add_executable(app)

#file(GLOB_RECURSE app_SRC CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE app_SRC CONFIGURE_DEPENDS 
	"src/*.cpp"
	"src/*.c")
file(GLOB_RECURSE app_INC CONFIGURE_DEPENDS "include/*.h")

# print out source and header files
message(STATUS "Source files:")
foreach(FILE_PATH IN LISTS app_SRC)
	file(RELATIVE_PATH STRIPPED_PATH "${CMAKE_CURRENT_SOURCE_DIR}" "${FILE_PATH}")
	message(STATUS "    ${STRIPPED_PATH}")
endforeach()
message(STATUS "Header files:")
foreach(FILE_PATH IN LISTS app_INC)
	file(RELATIVE_PATH STRIPPED_PATH "${CMAKE_CURRENT_SOURCE_DIR}" "${FILE_PATH}")
	message(STATUS "    ${STRIPPED_PATH}")
endforeach()

target_sources(app
	PRIVATE 
		FILE_SET SOURCES
		BASE_DIRS src/
		FILES ${app_SRC}
	PUBLIC  
		FILE_SET HEADERS 
		BASE_DIRS include
		FILES ${app_INC})

target_link_libraries(app PUBLIC wayland-client vulkan GPUOpen::VulkanMemoryAllocator glm::glm tinyobjloader::tinyobjloader)
target_compile_options(app PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wno-unused-parameter -Wno-unused-variable -Wno-attributes -Wno-missing-designated-field-initializers)
target_compile_definitions(app PRIVATE _POSIX_C_SOURCE=200112L)
