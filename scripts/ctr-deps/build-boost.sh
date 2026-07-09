#!/bin/bash
# Build/install Boost for OpenMW 3DS (CTR) port — devkitARM
# Mirrors scripts/vita-deps/build-boost.sh
set -e

trap 'rm -f "${WORKDIR}/boost_done"' ERR

export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITARM="${DEVKITARM:-${DEVKITPRO}/devkitARM}"
SYSROOT="${DEVKITPRO}/portlibs/3ds"

echo "=== Installing Boost for 3DS ==="

WORKDIR="${1:-$(pwd)/ctr-deps-build/boost}"
BOOST_VERSION="1.78.0"
BOOST_VERSION_UNDERSCORE="1_78_0"

mkdir -p "${WORKDIR}"
cd "${WORKDIR}"

if [ ! -f boost_done ]; then
    if [ ! -f "boost_${BOOST_VERSION_UNDERSCORE}.tar.bz2" ]; then
        wget -q "https://archives.boost.io/release/${BOOST_VERSION}/source/boost_${BOOST_VERSION_UNDERSCORE}.tar.bz2"
    fi

    rm -rf "boost_${BOOST_VERSION_UNDERSCORE}"
    tar xf "boost_${BOOST_VERSION_UNDERSCORE}.tar.bz2"
    cd "boost_${BOOST_VERSION_UNDERSCORE}"

    # 3DS (ARM11 MPCore, ARMv6K, VFPv2 hard-float, no mmap)
    cat > user-config.jam << JAMEOF
using gcc : ctr : ${DEVKITARM}/bin/arm-none-eabi-g++ :
    <compileflags>"-O2 -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft -D__3DS__ -ffunction-sections -fdata-sections -fvisibility=hidden -DBOOST_IOSTREAMS_NO_MAPPED_FILE"
    <archiver>${DEVKITARM}/bin/arm-none-eabi-ar
    <ranlib>${DEVKITARM}/bin/arm-none-eabi-ranlib
;
JAMEOF

    # 3DS: no mmap — stub out mapped_file (same trick as Vita)
    echo "// 3DS: no mmap support" > libs/iostreams/src/mapped_file.cpp

    ./bootstrap.sh

    ./b2 \
        --user-config=user-config.jam \
        toolset=gcc-ctr \
        target-os=linux \
        runtime-link=static \
        link=static \
        threading=multi \
        variant=release \
        --with-iostreams \
        --with-program_options \
        --prefix="${SYSROOT}" \
        --includedir="${SYSROOT}/include" \
        --libdir="${SYSROOT}/lib" \
        -j$(nproc | awk '{print ($1 > 8) ? 8 : $1}') \
        install

    if [ ! -f "${SYSROOT}/lib/libboost_iostreams.a" ] || [ ! -f "${SYSROOT}/lib/libboost_program_options.a" ]; then
        echo "ERROR: Boost installation failed - libraries not found in ${SYSROOT}/lib"
        exit 1
    fi

    cd ..
    touch boost_done
fi

echo "=== Boost installation complete ==="
