###############################################################################
# Create entry application -> executable app.exe
###############################################################################
add_executable(app)

file(GLOB_RECURSE app_SRC CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
file(GLOB_RECURSE app_INC CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h")

target_sources(app
	PRIVATE 
		FILE_SET SOURCES
		BASE_DIRS src/
		FILES ${app_SRC}
	PUBLIC  
		FILE_SET HEADERS 
		BASE_DIRS include
		FILES ${app_INC})

target_link_libraries(app PUBLIC wayland-client) # wayland-cursor)
target_compile_options(app PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wno-unused-parameter)
