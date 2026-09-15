###############################################################################
# Create entry application -> executable app.exe
###############################################################################
add_executable(app)

#file(GLOB_RECURSE app_SRC CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE app_SRC CONFIGURE_DEPENDS 
	"src/*.cpp"
	"src/*.c")
file(GLOB_RECURSE app_INC CONFIGURE_DEPENDS "include/*.h")

message(STATUS "Source files: ${app_SRC}")
message(STATUS "Header files: ${app_INC}")

target_sources(app
	PRIVATE 
		FILE_SET SOURCES
		BASE_DIRS src/
		FILES ${app_SRC}
	PUBLIC  
		FILE_SET HEADERS 
		BASE_DIRS include
		FILES ${app_INC})

target_link_libraries(app PUBLIC wayland-client)# wayland-cursor)
target_compile_options(app PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wno-unused-parameter -Wno-unused-variable -Wno-attributes)
target_compile_definitions(app PRIVATE _POSIX_C_SOURCE=200112L)
