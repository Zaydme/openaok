cmake_minimum_required(VERSION 3.20)

# The iOS app: the engine's sources, with SDL 2 and FreeType built here from
# source (ios/fetch_deps.sh), linked into OpenAoK.app.
# ios/build.sh configures this and writes ios/build/OpenAoK.xcodeproj.
#
#   cmake -G Xcode -S ios -B ios/build -DCMAKE_SYSTEM_NAME=iOS
#
# SDL's uikit main starts the application and calls the engine's main
# (src/main.cpp, renamed SDL_main by SDL_main.h), as SDL's SDL2main does on a
# desktop.

if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    message(FATAL_ERROR
        "Configure with -DCMAKE_SYSTEM_NAME=iOS (ios/build.sh does):\n"
        "  cmake -G Xcode -S ios -B ios/build -DCMAKE_SYSTEM_NAME=iOS")
endif()

# The oldest iOS the app runs on and the architecture it is built for.
# CMAKE_OSX_SYSROOT is iphoneos for a device, iphonesimulator for the
# simulator; either may be given on the command line instead.
set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "The oldest iOS OpenAoK runs on")
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "The architectures to build")

project(OpenAoK VERSION 0.1.0 LANGUAGES C CXX OBJC OBJCXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
endif()

set(REPO ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(DEPS ${CMAKE_CURRENT_SOURCE_DIR}/deps)
set(SDL_DIR ${DEPS}/SDL2-2.32.10)
set(FREETYPE_DIR ${DEPS}/freetype-VER-2-13-3)

foreach(dep ${SDL_DIR} ${FREETYPE_DIR})
    if(NOT IS_DIRECTORY ${dep})
        message(FATAL_ERROR "${dep} is missing: run ios/fetch_deps.sh first.")
    endif()
endforeach()

# SDL2, static. Its uikit main (src/main/uikit/SDL_uikit_main.c) is part of the
# library, so the app target needs no entry point of its own.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL2_DISABLE_SDL2MAIN OFF CACHE BOOL "" FORCE)
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

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS ${REPO}/src/*.cpp)
file(GLOB IOS_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/platform/*.mm)

add_executable(OpenAoK MACOSX_BUNDLE ${SOURCES} ${IOS_SOURCES})

target_include_directories(OpenAoK PRIVATE ${REPO}/src)
# Vendored single-header libraries (nlohmann/json, stb_image) are SYSTEM
# includes: they are not ours to keep warning-clean.
target_include_directories(OpenAoK SYSTEM PRIVATE ${REPO}/third_party)

# The desktop build's warnings, but not as errors: this compiler is not the
# one the code is kept warning-clean against.
target_compile_options(OpenAoK PRIVATE
    -O3 -Wall -Wextra -Wno-missing-designated-field-initializers)

# The version of the game's data this build plays from: prepare_data.py's, as
# android/app/build.gradle reads it. The folder ios/package.py makes carries it
# in version.txt, which the app checks when it copies the data in.
file(READ ${REPO}/tools/data/prepare_data.py PREPARE_DATA)
if(NOT PREPARE_DATA MATCHES "STAMP_VERSION = ([0-9]+)")
    message(FATAL_ERROR "tools/data/prepare_data.py names no STAMP_VERSION")
endif()
set(OPENAOK_DATA_VERSION ${CMAKE_MATCH_1})
target_compile_definitions(OpenAoK PRIVATE OPENAOK_DATA_VERSION=${OPENAOK_DATA_VERSION})
message(STATUS "The game's data version is ${OPENAOK_DATA_VERSION}")

set_source_files_properties(${IOS_SOURCES} PROPERTIES COMPILE_FLAGS "-fobjc-arc")

# SDL2main holds SDL's main, which starts the app and calls the engine's
# (SDL_main). It has to be linked: nothing in the app names main itself.
target_link_libraries(OpenAoK PRIVATE SDL2::SDL2main SDL2::SDL2-static freetype)

# zlib: the Indeo 5 decoder and the app's checking of the game's data against
# its manifest. iOS ships it in the system library. find_library caches an
# absolute path inside the SDK; a new Xcode moves that SDK, so forget the old
# path on each configure and look in the one being built against.
unset(ZLIB_LIBRARY CACHE)
find_library(ZLIB_LIBRARY z REQUIRED)
target_link_libraries(OpenAoK PRIVATE ${ZLIB_LIBRARY})

# The frameworks the app's own code uses; SDL's target links the rest. Their
# cached paths move with the SDK too.
foreach(framework Foundation UIKit CoreGraphics QuartzCore UniformTypeIdentifiers)
    unset(FRAMEWORK_${framework} CACHE)
    find_library(FRAMEWORK_${framework} ${framework} REQUIRED)
    target_link_libraries(OpenAoK PRIVATE ${FRAMEWORK_${framework}})
endforeach()

# The bundle: the game's name, id and version, and the Info.plist's landscape
# screen, the Files app's view of Documents, and the launch screen.
set_target_properties(OpenAoK PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
    MACOSX_BUNDLE_BUNDLE_NAME "OpenAoK"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.openaok.game"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    XCODE_ATTRIBUTE_PRODUCT_NAME "OpenAoK"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "org.openaok.game"
    XCODE_ATTRIBUTE_MARKETING_VERSION "${PROJECT_VERSION}"
    XCODE_ATTRIBUTE_CURRENT_PROJECT_VERSION "1"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    XCODE_ATTRIBUTE_GENERATE_INFOPLIST_FILE "NO"
    XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC "YES"
    XCODE_ATTRIBUTE_CLANG_ENABLE_MODULES "YES"
    XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
    XCODE_ATTRIBUTE_ENABLE_BITCODE "NO"
    # A scheme of its own, so Xcode offers Run, Test and Profile for the app
    # rather than only CMake's ALL_BUILD and ZERO_CHECK targets.
    XCODE_GENERATE_SCHEME TRUE
)

# The Apple team to sign with, for a build that goes straight onto a device.
# Left empty, Xcode builds unsigned (ios/build.sh --device) for AltStore to
# sign, or asks for a team when the project is opened.
set(OPENAOK_TEAM_ID "" CACHE STRING "Apple development team id, for signing on a device")
if(OPENAOK_TEAM_ID)
    set_target_properties(OpenAoK PROPERTIES
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${OPENAOK_TEAM_ID}")
endif()
