# CMake toolchain file for cross-compiling OrderDB from Linux to 64-bit
# Windows using MinGW-w64.
#
# Usage (from the project root):
#   mkdir build-win64 && cd build-win64
#   cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain-mingw64.cmake
#   make -j4
#
# THREADING MODEL NOTE: the server uses std::thread (one thread per client
# connection), which requires MinGW's "posix" threading model - the older
# "win32" threading model doesn't implement enough of C++11 threading to
# support it. Different distros package this differently:
#   - openSUSE's mingw64-cross-gcc-c++ ships a single x86_64-w64-mingw32-g++
#     binary that is already built posix-threaded - no suffix needed.
#   - Debian/Ubuntu's g++-mingw-w64-x86-64 ships BOTH variants side by side
#     as x86_64-w64-mingw32-g++-posix and -win32, defaulting to -win32.
# The detection below tries the -posix suffix first (needed on Debian/Ubuntu)
# and falls back to the plain name (correct on openSUSE) so this file works
# on either without editing. If you ever see link errors mentioning thread
# symbols, or the server hangs/crashes as soon as a client connects, check
# which one actually got picked with `cmake -LA | grep COMPILER`.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(TOOLCHAIN_PREFIX x86_64-w64-mingw32)

find_program(MINGW64_CXX_COMPILER NAMES ${TOOLCHAIN_PREFIX}-g++-posix ${TOOLCHAIN_PREFIX}-g++)
find_program(MINGW64_C_COMPILER   NAMES ${TOOLCHAIN_PREFIX}-gcc-posix ${TOOLCHAIN_PREFIX}-gcc)

if(NOT MINGW64_CXX_COMPILER)
    message(FATAL_ERROR "Could not find ${TOOLCHAIN_PREFIX}-g++ (or -g++-posix). "
                         "Install the MinGW-w64 cross toolchain first - see README_WINDOWS_BUILD.md.")
endif()

set(CMAKE_C_COMPILER   ${MINGW64_C_COMPILER})
set(CMAKE_CXX_COMPILER ${MINGW64_CXX_COMPILER})
set(CMAKE_RC_COMPILER  ${TOOLCHAIN_PREFIX}-windres)

set(CMAKE_FIND_ROOT_PATH /usr/${TOOLCHAIN_PREFIX})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Statically link the MinGW runtime (libgcc, libstdc++, winpthread) into the
# .exe so it runs on a bare Windows machine without needing those DLLs
# installed alongside it. Without this, you'd need to ship
# libgcc_s_seh-1.dll / libstdc++-6.dll / libwinpthread-1.dll next to the
# .exe, which is easy to forget and fails silently with a missing-DLL error.
set(CMAKE_EXE_LINKER_FLAGS "-static -static-libgcc -static-libstdc++")
