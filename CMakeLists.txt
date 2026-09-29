cmake_minimum_required(VERSION 3.16)
project(SealTeam VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# --- SDL3: use an installed package if there is one, otherwise build it from source.
option(SEALTEAM_FETCH_SDL3 "Download and build SDL3 if no installed SDL3 is found" ON)
find_package(SDL3 CONFIG QUIET)
if(NOT SDL3_FOUND)
    if(NOT SEALTEAM_FETCH_SDL3)
        message(FATAL_ERROR "SDL3 not found. Install it or enable SEALTEAM_FETCH_SDL3.")
    endif()
    message(STATUS "SDL3 not found - fetching and building it from source")
    include(FetchContent)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.2.28
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(SDL3)
endif()

# --- Main executable
file(GLOB_RECURSE SEALTEAM_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)
list(FILTER SEALTEAM_SOURCES EXCLUDE REGEX "/src/tools/")  # standalone test tools

add_executable(sealteam ${SEALTEAM_SOURCES})
target_include_directories(sealteam PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(sealteam PRIVATE SDL3::SDL3)

if(MSVC)
    target_compile_options(sealteam PRIVATE /W4 /permissive-)
    target_compile_definitions(sealteam PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(sealteam PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
if(MINGW)
    # Self-contained exe: no libgcc/libstdc++/winpthread DLLs needed.
    target_link_options(sealteam PRIVATE -static-libgcc -static-libstdc++)
    if(NOT TARGET SDL3::SDL3-shared)
        target_link_options(sealteam PRIVATE -static)
    endif()
endif()

# A shared SDL3 (installed package) must sit next to the executable on Windows.
if(WIN32 AND TARGET SDL3::SDL3-shared)
    add_custom_command(TARGET sealteam POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3-shared> $<TARGET_FILE_DIR:sealteam>)
endif()
