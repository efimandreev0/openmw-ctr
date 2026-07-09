#!/bin/bash
# Package OpenMW for Nintendo 3DS as .cia (and .3dsx for quick testing).
# Usage: make-cia.sh <build_dir> <source_dir>
set -e

BUILD_DIR="${1:?build dir}"
SOURCE_DIR="${2:?source dir}"

export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
TOOLS="${DEVKITPRO}/tools/bin"
CTR_DATA="${SOURCE_DIR}/data/ctr"
ELF="${BUILD_DIR}/apps/openmw/openmw.elf"
OUT_DIR="${BUILD_DIR}/apps/openmw"
ROMFS="${BUILD_DIR}/romfs"

echo "=== Assembling romfs ==="
rm -rf "${ROMFS}"
mkdir -p "${ROMFS}"
cp "${SOURCE_DIR}/apps/openmw/ctr/openmw.cfg" "${ROMFS}/openmw.cfg"
cp "${SOURCE_DIR}/apps/openmw/ctr/settings.cfg" "${ROMFS}/settings.cfg"
cp "${BUILD_DIR}/defaults.bin" "${ROMFS}/defaults.bin"
cp "${BUILD_DIR}/gamecontrollerdb.txt" "${ROMFS}/gamecontrollerdb.txt" 2>/dev/null || true
cp -r "${BUILD_DIR}/resources" "${ROMFS}/resources"

echo "=== Building SMDH / banner ==="
"${TOOLS}/bannertool" makesmdh -s "OpenMW" -l "OpenMW engine for Morrowind (New 3DS)" \
    -p "OpenMW team" -i "${CTR_DATA}/icon.png" -o "${OUT_DIR}/openmw.smdh"
"${TOOLS}/bannertool" makebanner -i "${CTR_DATA}/banner.png" -a "${CTR_DATA}/audio.wav" \
    -o "${OUT_DIR}/openmw.bnr"

echo "=== Building 3dsx (homebrew launcher testing) ==="
"${TOOLS}/3dsxtool" "${ELF}" "${OUT_DIR}/openmw.3dsx" \
    --smdh="${OUT_DIR}/openmw.smdh" --romfs="${ROMFS}"

echo "=== Building CIA ==="
"${TOOLS}/makerom" -f cia -o "${OUT_DIR}/openmw.cia" \
    -elf "${ELF}" \
    -rsf "${CTR_DATA}/openmw.rsf" \
    -icon "${OUT_DIR}/openmw.smdh" \
    -banner "${OUT_DIR}/openmw.bnr" \
    -exefslogo -target t \
    -DROMFS_ROOT="${ROMFS}"

echo "=== Building CXI (bootable directly in Citra) ==="
"${TOOLS}/makerom" -f cxi -o "${OUT_DIR}/openmw.cxi" \
    -elf "${ELF}" \
    -rsf "${CTR_DATA}/openmw.rsf" \
    -icon "${OUT_DIR}/openmw.smdh" \
    -banner "${OUT_DIR}/openmw.bnr" \
    -exefslogo -target t \
    -DROMFS_ROOT="${ROMFS}"

ls -la "${OUT_DIR}/openmw.cia" "${OUT_DIR}/openmw.3dsx"
echo "=== CIA complete: ${OUT_DIR}/openmw.cia ==="
