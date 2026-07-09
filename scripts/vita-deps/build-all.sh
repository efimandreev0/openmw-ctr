#!/bin/bash
# Master build script for all OpenMW Vita dependencies
#
# Dependencies that come from VitaSDK (no build needed):
#   zlib, SDL2, OpenAL, LZ4, FreeType
#
# Dependencies built by OpenMW's FetchContent (no pre-build needed):
#   Bullet, OpenSceneGraph, MyGUI, RecastNavigation, SQLite3, yaml-cpp
#
# Dependencies this script installs:
#   vitaGL                 (OpenGL-compatible rendering library)
#   PVR_PSP2               (pre-built GPU driver stubs)
#   Boost                  (source build)
#   LuaJIT                 (source build)
#   FFmpeg                 (source build)
#   ICU                    (source build)
#
# Usage: ./build-all.sh [work_dir] [vitagl_dir]
#   work_dir defaults to ./vita-deps-build/
#   vitagl_dir defaults to ~/vitaGL
#
# Prerequisites:
#   - VitaSDK installed and VITASDK env var set (or at /usr/local/vitasdk)
#   - wget, unzip, git, make, cmake
#   - gcc-multilib, g++-multilib (for LuaJIT and ICU host tools)
#   - ~5GB free disk space
#   - 1-2 hours for first build
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKDIR="${1:-$(pwd)/vita-deps-build}"
VITAGL_DIR="${2:-${HOME}/vitaGL}"
VITASDK="${VITASDK:-/usr/local/vitasdk}"

echo "========================================"
echo " OpenMW Vita Dependencies Builder"
echo "========================================"
echo "VitaSDK:    ${VITASDK}"
echo "vitaGL:     ${VITAGL_DIR}"
echo "Work dir:   ${WORKDIR}"
echo "Script dir: ${SCRIPT_DIR}"
echo ""

if [ ! -d "${VITASDK}" ]; then
    echo "ERROR: VitaSDK not found at ${VITASDK}"
    echo "Install from https://vitasdk.org/ or set VITASDK env var"
    exit 1
fi

export VITASDK
export VITAGL_DIR
export PATH="${VITASDK}/bin:${PATH}"

mkdir -p "${WORKDIR}"

# Track status
FAILED=""

run_script() {
    local name="$1"
    local script="$2"
    shift 2
    echo ""
    echo "========================================"
    echo " Building: ${name}"
    echo "========================================"
    if bash "${script}" "$@"; then
        echo "[OK] ${name} complete"
    else
        echo "[FAIL] ${name} failed!"
        FAILED="${FAILED} ${name}"
    fi
}

# Build in dependency order
# vitaGL must be built first (required by OpenMW build)
run_script "vitaGL" "${SCRIPT_DIR}/build-vitagl.sh" "${VITAGL_DIR}"
run_script "pvr-psp2" "${SCRIPT_DIR}/build-pvr-psp2.sh" "${WORKDIR}/pvr-psp2"
run_script "boost" "${SCRIPT_DIR}/build-boost.sh" "${WORKDIR}/boost"
run_script "luajit" "${SCRIPT_DIR}/build-luajit.sh" "${WORKDIR}/luajit"
run_script "ffmpeg" "${SCRIPT_DIR}/build-ffmpeg.sh" "${WORKDIR}/ffmpeg"
run_script "icu" "${SCRIPT_DIR}/build-icu.sh" "${WORKDIR}/icu"

echo ""
echo "========================================"
echo " Summary"
echo "========================================"

if [ -z "${FAILED}" ]; then
    echo "All dependencies built successfully!"
    echo ""
    echo "Next steps:"
    echo "  cd /path/to/openmw"
    echo "  export VITAGL_DIR=${VITAGL_DIR}"
    echo "  scripts/vita/build.sh"
else
    echo "FAILED:${FAILED}"
    echo "Fix the errors above and re-run this script."
    exit 1
fi
