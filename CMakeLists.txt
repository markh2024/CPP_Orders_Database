cmake_minimum_required(VERSION 3.15)

project(OrderDB CXX)


set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


include_directories(${CMAKE_SOURCE_DIR}/include)



# ----------------------------------------------------
# Output directory
# ----------------------------------------------------
# Kept separate per target so cross-compiling for Windows doesn't overwrite
# the native Linux build (or the other Windows architecture) sitting in the
# same source tree - dist/ stays exactly where it's always been for the
# native build; cross-builds land in their own dist-winXX/ folders.

if(CMAKE_CROSSCOMPILING AND CMAKE_SYSTEM_NAME STREQUAL "Windows")
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
        set(DIST_DIR ${CMAKE_SOURCE_DIR}/dist-win64)
    else()
        set(DIST_DIR ${CMAKE_SOURCE_DIR}/dist-win32)
    endif()
else()
    set(DIST_DIR ${CMAKE_SOURCE_DIR}/dist)
endif()


set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${DIST_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${DIST_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${DIST_DIR})



# ----------------------------------------------------
# Create data directory inside dist
# ----------------------------------------------------

file(MAKE_DIRECTORY ${DIST_DIR}/data)



# ----------------------------------------------------
# Core library
# ----------------------------------------------------

add_library(orderdb_core

    src/StorageEngine.cpp
    src/AuthManager.cpp
    src/Socket.cpp

)



find_package(Threads REQUIRED)

target_link_libraries(orderdb_core PUBLIC Threads::Threads)



if(WIN32)

    target_link_libraries(orderdb_core PUBLIC ws2_32)

endif()



# ----------------------------------------------------
# Server executable
# ----------------------------------------------------

add_executable(orderdb_server

    src/server_main.cpp

)


target_link_libraries(orderdb_server PRIVATE orderdb_core)



# ----------------------------------------------------
# Client executable
# ----------------------------------------------------

add_executable(orderdb_client

    src/client_main.cpp

)


target_link_libraries(orderdb_client PRIVATE orderdb_core)
