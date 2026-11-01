#!/bin/sh
# Xcode's C++ and Objective-C++ compiler, and its linker, through ccache, when
# the project is written with OPENAOK_CCACHE on (ios/CMakeLists.txt).
exec ccache clang++ "$@"
