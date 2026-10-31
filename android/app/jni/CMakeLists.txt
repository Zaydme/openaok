cmake_minimum_required(VERSION 3.22)
project(OpenAoKAndroid C CXX)

# The engine's own sources, unchanged, built as libmain.so -- the library
# SDL's Java side (SDLActivity) loads and whose SDL_main it runs -- against
# SDL2 and FreeType built here from source (android/deps, fetched by
# android/fetch_deps.sh).
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(REPO ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
set(DEPS ${CMAKE_CURRENT_SOURCE_DIR}/../../deps)
set(SDL_DIR ${DEPS}/SDL2-2.32.10)
set(FREETYPE_DIR ${DEPS}/freetype-VER-2-13-3)

# SDL2 as a shared library, which SDLActivity loads before libmain.
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory(${SDL_DIR} SDL2 EXCLUDE_FROM_ALL)

# FreeType, static, with none of its optional libraries: the game only reads
# the CD's TrueType fonts.
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(${FREETYPE_DIR} freetype EXCLUDE_FROM_ALL)

# The engine includes <SDL2/SDL.h>, as it finds SDL on a desktop.
file(GLOB SDL_HEADERS ${SDL_DIR}/include/*.h)
file(COPY ${SDL_HEADERS} DESTINATION ${CMAKE_BINARY_DIR}/sdl-include/SDL2)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS ${REPO}/src/*.cpp)
add_library(main SHARED ${SOURCES})
target_include_directories(main PRIVATE ${REPO}/src)
target_include_directories(main SYSTEM PRIVATE ${REPO}/third_party ${CMAKE_BINARY_DIR}/sdl-include)
# The desktop build's warnings, but not as errors: this compiler is not the
# one the code is kept warning-clean against.
target_compile_options(main PRIVATE -O3 -Wall -Wextra -Wno-missing-designated-field-initializers)
target_link_libraries(main PRIVATE SDL2 freetype android log m)
# Link-time optimisation, as the desktop build has, where the NDK's toolchain
# supports it.
include(CheckIPOSupported)
check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_ERROR)
if(IPO_SUPPORTED)
    set_property(TARGET main PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
else()
    message(STATUS "LTO disabled: ${IPO_ERROR}")
endif()
