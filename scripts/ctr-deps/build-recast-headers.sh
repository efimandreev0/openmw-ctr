#!/bin/bash
# Recast/Detour *headers only*: the 3DS build uses DetourNavigator's stub
# navigator (AI falls back to pathgrids, like vanilla Morrowind), but the game
# logic still uses its value types, which are defined with Recast constants.
HERE="$(cd "$(dirname "$0")" && pwd)"
source "${HERE}/common.sh"
fetch universe/r/recastnavigation recastnavigation_1.6.0+dfsg.orig.tar.gz
cd "${CTR_DEPS_WORK}"
rm -rf recast-src && mkdir recast-src && tar xzf recastnavigation_1.6.0+dfsg.orig.tar.gz -C recast-src
SRC="$(ls -d recast-src/*)"
mkdir -p "${CTR_DEPS_PREFIX}/include/recastnavigation"
cp "${SRC}"/Recast/Include/*.h "${SRC}"/Detour/Include/*.h "${SRC}"/DebugUtils/Include/*.h \
    "${CTR_DEPS_PREFIX}/include/recastnavigation/"
