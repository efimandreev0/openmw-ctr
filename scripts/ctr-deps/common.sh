#!/bin/bash
# Shared settings for the Nintendo 3DS dependency builds.
# Sources come from the Ubuntu archive (orig tarballs) so that the build does
# not need GitHub; everything is installed into ${CTR_DEPS_PREFIX}.
set -e
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITARM="${DEVKITARM:-${DEVKITPRO}/devkitARM}"
CTR_DEPS_PREFIX="${CTR_DEPS_PREFIX:-/opt/ctr-deps}"
CTR_DEPS_WORK="${CTR_DEPS_WORK:-${HOME}/ctr-deps-build}"
UBUNTU_POOL="${UBUNTU_POOL:-http://archive.ubuntu.com/ubuntu/pool}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

CTR_CC="${DEVKITARM}/bin/arm-none-eabi-gcc"
CTR_CXX="${DEVKITARM}/bin/arm-none-eabi-g++"
CTR_AR="${DEVKITARM}/bin/arm-none-eabi-ar"

# Must match cmake/CtrToolchain.cmake: every C++ object linked into openmw has
# to see the same int32_t/uint32_t types (they change C++ mangling).
CTR_FLAGS="-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft -mword-relocations \
-ffunction-sections -fdata-sections -D__3DS__ -include ${REPO_ROOT}/components/ctr/ctr_int32_fix.h \
-Os -DNDEBUG -fomit-frame-pointer -fno-math-errno -fno-trapping-math -fno-signed-zeros"

mkdir -p "${CTR_DEPS_PREFIX}/lib" "${CTR_DEPS_PREFIX}/include" "${CTR_DEPS_WORK}"

fetch() {
    # fetch <pool path> <file>
    local url="${UBUNTU_POOL}/$1/$2"
    if [ ! -s "${CTR_DEPS_WORK}/$2" ]; then
        for i in 1 2 3 4 5; do
            curl -sSfL -o "${CTR_DEPS_WORK}/$2.part" "${url}" && mv "${CTR_DEPS_WORK}/$2.part" "${CTR_DEPS_WORK}/$2" && return 0
            sleep $((i * 5))
        done
        echo "failed to download ${url}" >&2
        return 1
    fi
}
