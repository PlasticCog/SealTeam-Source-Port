cmake_minimum_required(VERSION 3.16)
project(SealTeam VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

find_package(SDL2 REQUIRED)

file(GLOB_RECURSE SEALTEAM_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)

add_executable(sealteam ${SEALTEAM_SOURCES})
target_include_directories(sealteam PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(TARGET SDL2::SDL2main)
    target_link_libraries(sealteam PRIVATE SDL2::SDL2main)
endif()
if(TARGET SDL2::SDL2)
    target_link_libraries(sealteam PRIVATE SDL2::SDL2)
else()
    target_include_directories(sealteam PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(sealteam PRIVATE ${SDL2_LIBRARIES})
endif()

if(MSVC)
    target_compile_options(sealteam PRIVATE /W4 /permissive-)
    target_compile_definitions(sealteam PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(sealteam PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# Copy SDL2 runtime next to the executable on Windows so it runs from the build dir.
if(WIN32 AND TARGET SDL2::SDL2)
    add_custom_command(TARGET sealteam POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:sealteam>
        COMMAND_EXPAND_LISTS)
endif()
