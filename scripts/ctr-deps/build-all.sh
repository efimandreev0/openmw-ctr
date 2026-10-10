#!/bin/bash
# Build every third-party dependency of the Nintendo 3DS (Old 3DS) target into
# ${CTR_DEPS_PREFIX} (default /opt/ctr-deps). Requires devkitPro with 3ds-dev
# and 3ds-portlibs (freetype, zlib, lz4, libmpg123, yaml-cpp ...).
set -e
here="$(dirname "$0")"
bash "${here}/build-boost.sh"
bash "${here}/build-bullet.sh"
bash "${here}/build-mygui.sh"
bash "${here}/build-osglite.sh"
bash "${here}/build-sdl2-headers.sh"
bash "${here}/build-recast-headers.sh"
