cmake_minimum_required(VERSION 3.16)

project(remotedeck VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# libchiaki (motor del protocolo Remote Play) desde el submódulo de chiaki-ng.
# Solo compilamos la librería: nada de GUI Qt, CLI ni tests.
# ---------------------------------------------------------------------------
set(CHIAKI_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(CHIAKI_ENABLE_CLI OFF CACHE BOOL "" FORCE)
set(CHIAKI_ENABLE_GUI OFF CACHE BOOL "" FORCE)
set(CHIAKI_ENABLE_BOREALIS OFF CACHE BOOL "" FORCE)
set(CHIAKI_ENABLE_STEAM_SHORTCUT OFF CACHE BOOL "" FORCE)
set(CHIAKI_ENABLE_STEAMDECK_NATIVE OFF CACHE STRING "" FORCE)
set(CHIAKI_ENABLE_SETSU OFF CACHE STRING "" FORCE)
set(CHIAKI_ENABLE_FFMPEG_DECODER ON CACHE STRING "" FORCE)
set(CHIAKI_ENABLE_PI_DECODER OFF CACHE STRING "" FORCE)
set(CHIAKI_LIB_ENABLE_OPUS ON CACHE BOOL "" FORCE)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third-party/chiaki-ng/CMakeLists.txt")
	message(FATAL_ERROR "Falta el submódulo chiaki-ng. Ejecuta: git submodule update --init --recursive")
endif()
add_subdirectory(third-party/chiaki-ng EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# Dependencias del frontend
# ---------------------------------------------------------------------------
find_package(SDL2 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(RD_FFMPEG REQUIRED IMPORTED_TARGET libavcodec libavutil)

add_executable(remotedeck
	src/main.c
	src/config.c
	src/discover.c
	src/regist.c
	src/stream.c
	src/input.c)

target_include_directories(remotedeck PRIVATE src)
target_link_libraries(remotedeck PRIVATE chiaki-lib PkgConfig::RD_FFMPEG)
if(TARGET SDL2::SDL2)
	target_link_libraries(remotedeck PRIVATE SDL2::SDL2)
else()
	target_include_directories(remotedeck PRIVATE ${SDL2_INCLUDE_DIRS})
	target_link_libraries(remotedeck PRIVATE ${SDL2_LIBRARIES})
endif()
target_compile_definitions(remotedeck PRIVATE RD_VERSION="${PROJECT_VERSION}")

install(TARGETS remotedeck RUNTIME DESTINATION bin)
