# ctrprep is a PC tool, built on its own (not with the 3DS toolchain of the main project):
#   cmake -S apps/ctrprep -B build-ctrprep -DCMAKE_BUILD_TYPE=Release && cmake --build build-ctrprep
# scripts/ctr/build-ctrprep.sh also cross-builds the Windows .exe with MinGW.
cmake_minimum_required(VERSION 3.16)
project(ctrprep CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(OPENMW_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

add_executable(ctrprep
    main.cpp
    dspadpcm.cpp
    ${OPENMW_ROOT}/components/ctr/gpu/texconvert.cpp
    ${OPENMW_ROOT}/components/ctr/gpu/imagedecode.cpp
)
target_include_directories(ctrprep PRIVATE ${OPENMW_ROOT})
# The shared texture code without the engine's VFS and logging.
target_compile_definitions(ctrprep PRIVATE OPENMW_CTR_PREP)

find_package(Threads REQUIRED)
target_link_libraries(ctrprep PRIVATE Threads::Threads)

if(WIN32)
    # wmain: paths with any characters.
    if(MINGW)
        target_link_options(ctrprep PRIVATE -municode -static)
    else()
        target_compile_definitions(ctrprep PRIVATE _UNICODE UNICODE)
    endif()
endif()
