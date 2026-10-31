# The dat dumper: reads EMPIRES2.DAT through genieutils and prints the tables
# the extractors use as JSON. Built by datfile.py.
cmake_minimum_required(VERSION 3.16)
project(OpenAoKDataTools CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(GUTILS_TOOLS OFF CACHE BOOL "" FORCE)
set(GUTILS_TEST OFF CACHE BOOL "" FORCE)
set(GUTILS_STATIC ON CACHE BOOL "" FORCE)
# genieutils' sources; a build for Windows names a copy with its extern/
# win-iconv added (windows/build.sh).
set(GENIEUTILS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/genieutils" CACHE PATH "genieutils' sources")
add_subdirectory(${GENIEUTILS_DIR} genieutils)

add_executable(dump_dat dump_dat.cpp)
target_include_directories(dump_dat PRIVATE ${GENIEUTILS_DIR}/include)
if(MINGW)
    target_link_options(dump_dat PRIVATE -static-libgcc -static-libstdc++ -static)
endif()
target_link_libraries(dump_dat PRIVATE genieutils)
