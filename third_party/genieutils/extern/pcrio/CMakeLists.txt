cmake_minimum_required(VERSION 3.0)

project(pcrio)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/")

if(NOT MSVC)
    add_definitions(-Wall -Wsign-compare)
endif()

add_library(pcrio SHARED pcrio.c)
set_target_properties(pcrio PROPERTIES PUBLIC_HEADER "pcrio.h;pcrdef.h")
install(TARGETS pcrio
        LIBRARY DESTINATION lib
        PUBLIC_HEADER DESTINATION include
)


find_package(Check)
if( CHECK_FOUND )
    add_subdirectory(tests)
endif()

