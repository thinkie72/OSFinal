cmake_minimum_required(VERSION 4.2)
project(OSFinal C)

set(CMAKE_C_STANDARD 11)

# ---- raylib ----
# Pulled in via FetchContent so there's no system install step.
# First configure will clone raylib (a few minutes); subsequent builds are cached.
include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        5.5
)
FetchContent_MakeAvailable(raylib)

add_executable(OSFinal
    main.c
    setup.c
    simulation_thread.c
)

target_link_libraries(OSFinal PRIVATE raylib)

# pthreads — needed by thread_compat.h on macOS / Linux.
if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(OSFinal PRIVATE Threads::Threads)
endif()
if(UNIX AND NOT APPLE)
    target_link_libraries(OSFinal PRIVATE m)
endif()

# Make data.csv and logos/ available next to the binary at run time.
configure_file(${CMAKE_SOURCE_DIR}/data.csv
               ${CMAKE_BINARY_DIR}/data.csv COPYONLY)

file(GLOB LOGO_FILES "${CMAKE_SOURCE_DIR}/logos/*.png")
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/logos)
foreach(LOGO ${LOGO_FILES})
    get_filename_component(LOGO_NAME ${LOGO} NAME)
    configure_file(${LOGO} ${CMAKE_BINARY_DIR}/logos/${LOGO_NAME} COPYONLY)
endforeach()
