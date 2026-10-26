cmake_minimum_required(VERSION 3.20)
project(OpenAoK VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
endif()

# Homebrew installs outside the default search roots on Apple Silicon
# (/opt/homebrew) and is not searched at all by Apple's own CMake, so make the
# active prefix explicit before any find_package().
if(APPLE)
    find_program(BREW_EXECUTABLE brew)
    if(BREW_EXECUTABLE)
        execute_process(COMMAND "${BREW_EXECUTABLE}" --prefix
            OUTPUT_VARIABLE HOMEBREW_PREFIX
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        if(HOMEBREW_PREFIX AND IS_DIRECTORY "${HOMEBREW_PREFIX}")
            list(APPEND CMAKE_PREFIX_PATH "${HOMEBREW_PREFIX}")
        endif()
    endif()
endif()

# Enable Link Time Optimization (LTO) where the toolchain actually supports it.
include(CheckIPOSupported)
check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_ERROR)
if(IPO_SUPPORTED)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
else()
    message(STATUS "LTO disabled: ${IPO_ERROR}")
endif()

# Strict warnings, as errors, and full optimisation.
if(MSVC)
    add_compile_options(/W4 /WX /O2)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror -O3)
endif()

# Vendored libraries (third_party/: EnTT 3.12.2 and others) are SYSTEM
# includes, so -Werror applies only to the engine's own code.

# Find SDL2 core
find_package(SDL2 REQUIRED)
# FreeType renders the original's own fonts the way GDI did: hinted, one bit
# per pixel. It ships with every Linux desktop and with Homebrew.
find_package(Freetype REQUIRED)

file(GLOB_RECURSE SOURCES "src/*.cpp")
file(GLOB_RECURSE HEADERS "src/*.h" "src/*.hpp")

add_executable(OpenAoK ${SOURCES} ${HEADERS})

target_include_directories(OpenAoK PRIVATE src)

# Vendored single-header libraries (nlohmann/json, stb_image) are SYSTEM
# includes: they are not ours to keep warning-clean.
target_include_directories(OpenAoK SYSTEM PRIVATE third_party)



# SDL2's imported targets where its CMake package provides them; otherwise the
# variables an older FindSDL2 module sets.
if(TARGET SDL2::SDL2)
    target_link_libraries(OpenAoK PRIVATE SDL2::SDL2)
    # SDL2main is a no-op shim on Linux but wires up the real entry point on
    # macOS and Windows; link it whenever the package ships it.
    if(TARGET SDL2::SDL2main)
        target_link_libraries(OpenAoK PRIVATE SDL2::SDL2main)
    endif()
else()
    target_link_libraries(OpenAoK PRIVATE ${SDL2_LIBRARIES})
    target_include_directories(OpenAoK SYSTEM PRIVATE ${SDL2_INCLUDE_DIRS})
endif()

target_link_libraries(OpenAoK PRIVATE Freetype::Freetype)
