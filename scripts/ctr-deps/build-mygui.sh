#!/bin/bash
# MyGUIEngine 3.4.3 (static, no render system: OpenMW supplies its own).
source "$(dirname "$0")/common.sh"
fetch universe/m/mygui mygui_3.4.3+dfsg.orig.tar.gz
cd "${CTR_DEPS_WORK}"
rm -rf mygui-3.4.3+dfsg mygui-build && tar xzf mygui_3.4.3+dfsg.orig.tar.gz
cmake -S mygui-3.4.3+dfsg -B mygui-build -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="${REPO_ROOT}/cmake/CtrToolchain.cmake" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${CTR_DEPS_PREFIX}" -DMYGUI_STATIC=ON -DMYGUI_RENDERSYSTEM=1 \
    -DMYGUI_DISABLE_PLUGINS=ON -DMYGUI_BUILD_DEMOS=OFF -DMYGUI_BUILD_TOOLS=OFF -DMYGUI_BUILD_PLUGINS=OFF \
    -DMYGUI_DONT_USE_OBSOLETE=ON -DMYGUI_USE_FREETYPE=ON \
    -DFREETYPE_INCLUDE_DIRS="${DEVKITPRO}/portlibs/3ds/include/freetype2" \
    -DFREETYPE_LIBRARY="${DEVKITPRO}/portlibs/3ds/lib/libfreetype.a"
cmake --build mygui-build
cmake --install mygui-build
