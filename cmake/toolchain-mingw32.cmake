# CMake toolchain file for cross-compiling OrderDB from Linux to 32-bit
# Windows using MinGW-w64. Same threading-model note applies as in
# toolchain-mingw64.cmake - see that file for details.
#
# Usage (from the project root):
#   mkdir build-win32 && cd build-win32
#   cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain-mingw32.cmake
#   make -j4
#
# You only need this if you specifically need a 32-bit .exe (e.g. a very
# old Windows machine). Otherwise prefer toolchain-mingw64.cmake - it runs
# on any 64-bit Windows (which is effectively all Windows 10/11 machines).

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86)

set(TOOLCHAIN_PREFIX i686-w64-mingw32)

find_program(MINGW32_CXX_COMPILER NAMES ${TOOLCHAIN_PREFIX}-g++-posix ${TOOLCHAIN_PREFIX}-g++)
find_program(MINGW32_C_COMPILER   NAMES ${TOOLCHAIN_PREFIX}-gcc-posix ${TOOLCHAIN_PREFIX}-gcc)

if(NOT MINGW32_CXX_COMPILER)
    message(FATAL_ERROR "Could not find ${TOOLCHAIN_PREFIX}-g++ (or -g++-posix). "
                         "Install the MinGW-w64 cross toolchain first - see README_WINDOWS_BUILD.md.")
endif()

set(CMAKE_C_COMPILER   ${MINGW32_C_COMPILER})
set(CMAKE_CXX_COMPILER ${MINGW32_CXX_COMPILER})
set(CMAKE_RC_COMPILER  ${TOOLCHAIN_PREFIX}-windres)

set(CMAKE_FIND_ROOT_PATH /usr/${TOOLCHAIN_PREFIX})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(CMAKE_EXE_LINKER_FLAGS "-static -static-libgcc -static-libstdc++")
