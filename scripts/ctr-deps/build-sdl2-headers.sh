#!/bin/bash
# SDL2 *headers only*. The 3DS build does not link SDL: the engine code keeps
# using SDL types/constants (keysyms, event structs, controller enums) and a
# tiny libctru-backed shim (components/ctr/sdlshim) implements the functions
# it calls. Configuring SDL for N3DS generates the matching SDL_config.h.
HERE="$(cd "$(dirname "$0")" && pwd)"
source "${HERE}/common.sh"
fetch main/libs/libsdl2 libsdl2_2.32.10+dfsg.orig.tar.gz
cd "${CTR_DEPS_WORK}"
rm -rf sdl2-src sdl2-build && mkdir sdl2-src && tar xzf libsdl2_2.32.10+dfsg.orig.tar.gz -C sdl2-src
cmake -S "$(ls -d sdl2-src/*)" -B sdl2-build -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="${REPO_ROOT}/cmake/CtrToolchain.cmake" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${CTR_DEPS_PREFIX}" -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF \
    -DSDL_AUDIO=OFF -DSDL_VIDEO=OFF -DSDL_RENDER=OFF -DSDL_HAPTIC=OFF -DSDL_SENSOR=OFF -DSDL_HIDAPI=OFF \
    -DSDL_POWER=OFF -DSDL_LOCALE=OFF -DSDL_MISC=OFF
cmake --build sdl2-build --target sdl_headers_copy 2>/dev/null || cmake --build sdl2-build
cmake --install sdl2-build
rm -f "${CTR_DEPS_PREFIX}"/lib/libSDL2*.a
