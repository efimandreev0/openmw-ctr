#!/bin/bash
# Fast incremental rebuild for OpenMW Vita.
#
# Skips cmake configure, ICU host check, and the separate OSG build step —
# make handles all of that incrementally via the eboot.bin-self target.
# If CMakeLists.txt changes, make will rerun cmake itself.
#
# Usage: scripts/vita/build-fast.sh [--vpk]
#   --vpk   Also build openmw.vpk (needed for first install / SFO updates)
#
# Requires that scripts/vita/build.sh has been run at least once to set up
# build-vita/.
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BUILD_DIR="${SRC_DIR}/build-vita"
VITASDK="${VITASDK:-/usr/local/vitasdk}"
VITAGL_DIR="${VITAGL_DIR:-${HOME}/vitaGL}"

export PATH="${VITASDK}/bin:${PATH}"
export PKG_CONFIG="${PKG_CONFIG:-arm-vita-eabi-pkg-config}"

if [ ! -f "${BUILD_DIR}/Makefile" ]; then
    echo "ERROR: ${BUILD_DIR}/Makefile not found."
    echo "Run scripts/vita/build.sh once to do the initial cmake configure."
    exit 1
fi

WANT_VPK=0
for arg in "$@"; do
    case "${arg}" in
        --vpk) WANT_VPK=1 ;;
        *) echo "Unknown arg: ${arg}"; exit 1 ;;
    esac
done

cd "${BUILD_DIR}"

echo "=== Building eboot.bin (-j$(nproc)) ==="
make -j"$(nproc)" eboot.bin-self

if [ "${WANT_VPK}" -eq 1 ]; then
    echo "=== Building VPK ==="
    make -j"$(nproc)" openmw.vpk-vpk
fi

echo ""
echo "=== Build Complete ==="
echo "eboot.bin: ${BUILD_DIR}/apps/openmw/eboot.bin"
[ "${WANT_VPK}" -eq 1 ] && echo "VPK:       ${BUILD_DIR}/apps/openmw/openmw.vpk"
